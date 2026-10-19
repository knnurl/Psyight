// The pattern effects (engine/scene.h): built on engine/space.h's mappings and clocks.

#include "engine/scene.h"
#include "engine/space.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

double frac(double x) { return x - std::floor(x); }

Mapping mapping_of(int m) { return static_cast<Mapping>(std::clamp(m, 0, kMappingCount - 1)); }
Clock clock_of(int c) { return static_cast<Clock>(std::clamp(c, 0, kClockCount - 1)); }

class Chase final : public Effect {
  public:
    explicit Chase(const ChaseParams &p) : p_(p) {}
    const char *name() const override { return "Chase"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const std::vector<double> u = map_pixels(c.rig, mapping_of(p_.mapping), c.seed);
        const double cycles = clock_cycles(c.in, clock_of(p_.clock), p_.rate);
        const int count = std::max(1, p_.count);
        const double tail = std::max(1e-3, p_.tail);
        for (size_t i = 0; i < out.size(); ++i) {
            const double x = fold_wings(u[i], p_.wings);
            double best = 0.0;
            int lit_by = 0;
            for (int k = 0; k < count; ++k) {
                const double phase = cycles + static_cast<double>(k) / count;
                double behind = 0.0;
                if (p_.bounce != 0) {
                    // There and back: the head on a triangle, the tail behind its motion.
                    const double f = frac(phase);
                    const double head = f < 0.5 ? 2.0 * f : 2.0 - 2.0 * f;
                    behind = f < 0.5 ? head - x : x - head;
                } else {
                    behind = frac(phase - x);
                }
                if (behind < 0.0 || behind >= tail) {
                    continue;
                }
                const double v = (1.0 - behind / tail) * (1.0 - behind / tail);
                if (v > best) {
                    best = v;
                    lit_by = k;
                }
            }
            out[i] = best > 0.0 ? scaled(c.palette.at(static_cast<double>(lit_by) / count),
                                         p_.level * best)
                                : Rgb{};
        }
    }

  private:
    ChaseParams p_;
};

class Wave final : public Effect {
  public:
    explicit Wave(const WaveParams &p) : p_(p) {}
    const char *name() const override { return "Wave"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const std::vector<double> u = map_pixels(c.rig, mapping_of(p_.mapping), c.seed);
        const double cycles = clock_cycles(c.in, clock_of(p_.clock), p_.rate);
        const double width = std::clamp(p_.width, 0.01, 1.0);
        for (size_t i = 0; i < out.size(); ++i) {
            const double x = fold_wings(u[i], p_.wings);
            const double f = frac(p_.spread * x - cycles);
            double v = 0.0;
            switch (p_.shape) {
            case 0: // Sine.
                v = 0.5 - 0.5 * std::cos(2.0 * kPi * f);
                break;
            case 1: // Triangle.
                v = 1.0 - std::fabs(2.0 * f - 1.0);
                break;
            case 2: // Ramp: bright at the leading edge, fading behind it.
                v = f;
                break;
            case 3: // Square, `width` of each cycle on.
                v = f < width ? 1.0 : 0.0;
                break;
            default: { // Bump: a soft pulse `width` wide.
                const double d = std::min(f, 1.0 - f) / (0.5 * width);
                v = std::exp(-4.0 * d * d);
                break;
            }
            }
            out[i] = scaled(c.palette.at(p_.colour_spread * x), p_.level * v);
        }
    }

  private:
    WaveParams p_;
};

// A beat's length, or 150 BPM's without a tempo.
double beat_of(const EngineInputs &in) { return in.beat_s > 0.0 ? in.beat_s : 0.4; }

class Strobe final : public Effect {
  public:
    explicit Strobe(const StrobeParams &p) : p_(p) {}
    const char *name() const override { return "Strobe"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const EngineInputs &in = c.in;
        const double beat_s = beat_of(in);
        const long long every = std::max(1, p_.every);
        double since = -1.0; // Since the trigger; negative: none.
        switch (p_.trigger) {
        case 0: // The drop's start.
            since = in.section == "drop" ? in.since_section_s : -1.0;
            break;
        case 1: // The build's last beat.
            if (in.section == "build" && in.section_length_s > 0.0) {
                since = in.since_section_s - (in.section_length_s - beat_s);
            }
            break;
        case 2: // Kicks.
            if (std::isfinite(in.since_kick_s) && in.kicks_so_far > 0 &&
                (in.kicks_so_far - 1) % every == 0) {
                since = in.since_kick_s;
            }
            break;
        case 3: // Beats.
            since = ((in.beat_index % every) + every) % every == 0 ? in.beat_phase * beat_s : -1.0;
            break;
        default: // Bars.
            since =
                ((in.bar_index % every) + every) % every == 0 ? in.bar_phase * 4.0 * beat_s : -1.0;
            break;
        }
        Rgb colour{};
        if (since >= 0.0) {
            const double slot_s = 0.25 * beat_s;
            const auto flash = static_cast<int>(std::floor(since / slot_s));
            if (flash < std::max(1, p_.flashes)) {
                const double env = std::exp(-(since - flash * slot_s) / std::max(1e-3, p_.decay_s));
                const Rgb base =
                    p_.colour == 0 ? Rgb{1.0f, 1.0f, 1.0f} : c.palette.at(0.25 * flash);
                colour = scaled(base, p_.level * env);
            }
        }
        std::fill(out.begin(), out.end(), colour);
    }

  private:
    StrobeParams p_;
};

class Stutter final : public Effect {
  public:
    explicit Stutter(const StutterParams &p) : p_(p) {}
    const char *name() const override { return "Stutter gate"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const EngineInputs &in = c.in;
        const double beat_s = beat_of(in);
        const double release_s = std::max(1e-3, p_.release_s);
        double open = 1.0;
        switch (p_.mode) {
        case 0: { // Grid.
            const double per_beat = p_.division == 0 ? 4.0 : p_.division == 1 ? 2.0 : 1.0;
            const double division_s = beat_s / per_beat;
            const double position =
                frac(per_beat * (static_cast<double>(in.beat_index) + in.beat_phase));
            open =
                position < p_.duty ? 1.0 : std::exp(-(position - p_.duty) * division_s / release_s);
            break;
        }
        case 1: // The bassline's gate.
            open = in.bass_active ? 1.0 : std::exp(-in.since_bass_offset_s / release_s);
            break;
        default: // Duck after each kick, reopening over duty of a beat.
            if (std::isfinite(in.since_kick_s)) {
                const double t = std::clamp(in.since_kick_s / (p_.duty * beat_s), 0.0, 1.0);
                open = t * t * (3.0 - 2.0 * t);
            }
            break;
        }
        const auto v = static_cast<float>(1.0 - std::clamp(p_.depth, 0.0, 1.0) * (1.0 - open));
        std::fill(out.begin(), out.end(), Rgb{v, v, v});
    }

  private:
    StutterParams p_;
};

// Octaves of value noise, 0 to 1.
double fbm(double x, double y, double z, int octaves, std::uint64_t seed) {
    double sum = 0.0;
    double weight = 0.0;
    double amplitude = 1.0;
    double scale = 1.0;
    for (int k = 0; k < std::clamp(octaves, 1, 6); ++k) {
        sum += amplitude * value_noise(x * scale, y * scale, z * scale, seed + k);
        weight += amplitude;
        amplitude *= 0.5;
        scale *= 2.0;
    }
    return sum / weight;
}

class Noise final : public Effect {
  public:
    explicit Noise(const NoiseParams &p) : p_(p) {}
    const char *name() const override { return "Noise field"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const double scale = std::max(0.01, p_.scale_m);
        const double z = c.in.t_s * p_.speed_hz;
        const double timbre =
            p_.brightness_colour * std::clamp(c.in.residual_centroid_hz / 6000.0, 0.0, 1.0);
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double n = fbm(q.x / scale, (q.y + 0.5 * q.z) / scale, z, p_.octaves, c.seed);
            const double v = std::clamp(0.5 + (n - 0.5) * p_.contrast, 0.0, 1.0);
            out[i] = scaled(c.palette.at(p_.colour_spread * n + timbre), p_.level * v);
        }
    }

  private:
    NoiseParams p_;
};

class Fire final : public Effect {
  public:
    explicit Fire(const FireParams &p) : p_(p) {}
    const char *name() const override { return "Fire"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const std::vector<double> u = map_pixels(c.rig, mapping_of(p_.mapping), c.seed);
        const double extent = std::max(
            {c.rig.max.x - c.rig.min.x, c.rig.max.y - c.rig.min.y, c.rig.max.z - c.rig.min.z, 0.1});
        const double scale = std::max(0.01, p_.scale_m);
        const double height =
            std::max(1e-3, p_.height * (1.0 + p_.riser_boost * std::clamp(c.in.riser, 0.0, 1.0)));
        const double rise = p_.speed_m_per_s * c.in.t_s;
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double n =
                fbm((q.x + q.z) / scale, (u[i] * extent - rise) / scale, 0.7 * c.in.t_s, 2, c.seed);
            const double flame = 1.0 - p_.flicker + 2.0 * p_.flicker * n;
            // Heat falls off with height (gently, so the flames have body), times the flicker;
            // the base rarely saturates.
            const double below = std::max(0.0, 1.0 - u[i] / height);
            const double heat = std::clamp(0.75 * std::pow(below, 0.7) * flame, 0.0, 1.0);
            if (heat <= 0.0) {
                out[i] = Rgb{};
                continue;
            }
            // Fire: deep red, orange, and yellow only in the hottest cores (white stays for kicks
            // and drops).
            const double warm = std::clamp((heat - 0.35) / 0.65, 0.0, 1.0);
            const Rgb base = p_.colours == 0
                                 ? Rgb{static_cast<float>(std::min(1.0, 1.6 * heat)),
                                       static_cast<float>(0.6 * std::pow(warm, 1.5)), 0.0f}
                                 : c.palette.at(0.5 * heat);
            out[i] = scaled(base, p_.level * std::pow(heat, 1.2));
        }
    }

  private:
    FireParams p_;
};

} // namespace

std::unique_ptr<Effect> make_strobe(const StrobeParams &params) {
    return std::make_unique<Strobe>(params);
}

std::unique_ptr<Effect> make_stutter(const StutterParams &params) {
    return std::make_unique<Stutter>(params);
}

std::unique_ptr<Effect> make_noise(const NoiseParams &params) {
    return std::make_unique<Noise>(params);
}

std::unique_ptr<Effect> make_fire(const FireParams &params) {
    return std::make_unique<Fire>(params);
}

std::unique_ptr<Effect> make_chase(const ChaseParams &params) {
    return std::make_unique<Chase>(params);
}

std::unique_ptr<Effect> make_wave(const WaveParams &params) {
    return std::make_unique<Wave>(params);
}

} // namespace psyight
