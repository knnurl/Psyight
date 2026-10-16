// Sleeps until a steady-clock time with about 1 ms precision or better (the default Windows
// timer tick is 15.6 ms, too coarse for 60 Hz output). Windows: a high-resolution waitable timer
// (Windows 10 1803 or later), falling back to std::this_thread::sleep_until.
#pragma once

#include <chrono>

namespace psyight {

void sleep_until_precise(std::chrono::steady_clock::time_point until);

} // namespace psyight
