/**
 * @file ring_buffer.cpp
 * @brief 预录 GOP 环形缓冲区实现
 *
 * 为执法记录仪的"预录"功能提供底层存储:
 *   - 即便用户未按下录像键，后台也持续将编码后的 GOP 写入环形缓冲
 *   - 用户按下录像键时，先 Flush 缓冲区内容到文件，再继续写入新 GOP
 *   - 实现"按下录像键前 N 秒已被记录"的关键体验
 *
 * 缓冲区模型:
 *   ┌──────────────────────────────────────────────────────┐
 *   │ Slot[0] │ Slot[1] │ ... │ Slot[28] │ Slot[29] │
 *   └──────────────────────────────────────────────────────┘
 *         ↑ write_index (环形递增，取模 kMaxGopSlots)
 *         每个 Slot 最多存 2MB GOP 数据
 */

#include "utils/ring_buffer.h"
#include "utils/logger.h"
#include <cstring>
#include <unistd.h>

namespace leavr {

/**
 * @brief 初始化预录缓冲区
 *
 * 根据期望的预录时长分配 slot 数量，并非实际分配 GOP 内存
 * (buffer_ 在构造函数中已一次性分配 kTotalSize 字节)。
 *
 * @param pre_record_sec  预录时长(秒)，0 表示禁用预录，
 *                        超过 kMaxGopSlots(30) 则截断
 * @return LEAVR_OK
 */
int PreRecordBuffer::Init(int pre_record_sec) {
    // 禁用预录: max_slots_=0 使 WriteGop/FlushToFile 直接返回
    if (pre_record_sec <= 0) {
        max_slots_ = 0;
        gop_count_ = 0;
        return LEAVR_OK;
    }

    // 上限保护: 不超过编译期常量 kMaxGopSlots (30s)
    if (pre_record_sec > static_cast<int>(kMaxGopSlots)) {
        pre_record_sec = kMaxGopSlots;
    }

    max_slots_ = static_cast<uint32_t>(pre_record_sec);
    gop_count_ = 0;
    write_index_ = 0;

    // 将所有 slot 标记为无效 (is_valid=false 的 slot 不会被 Flush)
    for (uint32_t i = 0; i < kMaxGopSlots; i++) {
        buffer_[i].is_valid = false;
        buffer_[i].size = 0;
    }

    LOG_INFO("PreRecordBuffer: Init %d sec (%u slots, %zu MB)",
             pre_record_sec, max_slots_,
             (kTotalSize / (1024 * 1024)));
    return LEAVR_OK;
}

/**
 * @brief 写入一个 GOP 到环形缓冲区
 *
 * 由编码线程在每完成一个 GOP 编码后调用。
 * 无论当前是否在录像中，GOP 都会写入缓冲区 ——
 * 如果未在录像，缓冲区满了旧的会被覆盖；如果正在录像，
 * 新的 GOP 同时写入文件和缓冲区，停止时不需要 Flush。
 *
 * 线程安全: pthread_mutex_lock 保护，与 FlushToFile/Clear 互斥。
 *
 * @param data   编码后的 H.264/H.265 数据指针
 * @param size   数据字节数，不得为 0 且不得超过 kMaxGopSize(2MB)
 * @param pts_ms 该 GOP 的 PTS (Presentation Timestamp)，毫秒
 * @return LEAVR_OK 成功, LEAVR_ERR_PARAM 参数无效
 */
int PreRecordBuffer::WriteGop(const uint8_t* data, uint32_t size, uint64_t pts_ms) {
    // 预录已禁用，直接返回成功
    if (max_slots_ == 0) return LEAVR_OK;

    // 参数校验
    if (!data || size == 0 || size > kMaxGopSize) {
        return LEAVR_ERR_PARAM;
    }

    pthread_mutex_lock(&lock_);

    // 获取当前写入位置 (relaxed: 仅在锁内访问，不需要同步)
    uint32_t idx = write_index_.load(std::memory_order_relaxed);
    GopSlot& slot = buffer_[idx];

    // 拷贝 GOP 数据 (必须拷贝，因为 data 可能指向编码器的内部缓冲区)
    memcpy(slot.data, data, size);
    slot.size = size;
    slot.pts_ms = pts_ms;
    slot.is_valid = true;

    // 环形递增写入指针: idx = (idx + 1) % kMaxGopSlots
    idx = (idx + 1) % kMaxGopSlots;
    write_index_.store(idx, std::memory_order_release);

    // 更新有效 GOP 计数 (不超过 max_slots_)
    // 当缓冲区满时，count 保持 max_slots_，最旧的 slot 被静默覆盖
    uint32_t count = gop_count_.load(std::memory_order_relaxed);
    if (count < max_slots_) {
        gop_count_.store(count + 1, std::memory_order_release);
    }

    pthread_mutex_unlock(&lock_);
    return LEAVR_OK;
}

/**
 * @brief 将缓冲区中所有缓存的 GOP 写入文件
 *
 * 在用户按下录像键时调用，将预录的历史 GOP 写入录像文件开头。
 * 写入后清除所有 slot，使后续的 GOP 直接从缓冲区开头覆盖。
 *
 * 写入顺序: 从最旧的 GOP 开始，按时间顺序写入 (FIFO)。
 * 起始索引 = (write_index - gop_count + kMaxGopSlots) % kMaxGopSlots
 * 环形回绕处理: 当 write_index 已回绕过时，起始位置在 write_index 之后。
 *
 * 线程安全: pthread_mutex_lock 保护。
 *
 * @param fd  目标文件描述符 (已打开的 .tmp 录像文件)
 * @return LEAVR_OK 成功, LEAVR_ERR_PARAM 参数无效
 */
int PreRecordBuffer::FlushToFile(int fd) {
    if (fd < 0) return LEAVR_ERR_PARAM;
    if (max_slots_ == 0) return LEAVR_OK;

    pthread_mutex_lock(&lock_);

    uint32_t count = gop_count_.load(std::memory_order_acquire);
    if (count == 0) {
        pthread_mutex_unlock(&lock_);
        return LEAVR_OK;
    }

    // 环形物理地址回绕: 起始索引 = (write_index - count) mod kMaxGopSlots
    // 示例: write_index=5, count=3, kMaxGopSlots=30 → start_idx=2 (slots 2,3,4)
    // 示例: write_index=2, count=5, kMaxGopSlots=30 → start_idx=27 (slots 27,28,29,0,1)
    uint32_t wr_idx = write_index_.load(std::memory_order_acquire);
    uint32_t start_idx = (wr_idx + kMaxGopSlots - count) % kMaxGopSlots;

    LOG_INFO("PreRecordBuffer: Flushing %u GOPs to fd=%d, start_idx=%u",
             count, fd, start_idx);

    uint32_t gops_written = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start_idx + i) % kMaxGopSlots;
        GopSlot& slot = buffer_[idx];

        if (slot.is_valid && slot.size > 0) {
            // 直接 write() 到文件，编码数据无需额外处理
            ssize_t written = write(fd, slot.data, slot.size);
            if (written != static_cast<ssize_t>(slot.size)) {
                LOG_WARN("PreRecordBuffer: Write GOP %u failed (written=%zd, expected=%u)",
                         i, written, slot.size);
                break;  // SD 卡可能满，停止写入
            }
            gops_written++;
        }

        // 清除已刷新的 slot，防止下次 Flush 时重复写入
        slot.is_valid = false;
        slot.size = 0;
    }

    // 清空计数: 缓冲区已全部刷出
    gop_count_ = 0;
    pthread_mutex_unlock(&lock_);

    LOG_INFO("PreRecordBuffer: Flushed %u GOPs", gops_written);
    return LEAVR_OK;
}

/**
 * @brief 清空缓冲区，丢弃所有缓存的 GOP
 *
 * 通常在停止录像后调用，确保下次录像从干净状态开始。
 * 重置所有 slot 为无效状态，write_index 和 gop_count 归零。
 *
 * 线程安全: pthread_mutex_lock 保护。
 */
void PreRecordBuffer::Clear() {
    pthread_mutex_lock(&lock_);
    for (uint32_t i = 0; i < kMaxGopSlots; i++) {
        buffer_[i].is_valid = false;
        buffer_[i].size = 0;
    }
    write_index_ = 0;
    gop_count_ = 0;
    pthread_mutex_unlock(&lock_);
}

} // namespace leavr