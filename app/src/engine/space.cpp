#include "engine/space.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kStepGlideS = 0.04;

double unit(double v, double lo, double hi) {
    return hi - lo > 1e-9 ? std::clamp((v - lo) / (hi - lo), 0.0, 1.0) : 0.5;
}

// Steps so far, the last gliding in.
double steps(long long so_far, double since_s) {
    if (so_far <= 0) {
        return 0.0;
    }
    const double glide = std::isfinite(since_s) ? std::clamp(since_s / kStepGlideS, 0.0, 1.0) : 1.0;
    return static_cast<double>(so_far - 1) + glide;
}

} // namespace

double hash_unit(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    std::uint64_t z = a * 0x9E3779B97F4A7C15ull ^
                      (b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull ^
                      (c + 0x85EBCA77C2B2AE63ull) * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) * 0x1.0p-53;
}

std::vector<double> map_pixels(const Rig &rig, Mapping mapping, std::uint64_t seed) {
    std::vector<double> u(rig.pixels.size(), 0.0);
    const Vec3 centre{0.5 * (rig.min.x + rig.max.x), 0.5 * (rig.min.y + rig.max.y),
                      0.5 * (rig.min.z + rig.max.z)};
    const double reach = std::hypot(0.5 * (rig.max.x - rig.min.x), 0.5 * (rig.max.y - rig.min.y),
                                    0.5 * (rig.max.z - rig.min.z));
    const auto strips = static_cast<double>(std::max<size_t>(1, rig.strips.size()));
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        const Pixel &p = rig.pixels[i];
        switch (mapping) {
        case Mapping::along_strip: {
            const int count =
                static_cast<size_t>(p.strip) < rig.strips.size() ? rig.strips[p.strip].count : 1;
            u[i] = (p.index + 0.5) / std::max(1, count);
            break;
        }
        case Mapping::across_x:
            u[i] = unit(p.position.x, rig.min.x, rig.max.x);
            break;
        case Mapping::across_y:
            u[i] = unit(p.position.y, rig.min.y, rig.max.y);
            break;
        case Mapping::radial:
            u[i] = reach > 1e-9
                       ? std::clamp(std::hypot(p.position.x - centre.x, p.position.y - centre.y,
                                               p.position.z - centre.z) /
                                        reach,
                                    0.0, 1.0)
                       : 0.0;
            break;
        case Mapping::by_strip:
            u[i] = (p.strip + 0.5) / strips;
            break;
        case Mapping::random:
            u[i] = hash_unit(seed, i, 31);
            break;
        }
    }
    return u;
}

double fold_wings(double u, int wings) {
    if (wings <= 1) {
        return u;
    }
    const double s = std::clamp(u, 0.0, 1.0) * wings;
    const double k = std::min(std::floor(s), static_cast<double>(wings - 1));
    const double f = s - k;
    return static_cast<long long>(k) % 2 == 0 ? f : 1.0 - f;
}

double clock_cycles(const EngineInputs &in, Clock clock, double rate) {
    const double beats = static_cast<double>(in.beat_index) + in.beat_phase;
    switch (clock) {
    case Clock::free:
        return rate * in.t_s;
    case Clock::sixteenth:
        return rate * 4.0 * beats;
    case Clock::beat:
        return rate * beats;
    case Clock::bar:
        return rate * (static_cast<double>(in.bar_index) + in.bar_phase);
    case Clock::phrase:
        return rate * (std::max(0, in.phrase_index) + in.phrase_phase);
    case Clock::bass_note:
        return rate * steps(in.bass_notes_so_far, in.since_bass_onset_s);
    case Clock::kick:
        return rate * steps(in.kicks_so_far, in.since_kick_s);
    }
    return 0.0;
}

} // namespace psyight
