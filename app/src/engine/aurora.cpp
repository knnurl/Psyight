#include "engine/aurora.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace psyight {

namespace {

// WLED's Aurora at its default intensity, in strip-relative units: about one wave per 12 pixels
// (2 to 12 a strip), 1/20 to 1/6 of the strip wide, drifting at 1.5 to 4.5 pixels per second.
constexpr int kPixelsPerWave = 12;
constexpr int kMinWaves = 2;
constexpr int kMaxWaves = 12;
constexpr double kMinLifeS = 8.0;
constexpr double kMaxLifeS = 25.0;
constexpr double kMinWidthShare = 1.0 / 20.0;
constexpr double kMaxWidthShare = 1.0 / 6.0;
constexpr double kMinWidthPixels = 2.0;
constexpr double kMinSpeedPxPerS = 1.5;
constexpr double kMaxSpeedPxPerS = 4.5;
constexpr double kMinAlpha = 0.6; // A wave's peak, at the middle of its life.

const Palette kAurora{
    "Aurora", {{0.0f, 1.0f, 0.35f}, {0.0f, 0.8f, 0.7f}, {0.1f, 0.4f, 1.0f}, {0.55f, 0.0f, 1.0f}}};

std::uint64_t mix(std::uint64_t x) { // splitmix64's finaliser.
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

// 0 to 1, from the wave's identity and which of its numbers this is.
double unit(std::uint64_t seed, int strip, int slot, long long generation, int which) {
    std::uint64_t h = mix(seed);
    h = mix(h ^ static_cast<std::uint64_t>(strip));
    h = mix(h ^ static_cast<std::uint64_t>(slot));
    h = mix(h ^ static_cast<std::uint64_t>(generation));
    h = mix(h ^ static_cast<std::uint64_t>(which));
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

double between(double lo, double hi, double u) { return lo + (hi - lo) * u; }

struct Wave {
    double centre_px = 0.0;
    double width_px = 1.0;
    double alpha = 0.0; // Now, with the fade in and out.
    Rgb colour;
};

} // namespace

const Palette &aurora_palette() { return kAurora; }

Frame render_aurora(const Rig &rig, double clock_s, std::uint64_t seed) {
    Frame frame(rig.pixels.size());
    const double t = std::max(0.0, clock_s);
    std::vector<Wave> waves;
    for (int strip = 0; strip < static_cast<int>(rig.strips.size()); ++strip) {
        const int count = std::max(1, rig.strips[static_cast<size_t>(strip)].count);
        const int slots = std::clamp(count / kPixelsPerWave, kMinWaves, kMaxWaves);
        waves.clear();
        for (int slot = 0; slot < slots; ++slot) {
            // Each slot's waves follow one another, the slots out of step.
            const double life_s = between(kMinLifeS, kMaxLifeS, unit(seed, strip, slot, -1, 0));
            const double shifted = t + life_s * unit(seed, strip, slot, -1, 1);
            const auto generation = static_cast<long long>(std::floor(shifted / life_s));
            const double age = shifted / life_s - static_cast<double>(generation); // 0 to 1.
            const auto u = [&](int which) { return unit(seed, strip, slot, generation, which); };
            Wave w;
            const double speed = between(kMinSpeedPxPerS, kMaxSpeedPxPerS, u(0));
            const double direction = u(1) < 0.5 ? -1.0 : 1.0;
            w.centre_px = u(2) * count + direction * speed * age * life_s;
            w.width_px =
                std::max(kMinWidthPixels, count * between(kMinWidthShare, kMaxWidthShare, u(3)));
            // Up over the first half of its life, down over the second.
            w.alpha = between(kMinAlpha, 1.0, u(4)) * (1.0 - std::abs(2.0 * age - 1.0));
            w.colour = kAurora.at(u(5));
            waves.push_back(w);
        }
        for (size_t i = 0; i < rig.pixels.size(); ++i) {
            const Pixel &p = rig.pixels[i];
            if (p.strip != strip) {
                continue;
            }
            Rgb sum;
            for (const Wave &w : waves) {
                // Around the strip as a loop, so short strips keep their waves (seamless on rings).
                const double along = std::fmod(std::abs(static_cast<double>(p.index) - w.centre_px),
                                               static_cast<double>(count));
                const double off = std::min(along, count - along);
                if (off >= w.width_px) {
                    continue;
                }
                const Rgb c = scaled(w.colour, w.alpha * (1.0 - off / w.width_px));
                sum = {sum.r + c.r, sum.g + c.g, sum.b + c.b};
            }
            // Overlaps glow brighter but keep their hue: scaled down, not clipped towards white.
            const float peak = std::max({sum.r, sum.g, sum.b});
            frame[i] = peak > 1.0f ? scaled(sum, 1.0 / peak) : sum;
        }
    }
    return frame;
}

} // namespace psyight
