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

} // namespace

std::unique_ptr<Effect> make_chase(const ChaseParams &params) {
    return std::make_unique<Chase>(params);
}

std::unique_ptr<Effect> make_wave(const WaveParams &params) {
    return std::make_unique<Wave>(params);
}

} // namespace psyight
