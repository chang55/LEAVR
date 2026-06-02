/**
 * @file ring_buffer.h
 * @brief 线程安全的环形缓冲区 - 用于预录和 EIS 陀螺仪数据
 */

#ifndef LEAVR_UTILS_RING_BUFFER_H
#define LEAVR_UTILS_RING_BUFFER_H

#include "leavr_types.h"
#include <pthread.h>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <new>

namespace leavr {

/**
 * @brief 通用的 SPSC (单生产者单消费者) 环形缓冲区
 */
template<typename T, size_t Capacity>
class RingBuffer {
public:
    RingBuffer() {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
        buffer_ = new (std::nothrow) T[Capacity];
    }

    ~RingBuffer() {
        delete[] buffer_;
    }

    bool Push(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & (Capacity - 1);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // 满
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool Pop(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // 空
        }
        item = buffer_[tail];
        tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    bool Empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    bool Full() const {
        size_t next = (head_.load(std::memory_order_acquire) + 1) & (Capacity - 1);
        return next == tail_.load(std::memory_order_acquire);
    }

    size_t Size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        if (head >= tail) return head - tail;
        return Capacity - (tail - head);
    }

    void Clear() {
        head_.store(0, std::memory_order_release);
        tail_.store(0, std::memory_order_release);
    }

private:
    T* buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

/**
 * @brief 预录 GOP 环形缓冲区 - 存储编码后的 GOP 数据
 */
class PreRecordBuffer {
public:
    static constexpr size_t kMaxGopSlots = 30;   // 最多 30 个 GOP (30s)
    static constexpr size_t kMaxGopSize = 2 * 1024 * 1024;  // 每个 GOP 最大 2MB
    static constexpr size_t kTotalSize = kMaxGopSlots * kMaxGopSize;

    struct GopSlot {
        uint8_t data[kMaxGopSize];
        uint32_t size;
        uint64_t pts_ms;
        bool is_valid;
    };

    PreRecordBuffer() : buffer_(new GopSlot[kMaxGopSlots]) {}
    ~PreRecordBuffer() { delete[] buffer_; }

    int Init(int pre_record_sec);
    int WriteGop(const uint8_t* data, uint32_t size, uint64_t pts_ms);
    int FlushToFile(int fd);
    int GetGopCount() const { return gop_count_; }
    void Clear();

private:
    GopSlot* buffer_;
    std::atomic<uint32_t> write_index_{0};
    std::atomic<uint32_t> gop_count_{0};
    uint32_t max_slots_ = 10;
    pthread_mutex_t lock_ = PTHREAD_MUTEX_INITIALIZER;
};

} // namespace leavr

#endif // LEAVR_UTILS_RING_BUFFER_H