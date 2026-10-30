#include "output/test_patterns.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr float kIdentifyR = 0.0f; // Cyan: not white (kept for kicks), clear on every strip type.
constexpr float kIdentifyG = 0.8f;
constexpr float kIdentifyB = 1.0f;
constexpr double kGoldenAngleDeg = 137.508; // Strip hues: neighbours never look alike.
constexpr double kPi = 3.14159265358979323846;
// Eye candy: the wheel turns once in 15 s, a ring leaves the centre every 2.5 s, the whole
// breathes over 8 s.
constexpr double kWheelDegPerS = 24.0;
constexpr double kTwistTurns = 0.6; // Hue turns from the centre to the edge.
constexpr double kRingS = 2.5;
constexpr double kRingsAcross = 1.5;
constexpr double kBreathS = 8.0;

} // namespace

int colour_order_step(double clock_s) {
    const auto step = static_cast<long long>(std::floor(std::max(0.0, clock_s) / kColourStepS));
    return static_cast<int>(step % 3);
}

Frame render_test_pattern(const TestPattern &pattern, const Rig &rig, const Patch &patch,
                          double clock_s) {
    Frame frame(rig.pixels.size());
    const auto level = static_cast<float>(std::clamp(pattern.level, 0.0, 1.0));
    switch (pattern.kind) {
    case TestPatternKind::off:
        break;
    case TestPatternKind::chase: {
        const int length = std::max(1, pattern.chase_length);
        for (size_t i = 0; i < rig.pixels.size(); ++i) {
            const Pixel &p = rig.pixels[i];
            if (pattern.strip >= 0 && p.strip != pattern.strip) {
                continue;
            }
            const auto count = p.strip < static_cast<int>(rig.strips.size())
                                   ? std::max(1, rig.strips[static_cast<size_t>(p.strip)].count)
                                   : 1;
            // The head runs from the first pixel past the last, then starts again.
            const double head = std::fmod(std::max(0.0, clock_s) * pattern.chase_pixels_per_s,
                                          static_cast<double>(count + length));
            const double behind = head - p.index;
            if (behind < 0.0 || behind >= length) {
                continue;
            }
            const double fade = 1.0 - behind / length;
            frame[i] =
                scaled(hsv(std::fmod(kGoldenAngleDeg * p.strip, 360.0), 1.0, 1.0), level * fade);
        }
        break;
    }
    case TestPatternKind::colour_order: {
        const int step = colour_order_step(clock_s);
        const Rgb c{step == 0 ? level : 0.0f, step == 1 ? level : 0.0f, step == 2 ? level : 0.0f};
        std::fill(frame.begin(), frame.end(), c);
        break;
    }
    case TestPatternKind::identify_universe:
        for (size_t i = 0; i < rig.pixels.size() && i < patch.pixels.size(); ++i) {
            const auto slot = static_cast<size_t>(patch.pixels[i].universe_slot);
            if (slot < patch.universes.size() && patch.universes[slot] == pattern.universe) {
                frame[i] = {kIdentifyR * level, kIdentifyG * level, kIdentifyB * level};
            }
        }
        break;
    case TestPatternKind::all_on:
        std::fill(frame.begin(), frame.end(), Rgb{1.0f, 1.0f, 1.0f});
        break;
    case TestPatternKind::eye_candy: {
        const double cx = 0.5 * (rig.min.x + rig.max.x);
        const double cy = 0.5 * (rig.min.y + rig.max.y);
        const double radius =
            std::max({0.5 * (rig.max.x - rig.min.x), 0.5 * (rig.max.y - rig.min.y), 1e-6});
        const double t = std::max(0.0, clock_s);
        const double breath = 0.85 + 0.15 * std::sin(2.0 * kPi * t / kBreathS);
        for (size_t i = 0; i < rig.pixels.size(); ++i) {
            const Vec3 &q = rig.pixels[i].position;
            const double r = std::hypot(q.x - cx, q.y - cy) / radius; // 0 centre, 1 edge.
            const double turn = std::atan2(q.y - cy, q.x - cx) / (2.0 * kPi);
            const double hue = 360.0 * (turn + kTwistTurns * r) + kWheelDegPerS * t;
            const double ring = 0.5 + 0.5 * std::cos(2.0 * kPi * (kRingsAcross * r - t / kRingS));
            frame[i] = hsv(hue, 1.0, level * breath * (0.25 + 0.75 * ring * ring));
        }
        break;
    }
    }
    return frame;
}

const char *test_pattern_name(TestPatternKind kind) {
    switch (kind) {
    case TestPatternKind::off:
        return "Off";
    case TestPatternKind::chase:
        return "Chase";
    case TestPatternKind::colour_order:
        return "Colour order";
    case TestPatternKind::identify_universe:
        return "Identify universe";
    case TestPatternKind::all_on:
        return "All on";
    case TestPatternKind::eye_candy:
        return "Eye candy";
    }
    return "?";
}

} // namespace psyight
