// Lock-free single-producer single-consumer ring buffer (SPEC section 4.2).
//
// Real-time safe after construction: push and pop never allocate, lock or block. Exactly one
// thread may push and exactly one other thread may pop. Capacity is rounded up to a power of two;
// one slot is kept free, so at most capacity() items are queued.
#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <vector>

namespace psyight {

template <typename T> class SpscQueue {
  public:
    explicit SpscQueue(size_t min_capacity) : buffer_(round_up_pow2(min_capacity + 1)) {
        mask_ = buffer_.size() - 1;
    }

    SpscQueue(const SpscQueue &) = delete;
    SpscQueue &operator=(const SpscQueue &) = delete;

    size_t capacity() const { return buffer_.size() - 1; }

    // Producer thread. Returns false when full.
    bool try_push(const T &item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer thread. Returns false when empty.
    bool try_pop(T &item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Approximate when called while the other thread is active.
    size_t size() const {
        return (head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire)) &
               mask_;
    }

  private:
    static size_t round_up_pow2(size_t n) {
        size_t p = 2;
        while (p < n) {
            p <<= 1;
        }
        return p;
    }

    // Padding keeps head and tail on different cache lines so producer and consumer do not
    // false-share. (alignas would trip MSVC's C4324 padding warning under /W4 /WX.)
    static constexpr size_t kCacheLineBytes = 64;
    std::vector<T> buffer_;
    size_t mask_ = 0;
    char pad_before_head_[kCacheLineBytes] = {};
    std::atomic<size_t> head_{0}; // Written by the producer.
    char pad_between_[kCacheLineBytes] = {};
    std::atomic<size_t> tail_{0}; // Written by the consumer.
};

} // namespace psyight
