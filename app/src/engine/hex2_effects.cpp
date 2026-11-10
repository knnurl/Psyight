// The hexagon-visuals effects (engine/scene.h; docs/PLAN_HEX_VISUALS.md section 6 on): built on
// engine/space.h's hex topology (rings innermost first, spokes, each pixel's hex radius and turn)
// and engine/shape.h's helpers. Pure functions of the rig, the inputs, the palette and the seed.
// On a rig with no hexagon rings, the ring effects render black.

#include "engine/scene.h"
#include "engine/shape.h"
#include "engine/space.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

double frac(double x) { return x - std::floor(x); }

void max_into(Rgb &out, const Rgb &c) {
    out = {std::max(out.r, c.r), std::max(out.g, c.g), std::max(out.b, c.b)};
}

// Level scaled down when the bass tracker doubts the note: 1 - amount * (1 - confidence).
double confidence_scale(double amount, const EngineInputs &in) {
    return 1.0 - std::clamp(amount, 0.0, 1.0) * (1.0 - std::clamp(in.bass_confidence, 0.0, 1.0));
}

class PitchClock final : public Effect {
  public:
    explicit PitchClock(const PitchClockParams &p) : p_(p) {}
    const char *name() const override { return "Pitch clock"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        std::fill(out.begin(), out.end(), Rgb{});
        const EngineInputs &in = c.in;
        if (in.bass_notes_so_far <= 0) {
            return;
        }
        const HexTopology t = hex_topology(c.rig);
        const auto rings = static_cast<int>(t.rings.size());
        if (rings == 0) {
            return;
        }
        const double env = note_level(in, p_.release_s);
        const double v0 = p_.level * confidence_scale(p_.confidence, in) * env;
        if (v0 < 1e-4) {
            return;
        }
        const double pitch = in.bass_midi + in.bass_cents / 100.0;
        const double angle = frac((pitch - p_.top_pitch) / 12.0);
        const int octave = std::clamp(
            static_cast<int>(std::floor((in.bass_midi - p_.low_midi) / 12.0)), 0, rings - 1);
        const int ring = p_.ring_order == 0 ? octave : rings - 1 - octave;
        const int strip = t.rings[static_cast<size_t>(ring)];
        const double width = std::max(1e-3, p_.width);
        const Rgb colour =
            p_.colours == 0 ? hsv(270.0 + 28.0 * pitch, 1.0, 1.0) : c.palette.at(angle);
        for (size_t i = 0; i < out.size(); ++i) {
            if (c.rig.pixels[i].strip != strip) {
                continue;
            }
            const double d = turn_distance(t.turn[i], angle) / width;
            if (d < 3.0) {
                out[i] = scaled(colour, v0 * std::exp(-d * d));
            }
        }
        if (p_.spokes == 1) {
            std::vector<Rgb> spoke(c.rig.strips.size());
            bool any = false;
            for (const int s : t.spokes) {
                const double d = turn_distance(t.strip_turn[static_cast<size_t>(s)], angle);
                if (d < width) {
                    spoke[static_cast<size_t>(s)] =
                        scaled(colour, v0 * std::exp(-(d / width) * (d / width)));
                    any = true;
                }
            }
            for (size_t i = 0; any && i < out.size(); ++i) {
                max_into(out[i], spoke[static_cast<size_t>(c.rig.pixels[i].strip)]);
            }
        }
    }

  private:
    PitchClockParams p_;
};

} // namespace

std::unique_ptr<Effect> make_pitch_clock(const PitchClockParams &params) {
    return std::make_unique<PitchClock>(params);
}

} // namespace psyight
