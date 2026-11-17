// The hexagon-visuals effects (engine/scene.h; docs/PLAN_HEX_VISUALS.md section 6 on): built on
// engine/space.h's hex topology (rings innermost first, spokes, each pixel's hex radius and turn)
// and engine/shape.h's helpers. Pure functions of the rig, the inputs, the palette and the seed.
// On a rig with no hexagon rings, the ring effects render black.

#include "engine/scene.h"
#include "engine/shape.h"
#include "engine/space.h"

#include <algorithm>
#include <array>
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
        // The trail: earlier notes as markers at half the level each step back.
        for (int k = 1; k <= std::clamp(p_.trail, 0, kNoteHistory - 1); ++k) {
            const NoteEvent &e = in.notes[static_cast<size_t>(k)];
            if (!std::isfinite(e.age_s) || e.age_s <= 0.0 || in.bass_notes_so_far <= k) {
                break;
            }
            const double trail_pitch = e.midi + e.cents / 100.0;
            const double trail_angle = frac((trail_pitch - p_.top_pitch) / 12.0);
            const int trail_octave = std::clamp(
                static_cast<int>(std::floor((e.midi - p_.low_midi) / 12.0)), 0, rings - 1);
            const int trail_strip = t.rings[static_cast<size_t>(
                p_.ring_order == 0 ? trail_octave : rings - 1 - trail_octave)];
            const double v = v0 * std::pow(0.5, k);
            const Rgb trail_colour = p_.colours == 0 ? hsv(270.0 + 28.0 * trail_pitch, 1.0, 1.0)
                                                     : c.palette.at(trail_angle);
            for (size_t i = 0; i < out.size(); ++i) {
                if (c.rig.pixels[i].strip != trail_strip) {
                    continue;
                }
                const double d = turn_distance(t.turn[i], trail_angle) / width;
                if (d < 3.0) {
                    max_into(out[i], scaled(trail_colour, v * std::exp(-d * d)));
                }
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
        // The ring for note number `note` whose onset was age_s ago.
        const auto ring_for = [&](long long note, double age_s) {
            switch (p_.ring_mode) {
            case 0: // Cycle.
                return static_cast<int>(((note % rings) + rings) % rings);
            case 1: { // By the onset's 16th within its beat: 1..3 to rings 0..2, the downbeat 0.
                const double bar_start_s = in.t_s - in.bar_phase * 4.0 * beat_s;
                const double onset_s = in.t_s - age_s;
                const auto slot =
                    static_cast<long long>(std::llround((onset_s - bar_start_s) / slot_s));
                const long long in_beat = ((slot % 4) + 4) % 4;
                return std::min(rings - 1, static_cast<int>(std::max(0LL, in_beat - 1)));
            }
            default: // Outer only.
                return rings - 1;
            }
        };
        const double dir = p_.direction == 0   ? 1.0
                           : p_.direction == 1 ? -1.0
                                               : (in.bar_index % 2 == 0 ? 1.0 : -1.0);
        // One note's arc: `len` turns from its corner, combined with max.
        const auto draw = [&](int ring, long long note, double len, double v0, double midi) {
            if (v0 < 1e-4 || len <= 0.0) {
                return;
            }
            const double start = corner_turn(t.frame, static_cast<int>(((note % 6) + 6) % 6));
            const int strip = t.rings[static_cast<size_t>(ring)];
            const int ring_pixels = std::max(1, c.rig.strips[static_cast<size_t>(strip)].count);
            const double pitch_turns = 1.0 / ring_pixels; // One pixel, for the soft ends.
            const Rgb colour = p_.colours == 0
                                   ? hsv(270.0 + 28.0 * midi, 1.0, 1.0)
                                   : c.palette.at(static_cast<double>(((note % 6) + 6) % 6) / 6.0);
            for (size_t i = 0; i < out.size(); ++i) {
                if (c.rig.pixels[i].strip != strip) {
                    continue;
                }
                const double a = frac((t.turn[i] - start) * dir);
                if (a >= len) {
                    continue;
                }
                const double edge = smoothstep(pitch_turns, a) * smoothstep(pitch_turns, len - a);
                max_into(out[i], scaled(colour, std::min(1.0, v0 * edge)));
            }
        };
        // Earlier notes from the history (note_history > 1): each on its ring, by its length.
        for (int k = std::min(std::clamp(p_.history, 1, kNoteHistory), static_cast<int>(n + 1)) - 1;
             k >= 1; --k) {
            const NoteEvent &e = in.notes[static_cast<size_t>(k)];
            if (e.length_s <= 0.0 || !std::isfinite(e.age_s)) {
                continue;
            }
            const double len = std::clamp(p_.arc_per_16th * e.length_s / slot_s, 0.0, 1.0);
            const double tau_s = e.decay_tau_ms > 0.0 ? e.decay_tau_ms * 1e-3 : 0.0;
            const double fade = e.sounding ? 1.0
                                           : std::exp(-std::max(0.0, e.age_s - e.length_s) /
                                                      std::max({1e-3, p_.release_s, tau_s}));
            const double conf = 1.0 - std::clamp(p_.confidence, 0.0, 1.0) *
                                          (1.0 - std::clamp(e.confidence, 0.0, 1.0));
            draw(ring_for(n - k, e.age_s), n - k, len,
                 p_.level * conf * std::clamp(e.velocity, 0.0, 1.0) * fade,
                 e.midi + e.cents / 100.0);
        }
        // The latest note, from the bass fields.
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
        const double v0 = p_.level * confidence_scale(p_.confidence, in) *
                          std::clamp(in.bass_velocity, 0.0, 1.0) * env * swell;
        draw(ring_for(n, in.since_bass_onset_s), n, len, v0, in.bass_midi + in.bass_cents / 100.0);
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

// Where in the phrase, 0 to 1: the phrase phase, or before the first phrase, bars in eights.
double phrase_position(const EngineInputs &in) {
    if (in.phrase_index >= 0) {
        return std::clamp(in.phrase_phase, 0.0, 1.0);
    }
    if (in.bar_index < 0) {
        return 0.0;
    }
    return frac(static_cast<double>(in.bar_index) / 8.0 + in.bar_phase / 8.0);
}

class PhraseFill final : public Effect {
  public:
    explicit PhraseFill(const PhraseFillParams &p) : p_(p) {}
    const char *name() const override { return "Phrase fill"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        std::fill(out.begin(), out.end(), Rgb{});
        const size_t n = out.size();
        if (n == 0) {
            return;
        }
        const HexTopology t = hex_topology(c.rig);
        // Each pixel's unit rank in the fill order, and the number of units.
        std::vector<long long> rank(n, 0);
        long long units = 0;
        if (p_.order == 0) { // By side: each ring's six sides, innermost first, then the spokes.
            const auto rings = static_cast<long long>(t.rings.size());
            std::vector<int> spokes = t.spokes;
            std::sort(spokes.begin(), spokes.end(), [&](int a, int b) {
                return t.strip_turn[static_cast<size_t>(a)] < t.strip_turn[static_cast<size_t>(b)];
            });
            std::vector<long long> spoke_rank(c.rig.strips.size(), 0);
            for (size_t k = 0; k < spokes.size(); ++k) {
                spoke_rank[static_cast<size_t>(spokes[k])] = static_cast<long long>(k);
            }
            const double first_corner = corner_turn(t.frame, 0);
            for (size_t i = 0; i < n; ++i) {
                const auto s = static_cast<size_t>(c.rig.pixels[i].strip);
                const int ring = t.ring_of_strip[s];
                if (ring >= 0) {
                    const auto side =
                        std::min(5LL, static_cast<long long>(frac(t.turn[i] - first_corner) * 6.0));
                    rank[i] = ring * 6 + side;
                } else {
                    rank[i] = rings * 6 + spoke_rank[s];
                }
            }
            units = rings * 6 + static_cast<long long>(spokes.size());
        } else {
            std::vector<size_t> order(n);
            for (size_t i = 0; i < n; ++i) {
                order[i] = i;
            }
            const auto ring_of = [&](size_t i) {
                return t.ring_of_strip[static_cast<size_t>(c.rig.pixels[i].strip)];
            };
            if (p_.order == 1) { // Inner to outer.
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                    return t.radius[a] != t.radius[b] ? t.radius[a] < t.radius[b]
                                                      : t.turn[a] < t.turn[b];
                });
            } else if (p_.order == 2) { // Spokes by radius, then the rings by ring and turn.
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                    const int ra = ring_of(a);
                    const int rb = ring_of(b);
                    if ((ra >= 0) != (rb >= 0)) {
                        return ra < 0;
                    }
                    if (ra < 0) {
                        return t.radius[a] < t.radius[b];
                    }
                    return ra != rb ? ra < rb : t.turn[a] < t.turn[b];
                });
            } else { // Random pixels.
                std::vector<double> key(n);
                for (size_t i = 0; i < n; ++i) {
                    key[i] = hash_unit(c.seed, i, 97);
                }
                std::sort(order.begin(), order.end(),
                          [&](size_t a, size_t b) { return key[a] < key[b]; });
            }
            for (size_t k = 0; k < n; ++k) {
                rank[order[k]] = static_cast<long long>(k);
            }
            units = static_cast<long long>(n);
        }
        if (units <= 0) {
            return;
        }
        // Unit r fills at step floor(r * (steps - 1) / units) + 1: none at the phrase's start,
        // all by its last step.
        const int steps = std::clamp(p_.steps, 4, 128);
        const double phase = phrase_position(c.in);
        const double at_steps = phase * steps;
        const auto done = static_cast<long long>(std::floor(at_steps));
        const double step_beats = 32.0 / steps; // An 8-bar phrase taken as 32 beats.
        double clear = 1.0;
        if (p_.clear_beats > 0.0) {
            clear = std::clamp((1.0 - phase) / (p_.clear_beats / 32.0), 0.0, 1.0);
        }
        for (size_t i = 0; i < n; ++i) {
            const long long first = rank[i] * (steps - 1) / units + 1;
            if (first > done) {
                continue;
            }
            const double age_beats = (at_steps - static_cast<double>(first)) * step_beats;
            const double v =
                p_.hold_level + (p_.head_level - p_.hold_level) *
                                    std::exp(-age_beats / std::max(1e-3, p_.head_decay_beats));
            out[i] = scaled(c.palette.at(p_.colour_spread * static_cast<double>(rank[i]) /
                                         static_cast<double>(units)),
                            std::clamp(p_.level * v * clear, 0.0, 1.0));
        }
    }

  private:
    PhraseFillParams p_;
};

constexpr int kMaxGenerations = 512; // Cap on generations run per render.

// A ring of n cells as bits, 64 to a word (bits past n kept 0), stepped bit-parallel.
class CellRing {
  public:
    explicit CellRing(int n) : n_(n), words_(static_cast<size_t>((n + 63) / 64), 0) {}
    void set(int j) { words_[static_cast<size_t>(j / 64)] |= 1ull << (j % 64); }
    bool get(int j) const { return (words_[static_cast<size_t>(j / 64)] >> (j % 64)) & 1ull; }

    // One generation of the elementary automaton `rule` (Wolfram's numbering), periodic.
    void step(unsigned rule) {
        const size_t w = words_.size();
        left_.resize(w);  // Cell j-1 at bit j.
        right_.resize(w); // Cell j+1 at bit j.
        const std::uint64_t last_bit = get(n_ - 1) ? 1ull : 0ull;
        const std::uint64_t first_bit = get(0) ? 1ull : 0ull;
        for (size_t k = 0; k < w; ++k) {
            left_[k] = (words_[k] << 1) | (k > 0 ? words_[k - 1] >> 63 : last_bit);
            right_[k] = (words_[k] >> 1) | (k + 1 < w ? words_[k + 1] << 63 : 0ull);
        }
        right_[w - 1] |= first_bit << ((n_ - 1) % 64);
        for (size_t k = 0; k < w; ++k) {
            const std::uint64_t l = left_[k];
            const std::uint64_t m = words_[k];
            const std::uint64_t r = right_[k];
            std::uint64_t next = 0;
            for (unsigned pattern = 0; pattern < 8; ++pattern) {
                if ((rule >> pattern) & 1u) {
                    next |= ((pattern & 4u) ? l : ~l) & ((pattern & 2u) ? m : ~m) &
                            ((pattern & 1u) ? r : ~r);
                }
            }
            words_[k] = next;
        }
        if (n_ % 64 != 0) {
            words_[w - 1] &= (1ull << (n_ % 64)) - 1;
        }
    }

  private:
    int n_;
    std::vector<std::uint64_t> words_;
    std::vector<std::uint64_t> left_;
    std::vector<std::uint64_t> right_;
};

class RingAutomaton final : public Effect {
  public:
    explicit RingAutomaton(const RingAutomatonParams &p) : p_(p) {}
    const char *name() const override { return "Ring automaton"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        std::fill(out.begin(), out.end(), Rgb{});
        const HexTopology t = hex_topology(c.rig);
        const auto rings = static_cast<int>(t.rings.size());
        if (rings == 0) {
            return;
        }
        const int generations = std::clamp(p_.generations, 8, 256);
        const double at = phrase_position(c.in) * generations;
        const int g = std::min(static_cast<int>(std::floor(at)), kMaxGenerations);
        const double f = at - std::floor(at);
        const double mix = p_.crossfade > 0.0 && f < p_.crossfade && g > 0 ? f / p_.crossfade : 1.0;
        const auto rule = static_cast<unsigned>(std::clamp(p_.rule, 0, 255));
        const auto phrase = static_cast<std::uint64_t>(std::max(0, c.in.phrase_index));
        std::vector<double> value(out.size(), 0.0); // Per pixel: its cell's value.
        for (int r = 0; r < rings; ++r) {
            const auto strip = static_cast<size_t>(t.rings[static_cast<size_t>(r)]);
            const int first = t.first_pixel[strip];
            int n = 0;
            while (first + n < static_cast<int>(out.size()) &&
                   c.rig.pixels[static_cast<size_t>(first + n)].strip == static_cast<int>(strip)) {
                ++n;
            }
            if (n < 3) {
                continue;
            }
            // The cell nearest each corner, in one pass (and the top is corner 0's turn when the
            // corners point up and down).
            std::array<int, 6> corner_cell{};
            std::array<double, 6> corner_distance;
            corner_distance.fill(1.0);
            const double corner0 = corner_turn(t.frame, 0);
            for (int j = 0; j < n; ++j) {
                const double from = frac(t.turn[static_cast<size_t>(first + j)] - corner0) * 6.0;
                const auto k = static_cast<int>(std::lround(from)) % 6;
                const double d = std::fabs(from - std::round(from));
                if (d < corner_distance[static_cast<size_t>(k)]) {
                    corner_distance[static_cast<size_t>(k)] = d;
                    corner_cell[static_cast<size_t>(k)] = j;
                }
            }
            CellRing cells(n);
            if (p_.seed_mode == 0) {
                // The corners: a sixth of the ring apart when it divides by six (a corner's
                // joint gap leaves two pixels equally near it), else each nearest its corner.
                for (int k = 0; k < 6; ++k) {
                    cells.set(n % 6 == 0 ? (corner_cell[0] + k * n / 6) % n
                                         : corner_cell[static_cast<size_t>(k)]);
                }
            } else if (p_.seed_mode == 1) {
                for (int j = 0; j < n; ++j) {
                    if (hash_unit(c.seed, phrase,
                                  1000 * static_cast<std::uint64_t>(r) +
                                      static_cast<std::uint64_t>(j)) < p_.density) {
                        cells.set(j);
                    }
                }
            } else { // One cell: the one nearest the top.
                int top = 0;
                for (int j = 1; j < n; ++j) {
                    if (turn_distance(t.turn[static_cast<size_t>(first + j)], 0.0) <
                        turn_distance(t.turn[static_cast<size_t>(first + top)], 0.0)) {
                        top = j;
                    }
                }
                cells.set(top);
            }
            for (int step = 0; step + 1 < g; ++step) {
                cells.step(rule);
            }
            const CellRing previous = cells; // Generation g - 1 (the seed when g is 0).
            if (g > 0) {
                cells.step(rule);
            }
            const Rgb colour = c.palette.at(p_.colour_spread * r / rings);
            for (int j = 0; j < n; ++j) {
                const auto i = static_cast<size_t>(first + j);
                const double before = previous.get(j) ? 1.0 : 0.0;
                const double now = cells.get(j) ? 1.0 : 0.0;
                value[i] = before + (now - before) * mix;
                if (value[i] > 0.0) {
                    out[i] = scaled(colour, p_.level * value[i]);
                }
            }
        }
        if (p_.spokes == 1) {
            for (const int sp : t.spokes) {
                const auto s = static_cast<size_t>(sp);
                // The spoke's inner end, the ring nearest it, and that ring's cell at its turn.
                double inner = 1e9;
                for (size_t i = 0; i < out.size(); ++i) {
                    if (c.rig.pixels[i].strip == sp) {
                        inner = std::min(inner, t.radius[i]);
                    }
                }
                size_t cell = out.size();
                double best = 1e9;
                for (size_t i = 0; i < out.size(); ++i) {
                    const auto rs = static_cast<size_t>(c.rig.pixels[i].strip);
                    if (t.ring_of_strip[rs] < 0) {
                        continue;
                    }
                    const double d =
                        std::abs(t.radius[i] - inner) + turn_distance(t.turn[i], t.strip_turn[s]);
                    if (d < best) {
                        best = d;
                        cell = i;
                    }
                }
                if (cell == out.size() || value[cell] <= 0.0) {
                    continue;
                }
                for (size_t i = 0; i < out.size(); ++i) {
                    if (c.rig.pixels[i].strip == sp) {
                        out[i] = out[cell];
                    }
                }
            }
        }
    }

  private:
    RingAutomatonParams p_;
};

// Arm notes: the arms (strips tagged "arms", else every strip that is not a closed ring), in turn
// round the bounding box's centre from the top, clockwise, and each pixel's place along its arm
// from the end nearer the centre (0) to the far end (1), by distance from the centre.
struct Arms {
    std::vector<int> strips;     // In turn order.
    std::vector<int> arm_of;     // Per pixel: index into strips, -1 off the arms.
    std::vector<double> along;   // Per pixel: 0 at the inner end, 1 at the outer.
    std::vector<double> pitch_u; // Per arm: one pixel's share of its length.
};

Arms arms_of(const Rig &rig) {
    const size_t n = rig.strips.size();
    std::vector<char> is_arm(n, 0);
    bool tagged = false;
    for (size_t s = 0; s < n; ++s) {
        const auto &g = rig.strips[s].groups;
        is_arm[s] = std::find(g.begin(), g.end(), "arms") != g.end();
        tagged = tagged || is_arm[s];
    }
    if (!tagged) {
        for (size_t s = 0; s < n; ++s) {
            const auto &p = rig.strips[s].polyline;
            const bool closed =
                p.size() > 2 && std::hypot(p.front().x - p.back().x, p.front().y - p.back().y,
                                           p.front().z - p.back().z) < 1e-3;
            is_arm[s] = !closed;
        }
    }
    const double cx = 0.5 * (rig.min.x + rig.max.x);
    const double cy = 0.5 * (rig.min.y + rig.max.y);
    std::vector<double> sx(n, 0.0);
    std::vector<double> sy(n, 0.0);
    std::vector<double> lo(n, 1e300);
    std::vector<double> hi(n, -1e300);
    std::vector<int> count(n, 0);
    std::vector<double> d(rig.pixels.size(), 0.0);
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        const Pixel &p = rig.pixels[i];
        const auto s = static_cast<size_t>(p.strip);
        if (s >= n || !is_arm[s]) {
            continue;
        }
        d[i] = std::hypot(p.position.x - cx, p.position.y - cy);
        sx[s] += p.position.x;
        sy[s] += p.position.y;
        lo[s] = std::min(lo[s], d[i]);
        hi[s] = std::max(hi[s], d[i]);
        ++count[s];
    }
    std::vector<std::pair<double, int>> by_turn;
    for (size_t s = 0; s < n; ++s) {
        if (is_arm[s] && count[s] > 0) {
            double turn =
                frac(std::atan2(sx[s] / count[s] - cx, sy[s] / count[s] - cy) / (2.0 * kPi));
            if (turn > 1.0 - 1e-9) {
                turn = 0.0; // An arm straight up, a rounding error left of the top.
            }
            by_turn.emplace_back(turn, static_cast<int>(s));
        }
    }
    std::sort(by_turn.begin(), by_turn.end());
    Arms a;
    std::vector<int> index(n, -1);
    for (const auto &[turn, s] : by_turn) {
        index[static_cast<size_t>(s)] = static_cast<int>(a.strips.size());
        a.strips.push_back(s);
        a.pitch_u.push_back(1.0 / std::max(1, count[static_cast<size_t>(s)] - 1));
    }
    a.arm_of.assign(rig.pixels.size(), -1);
    a.along.assign(rig.pixels.size(), 0.0);
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        const auto s = static_cast<size_t>(rig.pixels[i].strip);
        if (s < n && index[s] >= 0) {
            a.arm_of[i] = index[s];
            a.along[i] = hi[s] - lo[s] > 1e-9 ? (d[i] - lo[s]) / (hi[s] - lo[s]) : 0.0;
        }
    }
    return a;
}

class ArmNotes final : public Effect {
  public:
    explicit ArmNotes(const ArmNotesParams &p) : p_(p) {}
    const char *name() const override { return "Arm notes"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        std::fill(out.begin(), out.end(), Rgb{});
        const EngineInputs &in = c.in;
        if (in.bass_notes_so_far <= 0 || !std::isfinite(in.since_bass_onset_s)) {
            return;
        }
        const Arms a = arms_of(c.rig);
        const auto arms = static_cast<int>(a.strips.size());
        if (arms == 0) {
            return;
        }
        const double slot_s = in.beat_s > 0.0 ? in.beat_s / 4.0 : 0.1;
        const double beat_s = in.beat_s > 0.0 ? in.beat_s : 0.4;
        const auto notes =
            static_cast<int>(std::min<long long>(kNoteHistory, in.bass_notes_so_far));
        // Scale step: the distinct pitches of the notes in the history, low to high.
        std::vector<int> pitches;
        if (p_.arm_mode == 1) {
            pitches.push_back(in.bass_midi);
            for (int k = 1; k < notes; ++k) {
                if (std::isfinite(in.notes[static_cast<size_t>(k)].age_s)) {
                    pitches.push_back(in.notes[static_cast<size_t>(k)].midi);
                }
            }
            std::sort(pitches.begin(), pitches.end());
            pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
        }
        // The arm a pitch lights; -1: every arm.
        const auto arm_for = [&](int midi) {
            switch (p_.arm_mode) {
            case 0:
                return ((((midi - p_.top_pitch) % 12) + 12) % 12) % arms;
            case 1: {
                const auto rank = static_cast<int>(
                    std::lower_bound(pitches.begin(), pitches.end(), midi) - pitches.begin());
                const int distinct = std::max(1, static_cast<int>(pitches.size()));
                return std::min(arms - 1, rank * arms / distinct);
            }
            default:
                return -1;
            }
        };
        std::vector<double> head(static_cast<size_t>(arms), 0.0);
        std::vector<double> value(static_cast<size_t>(arms), 0.0);
        std::vector<Rgb> colour(static_cast<size_t>(arms));
        // A note on its arm (or every arm), lit from the inner end to `reach`; the brightest
        // note on an arm wins.
        const auto put = [&](int arm, double reach, double v, double pitch) {
            if (v < 2e-3 || reach <= 0.0) {
                return;
            }
            const int first = arm < 0 ? 0 : arm;
            const int last = arm < 0 ? arms - 1 : arm;
            for (int k = first; k <= last; ++k) {
                const auto u = static_cast<size_t>(k);
                if (v > value[u]) {
                    value[u] = v;
                    head[u] = reach;
                    colour[u] = p_.colours == 0 ? hsv(270.0 + 28.0 * pitch, 1.0, 1.0)
                                                : c.palette.at(static_cast<double>(k) / arms);
                }
            }
        };
        const double per_16th = std::max(0.0, p_.per_16th);
        const double release = std::max(0.1, p_.release);
        // A note's fade time constant: its decay times the release, at least floor_s.
        const auto fade_s = [&](double tau_ms, double floor_s) {
            return std::max(floor_s, release * std::max(0.0, tau_ms) * 1e-3);
        };
        // Earlier notes (trail): each on its arm at its final reach, dimmer by age, fading over
        // at least a beat once ended.
        const int trail = std::min(std::clamp(p_.trail, 0, kNoteHistory - 1), notes - 1);
        for (int k = trail; k >= 1; --k) {
            const NoteEvent &e = in.notes[static_cast<size_t>(k)];
            if (!std::isfinite(e.age_s) || e.length_s <= 0.0) {
                continue;
            }
            const double reach = std::clamp(per_16th * e.length_s / slot_s, 0.0, 1.0);
            const double fade = e.sounding ? 1.0
                                           : std::exp(-std::max(0.0, e.age_s - e.length_s) /
                                                      fade_s(e.decay_tau_ms, beat_s));
            const double conf = 1.0 - std::clamp(p_.confidence, 0.0, 1.0) *
                                          (1.0 - std::clamp(e.confidence, 0.0, 1.0));
            const double by_age = 1.0 - static_cast<double>(k) / (trail + 1);
            put(arm_for(e.midi), reach,
                p_.level * conf * std::clamp(e.velocity, 0.0, 1.0) * fade * by_age,
                e.midi + e.cents / 100.0);
        }
        // The latest note, from the bass fields: its head travels out while it sounds.
        const double held_s =
            in.bass_active ? in.since_bass_onset_s : std::max(0.0, in.bass_gate_ratio) * slot_s;
        const double reach = std::clamp(per_16th * held_s / slot_s, 0.0, 1.0);
        const double env =
            in.bass_active ? 1.0
                           : std::exp(-in.since_bass_offset_s / fade_s(in.bass_decay_tau_ms, 0.03));
        const double v0 = p_.level * confidence_scale(p_.confidence, in) *
                          std::clamp(in.bass_velocity, 0.0, 1.0) * env;
        const double pitch = in.bass_midi + in.bass_cents / 100.0;
        const int arm = arm_for(in.bass_midi);
        const NoteEvent &prev = in.notes[1];
        const int from = notes > 1 ? arm_for(prev.midi) : arm;
        if (in.bass_slide && arm >= 0 && from != arm) {
            // A slide: from the previous note's arm to this one's over slide_16ths.
            const double w = std::clamp(
                in.since_bass_onset_s / (std::max(0.25, p_.slide_16ths) * slot_s), 0.0, 1.0);
            put(from, reach, v0 * (1.0 - w), prev.midi + prev.cents / 100.0);
            put(arm, reach, v0 * w, pitch);
        } else {
            put(arm, reach, v0, pitch);
        }
        for (size_t i = 0; i < out.size(); ++i) {
            const int k = a.arm_of[i];
            if (k < 0) {
                continue;
            }
            const auto u = static_cast<size_t>(k);
            if (value[u] <= 0.0) {
                continue;
            }
            // Soft over one pixel at the head.
            const double edge = std::clamp((head[u] - a.along[i]) / a.pitch_u[u] + 0.5, 0.0, 1.0);
            if (edge > 0.0) {
                out[i] = scaled(colour[u], std::min(1.0, value[u] * edge));
            }
        }
    }

  private:
    ArmNotesParams p_;
};

} // namespace

std::unique_ptr<Effect> make_arm_notes(const ArmNotesParams &params) {
    return std::make_unique<ArmNotes>(params);
}

std::unique_ptr<Effect> make_ring_automaton(const RingAutomatonParams &params) {
    return std::make_unique<RingAutomaton>(params);
}

std::unique_ptr<Effect> make_phrase_fill(const PhraseFillParams &params) {
    return std::make_unique<PhraseFill>(params);
}

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
