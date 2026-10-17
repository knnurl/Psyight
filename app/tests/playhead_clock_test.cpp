// The smoothed playhead (core/playhead_clock.h) against irregular transport callbacks.

#include "core/playhead_clock.h"

#include <doctest/doctest.h>

#include <cmath>
#include <random>
#include <vector>

using namespace psyight;

TEST_CASE("playhead clock: smooth steps from bursty 10 ms callbacks, snapping on seeks") {
    constexpr double kRate = 48000.0;
    std::mt19937 rng(3);
    // Callbacks every 10 ms of audio, delivered late by up to 9 ms and sometimes two at once;
    // each reports the position at the end of the block it rendered.
    std::vector<double> callback_s;
    for (double t = 0.0; t < 6.0; t += 0.01) {
        const bool burst = rng() % 5 == 0;
        callback_s.push_back(t + (burst ? 0.012 : 0.0) + 0.009 * (rng() % 100) / 100.0);
    }
    PlayheadClock clock;
    std::vector<double> steps_ms;
    double last = 0.0;
    double worst_error_ms = 0.0;
    size_t c = 0;
    SampleIndex measured = 0;
    for (int f = 0; f < 340; ++f) {
        const double now = f / 60.0;
        while (c < callback_s.size() && callback_s[c] <= now) {
            measured = static_cast<SampleIndex>((static_cast<double>(c) + 1.0) * 0.01 * kRate);
            ++c;
        }
        const double p = static_cast<double>(clock.update(now, measured, kRate));
        if (f > 60) { // After a second to lock.
            steps_ms.push_back((p - last) / 48.0);
            worst_error_ms = std::max(worst_error_ms, std::abs(p / kRate - now) * 1e3);
        }
        last = p;
    }
    double mean = 0.0;
    for (const double s : steps_ms) {
        mean += s / static_cast<double>(steps_ms.size());
    }
    double sd = 0.0;
    for (const double s : steps_ms) {
        sd += (s - mean) * (s - mean) / static_cast<double>(steps_ms.size());
    }
    sd = std::sqrt(sd);
    MESSAGE("steps: mean " << mean << " ms, sd " << sd << " ms; worst error " << worst_error_ms
                           << " ms");
    CHECK(mean == doctest::Approx(16.667).epsilon(0.02));
    CHECK(sd < 1.5);              // The raw measurements step 10 to 30 ms.
    CHECK(worst_error_ms < 25.0); // Stays within a couple of periods of the true position.

    // A seek snaps; a pause returns the measurement as is.
    CHECK(clock.update(6.0, 96000, kRate) == 96000);
    CHECK(clock.update(6.5, 12345, 0.0) == 12345);
}
