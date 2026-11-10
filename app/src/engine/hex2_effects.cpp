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

double smoothstep(double edge, double x) {
    if (edge <= 0.0) {
        return x > 0.0 ? 1.0 : 0.0;
    }
    const double t = std::clamp(x / edge, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

class NoteArcs final : public Effect {
  public:
    explicit NoteArcs(const NoteArcsParams &p) : p_(p) {}
    const char *name() const override { return "Note arcs"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        std::fill(out.begin(), out.end(), Rgb{});
        const EngineInputs &in = c.in;
        if (in.bass_notes_so_far <= 0 || !std::isfinite(in.since_bass_onset_s)) {
            return;
        }
        const HexTopology t = hex_topology(c.rig);
        const auto rings = static_cast<int>(t.rings.size());
        if (rings == 0) {
            return;
        }
        const double beat_s = in.beat_s > 0.0 ? in.beat_s : 0.4;
        const double slot_s = in.beat_s > 0.0 ? in.beat_s / 4.0 : 0.1;
        const long long n = in.bass_notes_so_far - 1;
        int ring = 0;
        switch (p_.ring_mode) {
        case 0: // Cycle.
            ring = static_cast<int>(n % rings);
            break;
        case 1: { // By the onset's 16th within its beat: 1..3 to rings 0..2, the downbeat to 0.
            const double bar_start_s = in.t_s - in.bar_phase * 4.0 * beat_s;
            const double onset_s = in.t_s - in.since_bass_onset_s;
            const auto slot =
                static_cast<long long>(std::llround((onset_s - bar_start_s) / slot_s));
            const long long in_beat = ((slot % 4) + 4) % 4;
            ring = std::min(rings - 1, static_cast<int>(std::max(0LL, in_beat - 1)));
            break;
        }
        default: // Outer only.
            ring = rings - 1;
            break;
        }
        const double start = corner_turn(t.frame, static_cast<int>(n % 6));
        const double held_s =
            in.bass_active ? in.since_bass_onset_s : std::max(0.0, in.bass_gate_ratio) * slot_s;
        const double len = std::clamp(p_.arc_per_16th * held_s / slot_s, 0.0, 1.0);
        const double tau_s = in.bass_decay_tau_ms > 0.0 ? in.bass_decay_tau_ms * 1e-3 : 0.0;
        const double env =
            in.bass_active
                ? 1.0
                : std::exp(-in.since_bass_offset_s / std::max({1e-3, p_.release_s, tau_s}));
        const double swell = held_s > 2.0 * slot_s
                                 ? 1.0 + p_.breathe * std::sin(2.0 * kPi * 2.0 * in.t_s) * 0.5
                                 : 1.0;
        const double dir = p_.direction == 0   ? 1.0
                           : p_.direction == 1 ? -1.0
                                               : (in.bar_index % 2 == 0 ? 1.0 : -1.0);
        const double v0 = p_.level * confidence_scale(p_.confidence, in) *
                          std::clamp(in.bass_velocity, 0.0, 1.0) * env * swell;
        if (v0 < 1e-4 || len <= 0.0) {
            return;
        }
        const int strip = t.rings[static_cast<size_t>(ring)];
        const int ring_pixels = std::max(1, c.rig.strips[static_cast<size_t>(strip)].count);
        const double pitch_turns = 1.0 / ring_pixels; // One pixel, for the soft ends.
        const double note_pitch = in.bass_midi + in.bass_cents / 100.0;
        const Rgb colour = p_.colours == 0 ? hsv(270.0 + 28.0 * note_pitch, 1.0, 1.0)
                                           : c.palette.at(static_cast<double>(n % 6) / 6.0);
        for (size_t i = 0; i < out.size(); ++i) {
            if (c.rig.pixels[i].strip != strip) {
                continue;
            }
            const double a = frac((t.turn[i] - start) * dir);
            if (a >= len) {
                continue;
            }
            const double edge = smoothstep(pitch_turns, a) * smoothstep(pitch_turns, len - a);
            out[i] = scaled(colour, std::min(1.0, v0 * edge));
        }
    }

  private:
    NoteArcsParams p_;
};

} // namespace

std::unique_ptr<Effect> make_note_arcs(const NoteArcsParams &params) {
    return std::make_unique<NoteArcs>(params);
}

std::unique_ptr<Effect> make_pitch_clock(const PitchClockParams &params) {
    return std::make_unique<PitchClock>(params);
}

} // namespace psyight
