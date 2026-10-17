// A single-writer snapshot of a small trivially copyable value (SPEC 4.2: snapshots the reader
// never blocks on). The writer never waits; a reader copies and retries while a write was in
// progress. Store and load never allocate. Meant for values written at most a few hundred times a
// second and read by a few threads (the live analysis state).
//
// The copy itself is a plain memcpy guarded by the sequence counter and fences, the usual seqlock
// pattern; the counter makes a torn copy detectable, and a torn copy is never returned.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace psyight {

template <typename T> class Seqlock {
    static_assert(std::is_trivially_copyable_v<T>);

  public:
    void store(const T &value) {
        const std::uint32_t sequence = sequence_.load(std::memory_order_relaxed);
        sequence_.store(sequence + 1, std::memory_order_relaxed); // Odd: writing.
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&value_, &value, sizeof(T));
        sequence_.store(sequence + 2, std::memory_order_release);
    }

    T load() const {
        T out;
        for (;;) {
            const std::uint32_t before = sequence_.load(std::memory_order_acquire);
            if (before & 1u) {
                continue;
            }
            std::memcpy(&out, &value_, sizeof(T));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence_.load(std::memory_order_relaxed) == before) {
                return out;
            }
        }
    }

    // How many stores so far (for change detection).
    std::uint32_t version() const { return sequence_.load(std::memory_order_acquire) / 2; }

  private:
    std::atomic<std::uint32_t> sequence_{0};
    T value_{};
};

} // namespace psyight
