/**
 * @file ring_buffer.cpp
 * @brief 预录 GOP 环形缓冲区实现
 */

#include "utils/ring_buffer.h"
#include "utils/logger.h"
#include <cstring>
#include <unistd.h>

namespace leavr {

int PreRecordBuffer::Init(int pre_record_sec) {
    if (pre_record_sec <= 0) {
        max_slots_ = 0;
        gop_count_ = 0;
        return LEAVR_OK;
    }

    if (pre_record_sec > static_cast<int>(kMaxGopSlots)) {
        pre_record_sec = kMaxGopSlots;
    }

    max_slots_ = static_cast<uint32_t>(pre_record_sec);
    gop_count_ = 0;
    write_index_ = 0;

    // 初始化所有 GOP slot
    for (uint32_t i = 0; i < kMaxGopSlots; i++) {
        buffer_[i].is_valid = false;
        buffer_[i].size = 0;
    }

    LOG_INFO("PreRecordBuffer: Init %d sec (%u slots, %zu MB)",
             pre_record_sec, max_slots_,
             (kTotalSize / (1024 * 1024)));
    return LEAVR_OK;
}

int PreRecordBuffer::WriteGop(const uint8_t* data, uint32_t size, uint64_t pts_ms) {
    if (max_slots_ == 0) return LEAVR_OK;
    if (!data || size == 0 || size > kMaxGopSize) {
        return LEAVR_ERR_PARAM;
    }

    pthread_mutex_lock(&lock_);

    uint32_t idx = write_index_.load(std::memory_order_relaxed);
    GopSlot& slot = buffer_[idx];

    memcpy(slot.data, data, size);
    slot.size = size;
    slot.pts_ms = pts_ms;
    slot.is_valid = true;

    // 环形递增
    idx = (idx + 1) % kMaxGopSlots;
    write_index_.store(idx, std::memory_order_release);

    // 更新 GOP 计数 (不超过 max_slots_)
    uint32_t count = gop_count_.load(std::memory_order_relaxed);
    if (count < max_slots_) {
        gop_count_.store(count + 1, std::memory_order_release);
    }

    pthread_mutex_unlock(&lock_);
    return LEAVR_OK;
}

int PreRecordBuffer::FlushToFile(int fd) {
    if (fd < 0) return LEAVR_ERR_PARAM;
    if (max_slots_ == 0) return LEAVR_OK;

    pthread_mutex_lock(&lock_);

    uint32_t count = gop_count_.load(std::memory_order_acquire);
    if (count == 0) {
        pthread_mutex_unlock(&lock_);
        return LEAVR_OK;
    }

    // 计算起始索引: write_index - count (环形回绕)
    uint32_t wr_idx = write_index_.load(std::memory_order_acquire);
    uint32_t start_idx = (wr_idx + kMaxGopSlots - count) % kMaxGopSlots;

    LOG_INFO("PreRecordBuffer: Flushing %u GOPs to fd=%d, start_idx=%u",
             count, fd, start_idx);

    uint32_t gops_written = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start_idx + i) % kMaxGopSlots;
        GopSlot& slot = buffer_[idx];

        if (slot.is_valid && slot.size > 0) {
            ssize_t written = write(fd, slot.data, slot.size);
            if (written != static_cast<ssize_t>(slot.size)) {
                LOG_WARN("PreRecordBuffer: Write GOP %u failed", i);
                break;
            }
            gops_written++;
        }

        // 清除已刷新的 slot
        slot.is_valid = false;
        slot.size = 0;
    }

    gop_count_ = 0;
    pthread_mutex_unlock(&lock_);

    LOG_INFO("PreRecordBuffer: Flushed %u GOPs", gops_written);
    return LEAVR_OK;
}

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