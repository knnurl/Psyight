// Limiters: flash rate, brightness cap, power budget and gamma.

#include "engine/limiters.h"

#include <doctest/doctest.h>

#include <cmath>
#include <deque>
#include <vector>

using namespace psyight;

namespace {

constexpr double kFps = 60.0;
constexpr size_t kPixels = 444;

// Counts flashes (as the limiter defines them) in a frame sequence, and the most in any 1 s.
int max_flashes_per_second(const std::vector<Frame> &frames, const LimiterParams &p) {
    std::vector<double> previous(kPixels, 0.0);
    std::deque<double> recent;
    int most = 0;
    for (size_t f = 0; f < frames.size(); ++f) {
        const double t = static_cast<double>(f) / kFps;
        size_t rising = 0;
        for (size_t i = 0; i < kPixels; ++i) {
            const double l = luminance(frames[f][i]);
            rising += l - previous[i] >= p.flash_delta;
            previous[i] = l;
        }
        while (!recent.empty() && t - recent.front() >= 1.0) {
            recent.pop_front();
        }
        if (static_cast<double>(rising) >= p.flash_area * kPixels) {
            recent.push_back(t);
        }
        most = std::max(most, static_cast<int>(recent.size()));
    }
    return most;
}

std::vector<Frame> strobe(double rate_hz, double seconds) {
    std::vector<Frame> frames;
    for (int f = 0; f < static_cast<int>(seconds * kFps); ++f) {
        const bool on = std::fmod(f / kFps * rate_hz, 1.0) < 0.5;
        frames.emplace_back(kPixels, on ? Rgb{1.0f, 1.0f, 1.0f} : Rgb{});
    }
    return frames;
}

} // namespace

TEST_CASE("limiters: a 10 Hz full-field strobe is held to 3 flashes in any second") {
    LimiterParams p;
    p.budget_a = 0.0; // Power budget off: test the flash limiter alone.
    const std::vector<Frame> input = strobe(10.0, 3.0);
    CHECK(max_flashes_per_second(input, p) >= 10); // 10 Hz, frame-rounded.
    Limiters limiters(p);
    std::vector<Frame> output = input;
    int limited = 0;
    for (size_t f = 0; f < output.size(); ++f) {
        limited += limiters.process(output[f], static_cast<double>(f) / kFps).flash_limited;
    }
    CHECK(max_flashes_per_second(output, p) <= 3);
    CHECK(max_flashes_per_second(output, p) >= 3); // The allowed ones still pass.
    CHECK(limited > 0);
}

TEST_CASE("limiters: slow fades pass unchanged; a disabled limiter passes the strobe") {
    LimiterParams p;
    p.budget_a = 0.0;
    Limiters limiters(p);
    for (int f = 0; f < 120; ++f) {
        const float v = static_cast<float>(f) / 120.0f; // 0 to 1 over 2 s.
        Frame frame(kPixels, Rgb{v, v, v});
        const Frame original = frame;
        CHECK_FALSE(limiters.process(frame, f / kFps).flash_limited);
        CHECK(frame == original);
    }
    p.flash_limiter = false;
    Limiters off(p);
    std::vector<Frame> frames = strobe(10.0, 2.0);
    const std::vector<Frame> original = frames;
    for (size_t f = 0; f < frames.size(); ++f) {
        off.process(frames[f], static_cast<double>(f) / kFps);
    }
    CHECK(frames == original);
}

TEST_CASE("limiters: the power budget scales a frame down to the supply's limit") {
    LimiterParams p;
    p.flash_limiter = false;
    p.budget_a = 10.0;
    Frame white(kPixels, Rgb{1.0f, 1.0f, 1.0f});
    CHECK(estimated_current_a(white, 20.0) == doctest::Approx(444 * 3 * 0.02)); // 26.6 A.
    Limiters limiters(p);
    const LimiterReport report = limiters.process(white, 0.0);
    CHECK(report.current_a == doctest::Approx(10.0).epsilon(0.001));
    CHECK(report.power_scale == doctest::Approx(10.0 / 26.64).epsilon(0.001));
    Frame dim(kPixels, Rgb{0.1f, 0.0f, 0.0f});
    CHECK(limiters.process(dim, 1.0).power_scale == 1.0);
}

TEST_CASE("limiters: brightness cap and gamma to bytes") {
    LimiterParams p;
    p.brightness_cap = 0.5;
    p.flash_limiter = false;
    p.budget_a = 0.0;
    Limiters limiters(p);
    Frame frame(2, Rgb{0.8f, 0.4f, 1.0f});
    limiters.process(frame, 0.0);
    CHECK(frame[0].r == doctest::Approx(0.4f));
    CHECK(frame[0].b == doctest::Approx(0.5f));
    const std::vector<std::uint8_t> bytes =
        to_bytes({Rgb{0.0f, 1.0f, 0.5f}, Rgb{2.0f, -1.0f, 0.25f}}, 2.2);
    REQUIRE(bytes.size() == 6);
    CHECK(bytes[0] == 0);
    CHECK(bytes[1] == 255);
    CHECK(bytes[2] == 55);  // 255 * 0.5^2.2 = 55.49.
    CHECK(bytes[3] == 255); // Clamped.
    CHECK(bytes[4] == 0);
    CHECK(bytes[5] == 12); // 255 * 0.25^2.2 = 12.0.
}
