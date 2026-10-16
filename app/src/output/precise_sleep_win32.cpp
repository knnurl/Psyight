#include "output/precise_sleep.h"

#include <windows.h>

#include <thread>

namespace psyight {

namespace {

// One timer per thread, closed when the thread exits.
struct WaitableTimer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                           TIMER_ALL_ACCESS);
    WaitableTimer() = default;
    WaitableTimer(const WaitableTimer &) = delete;
    WaitableTimer &operator=(const WaitableTimer &) = delete;
    ~WaitableTimer() {
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }
};

} // namespace

void sleep_until_precise(std::chrono::steady_clock::time_point until) {
    const auto remaining = until - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
        return;
    }
    thread_local WaitableTimer timer;
    LARGE_INTEGER due;
    due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100;
    if (timer.handle != nullptr &&
        SetWaitableTimerEx(timer.handle, &due, 0, nullptr, nullptr, nullptr, 0)) {
        WaitForSingleObject(timer.handle, INFINITE);
        return;
    }
    std::this_thread::sleep_until(until);
}

} // namespace psyight
