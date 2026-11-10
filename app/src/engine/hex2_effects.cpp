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

// The latest trigger that counts (every nth), and its age; false when none.
bool latest_every(const HitTrigger &t, int every, long long *index, double *age_s) {
    if (t.index < 0) {
        return false;
    }
    const long long n = std::max(1, every);
    for (long long k = 0; k < n; ++k) {
        const long long event = t.index - k;
        if (event < 0) {
            return false;
        }
        if (((event % n) + n) % n != 0) {
            continue;
        }
        double age = t.since_s;
        if (k > 0) {
            if (t.ages != nullptr && k < kEventHistory) {
                age = (*t.ages)[static_cast<size_t>(k)];
            } else if (t.spacing_s > 0.0) {
                age = t.since_s + static_cast<double>(k) * t.spacing_s;
            } else {
                return false;
            }
        }
        if (!std::isfinite(age)) {
            return false;
        }
        *index = event;
        *age_s = age;
        return true;
    }
    return false;
}

class SpokeKick final : public Effect {
  public:
    explicit SpokeKick(const SpokeKickParams &p) : p_(p) {}
    const char *name() const override { return "Spoke kick"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        std::fill(out.begin(), out.end(), Rgb{});
        long long index = 0;
        double since_s = 0.0;
        if (!latest_every(resolve_trigger(c.in, p_.trigger), p_.every, &index, &since_s)) {
            return;
        }
        const double beat_s = c.in.beat_s > 0.0 ? c.in.beat_s : kReferenceBeatS;
        const double env = hit_envelope(since_s, p_.hold_s, p_.decay_beats * beat_s);
        if (env < 1e-3) {
            return;
        }
        const HexTopology t = hex_topology(c.rig);
        const double span = std::max(1e-6, 1.0 - t.u_min);
        const double x = since_s / std::max(1e-3, p_.travel_beats * beat_s);
        const double head = overshoot_ease(std::min(x, 1.0), p_.overshoot);
        const double tail = std::max(0.01, p_.tail);
        const Rgb colour = p_.colours == 1
                               ? Rgb{1.0f, 1.0f, 1.0f}
                               : c.palette.at(0.25 * static_cast<double>(((index % 4) + 4) % 4));
        std::vector<char> is_spoke(c.rig.strips.size(), 0);
        for (const int s : t.spokes) {
            is_spoke[static_cast<size_t>(s)] = 1;
        }
        // Each ring's place along the spokes, for the flash as the head passes.
        std::vector<double> ring_w(c.rig.strips.size(), -1.0);
        if (p_.rings == 1) {
            std::vector<double> sum(c.rig.strips.size(), 0.0);
            std::vector<int> count(c.rig.strips.size(), 0);
            for (size_t i = 0; i < out.size(); ++i) {
                const auto s = static_cast<size_t>(c.rig.pixels[i].strip);
                sum[s] += t.radius[i];
                ++count[s];
            }
            for (const int r : t.rings) {
                const auto s = static_cast<size_t>(r);
                ring_w[s] = count[s] > 0 ? (sum[s] / count[s] - t.u_min) / span : -1.0;
            }
        }
        for (size_t i = 0; i < out.size(); ++i) {
            const auto s = static_cast<size_t>(c.rig.pixels[i].strip);
            const double w = (t.radius[i] - t.u_min) / span;
            double v = 0.0;
            if (is_spoke[s] != 0) {
                if (w <= head) {
                    v = std::max(0.0, 1.0 - (head - w) / tail);
                }
                if (since_s < p_.hold_s && w < 0.1) {
                    v = 1.0; // The hit is on the kick.
                }
            } else if (ring_w[s] >= 0.0) {
                const double d = (ring_w[s] - head) / 0.08;
                v = std::exp(-d * d);
            }
            if (v > 0.0) {
                out[i] = scaled(colour, std::min(1.0, p_.level * env * v));
            }
        }
    }

  private:
    SpokeKickParams p_;
};

class CornerSparks final : public Effect {
  public:
    explicit CornerSparks(const CornerSparksParams &p) : p_(p) {}
    const char *name() const override { return "Corner sparks"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        std::fill(out.begin(), out.end(), Rgb{});
        const HitTrigger trig = resolve_trigger(c.in, p_.trigger);
        if (trig.index < 0) {
            return;
        }
        const HexTopology t = hex_topology(c.rig);
        // Pixels per strip (strips are contiguous in rig.pixels, by index).
        std::vector<int> pixels(c.rig.strips.size(), 0);
        for (const Pixel &px : c.rig.pixels) {
            ++pixels[static_cast<size_t>(px.strip)];
        }
        // The sites: each ring's pixel nearest each corner, then each spoke's outer end.
        std::vector<size_t> sites;
        if (p_.sites != 1) {
            for (const int r : t.rings) {
                const auto s = static_cast<size_t>(r);
                for (int k = 0; k < 6; ++k) {
                    const double at = corner_turn(t.frame, k);
                    size_t best = static_cast<size_t>(t.first_pixel[s]);
                    for (int j = 0; j < pixels[s]; ++j) {
                        const auto i = static_cast<size_t>(t.first_pixel[s] + j);
                        if (turn_distance(t.turn[i], at) < turn_distance(t.turn[best], at)) {
                            best = i;
                        }
                    }
                    sites.push_back(best);
                }
            }
        }
        if (p_.sites != 0) {
            for (const int sp : t.spokes) {
                const auto s = static_cast<size_t>(sp);
                size_t best = static_cast<size_t>(t.first_pixel[s]);
                for (int j = 0; j < pixels[s]; ++j) {
                    const auto i = static_cast<size_t>(t.first_pixel[s] + j);
                    if (t.radius[i] > t.radius[best]) {
                        best = i;
                    }
                }
                sites.push_back(best);
            }
        }
        if (sites.empty()) {
            return;
        }
        const int keep = std::clamp(p_.keep, 1, kEventHistory);
        const int span = std::clamp(p_.span, 1, 8);
        for (int k = 0; k < keep; ++k) {
            const long long event = trig.index - k;
            if (event < 0) {
                break;
            }
            double age = trig.since_s;
            if (k > 0) {
                if (trig.ages != nullptr) {
                    age = (*trig.ages)[static_cast<size_t>(k)];
                } else if (trig.spacing_s > 0.0) {
                    age = trig.since_s + k * trig.spacing_s;
                } else {
                    break;
                }
            }
            if (!std::isfinite(age)) {
                break;
            }
            const double fade = p_.level * std::exp(-age / std::max(1e-3, p_.decay_s));
            if (fade < 1e-3) {
                continue;
            }
            for (int j = 0; j < std::clamp(p_.count, 1, 6); ++j) {
                const auto pick = std::min(
                    sites.size() - 1,
                    static_cast<size_t>(hash_unit(c.seed, static_cast<std::uint64_t>(event),
                                                  211 + static_cast<std::uint64_t>(j)) *
                                        static_cast<double>(sites.size())));
                const size_t site = sites[pick];
                const Pixel &at = c.rig.pixels[site];
                const auto s = static_cast<size_t>(at.strip);
                const bool ring = t.ring_of_strip[s] >= 0;
                const int n = pixels[s];
                const Rgb colour = c.palette.at(p_.colour_spread * static_cast<double>(pick) /
                                                static_cast<double>(sites.size()));
                for (int d = -span; d <= span; ++d) {
                    int idx = at.index + d;
                    if (ring) {
                        idx = ((idx % n) + n) % n;
                    } else if (idx < 0 || idx >= n) {
                        continue;
                    }
                    const auto i = static_cast<size_t>(t.first_pixel[s] + idx);
                    const double v = fade * (1.0 - std::abs(d) / (span + 1.0));
                    max_into(out[i], scaled(colour, std::min(1.0, v)));
                }
            }
        }
    }

  private:
    CornerSparksParams p_;
};

class PolarNoise final : public Effect {
  public:
    explicit PolarNoise(const PolarNoiseParams &p) : p_(p) {}
    const char *name() const override { return "Polar noise"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const HexFrame frame = hex_frame(c.rig);
        const std::vector<double> radius = hex_radii(c.rig, frame);
        const Clock clock = static_cast<Clock>(std::clamp(p_.clock, 0, kClockCount - 1));
        const double shift = p_.flow * clock_cycles(c.in, clock, 1.0);
        const double lobes = p_.lobes / (2.0 * kPi);
        const double timbre =
            p_.brightness_colour * std::clamp(c.in.residual_centroid_hz / 6000.0, 0.0, 1.0);
        for (size_t i = 0; i < out.size(); ++i) {
            const double r = radius[i] * p_.rings_scale - shift;
            // The cosine and sine of the turn (0 at the top, clockwise) from the position.
            const double dx = c.rig.pixels[i].position.x - frame.cx;
            const double dy = c.rig.pixels[i].position.y - frame.cy;
            const double d = std::hypot(dx, dy);
            const double cos_a = d > 1e-12 ? dy / d : 1.0;
            const double sin_a = d > 1e-12 ? dx / d : 0.0;
            const double n = fbm(r, lobes * cos_a, lobes * sin_a, p_.octaves, c.seed);
            const double shown =
                std::max(p_.floor, std::clamp(0.5 + (n - 0.5) * p_.contrast, 0.0, 1.0));
            out[i] = scaled(c.palette.at(p_.colour_spread * n + timbre), p_.level * shown);
        }
    }

  private:
    PolarNoiseParams p_;
};

} // namespace

std::unique_ptr<Effect> make_polar_noise(const PolarNoiseParams &params) {
    return std::make_unique<PolarNoise>(params);
}

std::unique_ptr<Effect> make_corner_sparks(const CornerSparksParams &params) {
    return std::make_unique<CornerSparks>(params);
}

std::unique_ptr<Effect> make_spoke_kick(const SpokeKickParams &params) {
    return std::make_unique<SpokeKick>(params);
}

std::unique_ptr<Effect> make_note_arcs(const NoteArcsParams &params) {
    return std::make_unique<NoteArcs>(params);
}

std::unique_ptr<Effect> make_pitch_clock(const PitchClockParams &params) {
    return std::make_unique<PitchClock>(params);
}

} // namespace psyight
