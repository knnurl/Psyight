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

class Bursts final : public Effect {
  public:
    explicit Bursts(const BurstsParams &p) : p_(p) {}
    const char *name() const override { return "Bursts"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const EngineInputs &in = c.in;
        const double beat_s = beat_of(in);
        // The latest trigger: its index, the time since it, and the spacing of earlier ones
        // (0 when unknown: only the latest is drawn).
        long long index = -1;
        double since = 0.0;
        double spacing = 0.0;
        switch (p_.trigger) {
        case 0: // Kick: earlier kicks taken a beat apart.
            if (in.kicks_so_far > 0 && std::isfinite(in.since_kick_s)) {
                index = in.kicks_so_far - 1;
                since = in.since_kick_s;
                spacing = beat_s;
            }
            break;
        case 1: // Hat.
            if (in.hats_so_far > 0 && std::isfinite(in.since_hat_s)) {
                index = in.hats_so_far - 1;
                since = in.since_hat_s;
            }
            break;
        case 2: // Beat.
            index = in.beat_index;
            since = in.beat_phase * beat_s;
            spacing = beat_s;
            break;
        case 3: // Bar.
            index = in.bar_index;
            since = in.bar_phase * 4.0 * beat_s;
            spacing = 4.0 * beat_s;
            break;
        default: // Bass note.
            if (in.bass_notes_so_far > 0 && std::isfinite(in.since_bass_onset_s)) {
                index = in.bass_notes_so_far - 1;
                since = in.since_bass_onset_s;
            }
            break;
        }
        std::fill(out.begin(), out.end(), Rgb{});
        if (index < 0) {
            return;
        }
        const long long every = std::max(1, p_.every);
        const int keep = spacing > 0.0 ? std::clamp(p_.keep, 1, 6) : 1;
        // Walk back over triggers until `keep` bursts (every nth trigger) are drawn.
        int drawn = 0;
        for (long long k = 0; drawn < keep && k < keep * every; ++k) {
            const long long event = index - k;
            if (event < 0) {
                break;
            }
            if (((event % every) + every) % every != 0) {
                continue;
            }
            ++drawn;
            const double age = since + static_cast<double>(k) * spacing;
            const double fade = std::exp(-age / std::max(1e-3, p_.decay_s));
            if (fade < 0.01) {
                continue;
            }
            draw_burst(c, out, static_cast<std::uint64_t>(event), age, fade);
        }
    }

  private:
    void draw_burst(const EffectContext &c, std::span<Rgb> out, std::uint64_t event, double age,
                    double fade) const {
        const Rig &rig = c.rig;
        // A seeded pixel as the origin (on a strip, so the burst is seen from its start), and a
        // colour.
        if (rig.pixels.empty()) {
            return;
        }
        const auto at = std::min(rig.pixels.size() - 1,
                                 static_cast<size_t>(hash_unit(c.seed, event, 41) *
                                                     static_cast<double>(rig.pixels.size())));
        const Vec3 origin = rig.pixels[at].position;
        const Rgb colour = c.palette.at(hash_unit(c.seed, event, 44));
        const double radius = age * p_.speed_m_per_s;
        const double width = std::max(0.01, p_.width_m);
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = rig.pixels[i].position;
            const double dx = q.x - origin.x;
            const double dy = q.y - origin.y;
            const double d = std::hypot(dx, dy, q.z - origin.z);
            const double x = (d - radius) / width;
            if (std::fabs(x) > 3.0) {
                continue;
            }
            double v = fade * std::exp(-x * x);
            if (p_.sparks > 0.0) {
                // Sparks: 24 angular sectors per burst, a seeded share of them dark.
                const double angle = std::atan2(dy, dx) / (2.0 * kPi) + 0.5;
                const auto sector = static_cast<std::uint64_t>(angle * 24.0);
                if (hash_unit(c.seed, event * 31 + sector, 45) < p_.sparks) {
                    continue;
                }
            }
            v *= p_.level;
            out[i] = Rgb{std::max(out[i].r, static_cast<float>(colour.r * v)),
                         std::max(out[i].g, static_cast<float>(colour.g * v)),
                         std::max(out[i].b, static_cast<float>(colour.b * v))};
        }
    }

    BurstsParams p_;
};

class Spiral final : public Effect {
  public:
    explicit Spiral(const SpiralParams &p) : p_(p) {}
    const char *name() const override { return "Spiral"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const std::vector<double> r = map_pixels(c.rig, Mapping::radial, c.seed);
        const Vec3 centre{0.5 * (c.rig.min.x + c.rig.max.x), 0.5 * (c.rig.min.y + c.rig.max.y),
                          0.0};
        const double cycles = clock_cycles(c.in, clock_of(p_.clock), p_.rate);
        const int arms = std::max(1, p_.arms);
        const double width = std::clamp(p_.width, 0.02, 1.0);
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double turn = std::atan2(q.y - centre.y, q.x - centre.x) / (2.0 * kPi);
            const double f = frac(arms * (turn + p_.twist * r[i] - cycles));
            const double d = std::min(f, 1.0 - f) / (0.5 * width);
            const double v = d < 1.0 ? 0.5 + 0.5 * std::cos(kPi * d) : 0.0;
            out[i] = scaled(c.palette.at(p_.colour_spread * r[i] + turn), p_.level * v);
        }
    }

  private:
    SpiralParams p_;
};

class BassBars final : public Effect {
  public:
    explicit BassBars(const BassBarsParams &p) : p_(p) {}
    const char *name() const override { return "Bass bars"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const EngineInputs &in = c.in;
        // As the conveyor: velocity times the note's decay, then a release after it ends.
        double level = 0.0;
        if (std::isfinite(in.since_bass_onset_s) && in.bass_notes_so_far > 0) {
            const double tau_s = in.bass_decay_tau_ms > 0.0 ? in.bass_decay_tau_ms * 1e-3 : 0.2;
            level = std::max(0.2, in.bass_velocity) * std::exp(-in.since_bass_onset_s / tau_s);
            if (!in.bass_active) {
                level *= std::exp(-in.since_bass_offset_s / std::max(1e-3, p_.release_s));
            }
        }
        if (level < 1e-3) {
            std::fill(out.begin(), out.end(), Rgb{});
            return;
        }
        const double pitch = in.bass_midi + in.bass_cents / 100.0;
        const double span = std::max(1, p_.high_midi - p_.low_midi);
        const double at = std::clamp((pitch - p_.low_midi) / span, 0.0, 1.0);
        const Rgb colour = p_.colours == 0 ? hsv(270.0 + 28.0 * pitch, 1.0, 1.0) : c.palette.at(at);
        const std::vector<double> u = map_pixels(c.rig, mapping_of(p_.mapping), c.seed);
        const double width = std::max(0.005, p_.width);
        for (size_t i = 0; i < out.size(); ++i) {
            double v = 0.0;
            if (p_.mode == 0) {
                const double d = std::fabs(u[i] - at) / width;
                v = d < 1.0 ? 1.0 - d * d : 0.0;
            } else {
                v = u[i] <= at ? 1.0 : std::exp(-(u[i] - at) / width * 4.0);
            }
            out[i] = scaled(colour, p_.level * level * v);
        }
    }

  private:
    BassBarsParams p_;
};

} // namespace

std::unique_ptr<Effect> make_bursts(const BurstsParams &params) {
    return std::make_unique<Bursts>(params);
}

std::unique_ptr<Effect> make_spiral(const SpiralParams &params) {
    return std::make_unique<Spiral>(params);
}

std::unique_ptr<Effect> make_bass_bars(const BassBarsParams &params) {
    return std::make_unique<BassBars>(params);
}

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
