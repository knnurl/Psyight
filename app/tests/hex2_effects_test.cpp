// The hexagon-visuals effects (docs/PLAN_HEX_VISUALS.md phases 5 to 9) and the hexagon topology
// they share (engine/space.h hex_topology), on the hexagon mandala preset.

#include "effect_test_helpers.h"
#include "engine/shape.h"
#include "engine/space.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace psyight;
using test::grid_at;
using test::level;
using test::mandala;
using test::pixels_of;
using test::render;

namespace {

// A bass note sounding on the grid at 150 BPM, `since_s` after its onset.
EngineInputs note(int midi, double cents = 0.0, double since_s = 0.0) {
    EngineInputs in = grid_at(40, 0.25);
    in.section = "groove";
    in.bass_notes_so_far = 120;
    in.bass_active = true;
    in.bass_midi = midi;
    in.bass_cents = cents;
    in.bass_velocity = 1.0;
    in.bass_confidence = 1.0;
    in.bass_decay_tau_ms = 400.0;
    in.bass_gate_ratio = 1.0;
    in.since_bass_onset_s = since_s;
    in.bass_onset_ages_s = {since_s, since_s + 0.1, since_s + 0.2, since_s + 0.3};
    return in;
}

// The strips of the lit pixels (above 0.01).
std::vector<int> lit_strips(const Rig &rig, const Frame &out) {
    std::vector<int> strips;
    for (size_t i = 0; i < out.size(); ++i) {
        if (level(out[i]) > 0.01f &&
            std::find(strips.begin(), strips.end(), rig.pixels[i].strip) == strips.end()) {
            strips.push_back(rig.pixels[i].strip);
        }
    }
    std::sort(strips.begin(), strips.end());
    return strips;
}

// The brightness-weighted circular mean turn of the lit pixels.
double lit_turn(const HexTopology &t, const Frame &out) {
    double s = 0.0;
    double c = 0.0;
    for (size_t i = 0; i < out.size(); ++i) {
        const double w = level(out[i]);
        s += w * std::sin(2.0 * 3.14159265358979323846 * t.turn[i]);
        c += w * std::cos(2.0 * 3.14159265358979323846 * t.turn[i]);
    }
    const double a = std::atan2(s, c) / (2.0 * 3.14159265358979323846);
    return a - std::floor(a);
}

bool all_black(const Frame &out) {
    return std::all_of(out.begin(), out.end(), [](const Rgb &c) { return level(c) == 0.0f; });
}

// Every new effect: two renders equal, every channel within 0 to 1.
void check_sane(const EffectSpec &spec, const Rig &rig, const EngineInputs &in) {
    const Frame a = render(spec, rig, in);
    CHECK(a == render(spec, rig, in));
    for (const Rgb &c : a) {
        CHECK((c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f && c.b >= 0.0f &&
               c.b <= 1.0f));
    }
}

} // namespace

TEST_CASE("hex topology: the mandala's rings innermost first, its spokes, and the corners") {
    const Rig rig = mandala();
    const HexTopology t = hex_topology(rig);
    // Strips: outer, middle, inner hexagon, then six spokes.
    REQUIRE(t.rings == std::vector<int>{2, 1, 0});
    CHECK(t.spokes == std::vector<int>{3, 4, 5, 6, 7, 8});
    CHECK(t.ring_of_strip[0] == 2);
    CHECK(t.ring_of_strip[2] == 0);
    CHECK(t.ring_of_strip[3] == -1);
    CHECK(t.u_min == doctest::Approx(1.0 / 3.0).epsilon(0.05));
    REQUIRE(t.radius.size() == rig.pixels.size());
    for (size_t s = 0; s < rig.strips.size(); ++s) {
        CHECK(rig.pixels[static_cast<size_t>(t.first_pixel[s])].strip == static_cast<int>(s));
        CHECK(rig.pixels[static_cast<size_t>(t.first_pixel[s])].index == 0);
    }
    // The outer-ring pixel nearest each corner's turn is the one nearest the polyline's corner.
    const std::vector<size_t> outer = pixels_of(rig, 0);
    const std::vector<Vec3> &corners = rig.strips[0].polyline;
    for (int k = 0; k < 6; ++k) {
        CAPTURE(k);
        const double at = corner_turn(t.frame, k);
        const size_t by_turn =
            *std::min_element(outer.begin(), outer.end(), [&](size_t a, size_t b) {
                return turn_distance(t.turn[a], at) < turn_distance(t.turn[b], at);
            });
        const Vec3 corner =
            *std::min_element(corners.begin(), corners.end(), [&](const Vec3 &a, const Vec3 &b) {
                return turn_distance(hex_turn(t.frame, a.x, a.y), at) <
                       turn_distance(hex_turn(t.frame, b.x, b.y), at);
            });
        CHECK(turn_distance(hex_turn(t.frame, corner.x, corner.y), at) < 1e-6);
        const size_t by_place =
            *std::min_element(outer.begin(), outer.end(), [&](size_t a, size_t b) {
                const Vec3 &p = rig.pixels[a].position;
                const Vec3 &q = rig.pixels[b].position;
                return std::hypot(p.x - corner.x, p.y - corner.y) <
                       std::hypot(q.x - corner.x, q.y - corner.y);
            });
        // The same pixel, or its neighbour equally near (a corner's joint gap leaves a pair).
        const auto distance = [&](size_t i) {
            return std::hypot(rig.pixels[i].position.x - corner.x,
                              rig.pixels[i].position.y - corner.y);
        };
        CHECK(distance(by_turn) <= distance(by_place) + 1e-3);
    }
}

TEST_CASE("hex topology: a rig without closed strips has no rings") {
    const HexTopology t = hex_topology(default_rig());
    CHECK(t.rings.empty());
    CHECK(t.spokes.size() == default_rig().strips.size());
}

TEST_CASE("pitch clock: the pitch class round the hexagon, the octave picks the ring") {
    const Rig rig = mandala();
    const HexTopology t = hex_topology(rig);
    PitchClockParams p; // Lowest B0 (23), low notes inside, C at the top.
    // B1: the middle ring only, centred on turn 11/12 (the spokes are a twelfth away).
    const Frame b1 = render({p}, rig, note(35));
    CHECK(lit_strips(rig, b1) == std::vector<int>{t.rings[1]});
    CHECK(turn_distance(lit_turn(t, b1), 11.0 / 12.0) < 0.005);
    // B0: the inner ring at the same turn.
    const Frame b0 = render({p}, rig, note(23));
    CHECK(lit_strips(rig, b0) == std::vector<int>{t.rings[0]});
    CHECK(turn_distance(lit_turn(t, b0), 11.0 / 12.0) < 0.005);
    // 50 cents sharp: a 24th of a turn on.
    const Frame sharp = render({p}, rig, note(35, 50.0));
    CHECK(turn_distance(lit_turn(t, sharp), 11.0 / 12.0 + 1.0 / 24.0) < 0.005);
    // Low notes outside: B0 on the outer ring.
    p.ring_order = 1;
    CHECK(lit_strips(rig, render({p}, rig, note(23))) == std::vector<int>{t.rings[2]});
    // A note at a corner lights the spoke there too: C (turn 0) at the top.
    p.ring_order = 0;
    const std::vector<int> c_strips = lit_strips(rig, render({p}, rig, note(36)));
    CHECK(c_strips.size() == 2);
    // No note yet: black.
    EngineInputs none = note(35);
    none.bass_notes_so_far = 0;
    CHECK(all_black(render({p}, rig, none)));
    // A doubted note is dimmer.
    EngineInputs doubted = note(35);
    doubted.bass_confidence = 0.0;
    float sure = 0.0f;
    float doubt = 0.0f;
    for (const Rgb &c : b1) {
        sure = std::max(sure, level(c));
    }
    for (const Rgb &c : render({p}, rig, doubted)) {
        doubt = std::max(doubt, level(c));
    }
    CHECK(doubt == doctest::Approx(0.5f * sure).epsilon(0.01));
    check_sane({p}, rig, note(35, 20.0, 0.05));
    CHECK(all_black(render({p}, default_rig(), note(35)))); // No rings.
}

// Cost per frame of each hexagon-visuals effect on a rig of about 1,500 pixels, Release
// (docs/PLAN_HEX_VISUALS.md budget 0.15 ms). Run by hand:
//   psyight_tests -tc="hexagon visuals: cost*" --no-skip
TEST_CASE("hexagon visuals: cost per frame on 1,500 pixels" * doctest::skip()) {
    // Five mandalas' worth of pixels: the preset with a fifth of the pitch, so five times the
    // pixels on the same shape.
    std::vector<StripSpec> strips;
    for (const RigPreset &preset : builtin_rig_presets()) {
        if (preset.name == "Hexagon mandala") {
            strips = preset.strips;
        }
    }
    const double factor = 1500.0 / static_cast<double>(mandala().pixels.size());
    for (StripSpec &s : strips) {
        s.pixels_per_m *= factor;
        s.count = static_cast<int>(std::lround(s.count * factor));
    }
    const Rig rig = build_rig(strips);
    MESSAGE("pixels: " << rig.pixels.size());
    const Palette palette{"test", {{1, 0, 0}, {0, 0, 1}}};
    for (const EffectType type : kEffectTypes) {
        if (static_cast<int>(type) < static_cast<int>(EffectType::pitch_clock)) {
            continue;
        }
        const auto effect = make_effect(default_spec(type));
        Frame out(rig.pixels.size());
        constexpr int kFrames = 2000;
        const auto start = std::chrono::steady_clock::now();
        for (int f = 0; f < kFrames; ++f) {
            EngineInputs in = note(35, 0.0, 0.01 * (f % 20));
            in.t_s = f / 60.0;
            in.phrase_phase = (f % 960) / 960.0;
            in.phrase_index = 3;
            in.kicks_so_far = 40 + f / 24;
            in.since_kick_s = (f % 24) / 60.0;
            in.hats_so_far = 80 + f / 12;
            in.since_hat_s = (f % 12) / 60.0;
            in.hat_ages_s = {in.since_hat_s, in.since_hat_s + 0.2, in.since_hat_s + 0.4,
                             in.since_hat_s + 0.6};
            effect->render({rig, in, palette, 7}, out);
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count() /
            kFrames;
        MESSAGE(std::string(effect_type_id(type)) << ": " << ms << " ms per frame");
    }
}

namespace {

size_t lit_count(const Frame &out) {
    return static_cast<size_t>(
        std::count_if(out.begin(), out.end(), [](const Rgb &c) { return level(c) > 0.01f; }));
}

} // namespace

TEST_CASE("note arcs: a note's arc is as long as it is held") {
    const Rig rig = mandala();
    const HexTopology t = hex_topology(rig);
    NoteArcsParams p;
    p.ring_mode = 2; // The outer ring, so the arcs are long enough to count.
    p.arc_per_16th = 0.25;
    // An ended note just after its offset: gate 0.5 draws half the arc of gate 1.0.
    EngineInputs ended = note(35, 0.0, 0.12);
    ended.bass_active = false;
    ended.since_bass_offset_s = 0.001;
    ended.bass_gate_ratio = 1.0;
    const size_t full = lit_count(render({p}, rig, ended));
    ended.bass_gate_ratio = 0.5;
    const size_t half = lit_count(render({p}, rig, ended));
    CHECK(full > 20);
    CHECK(static_cast<double>(half) == doctest::Approx(0.5 * full).epsilon(0.1));
    // A sounding note's arc grows with the time since its onset, on one ring only.
    size_t previous = 0;
    for (int k = 1; k <= 12; ++k) {
        const Frame out = render({p}, rig, note(35, 0.0, 0.025 * k));
        const size_t now = lit_count(out);
        CHECK(now >= previous);
        previous = now;
        CHECK(lit_strips(rig, out).size() <= 1);
    }
    // At the defaults, at most one ring is lit, never more than 40% of the rig.
    const NoteArcsParams defaults;
    for (int k = 0; k < 24; ++k) {
        EngineInputs in = note(28 + k % 12, 0.0, 0.05 * k);
        in.bass_notes_so_far = 100 + k;
        const Frame out = render({defaults}, rig, in);
        CHECK(lit_strips(rig, out).size() <= 1);
        CHECK(static_cast<double>(lit_count(out)) <= 0.4 * static_cast<double>(rig.pixels.size()));
    }
    // Cycle: successive notes on successive rings, starting at successive corners.
    NoteArcsParams cycle;
    EngineInputs a = note(35, 0.0, 0.1);
    a.bass_notes_so_far = 3; // n = 2: the outer ring.
    CHECK(lit_strips(rig, render({cycle}, rig, a)) == std::vector<int>{t.rings[2]});
    a.bass_notes_so_far = 4; // n = 3: the inner ring.
    CHECK(lit_strips(rig, render({cycle}, rig, a)) == std::vector<int>{t.rings[0]});
    // Released: fades out after the note.
    EngineInputs gone = ended;
    gone.since_bass_offset_s = 2.0;
    CHECK(lit_count(render({p}, rig, gone)) == 0);
    check_sane({p}, rig, note(35, 0.0, 0.3));
    check_sane({NoteArcsParams{}}, rig, note(35, 0.0, 0.6));
}

namespace {

// A kick `since_s` ago in a groove at 150 BPM.
EngineInputs kick(double since_s) {
    EngineInputs in = grid_at(40, since_s / 0.4);
    in.section = "groove";
    in.kicks_so_far = 41;
    in.since_kick_s = since_s;
    return in;
}

} // namespace

TEST_CASE("spoke kick: from the inner ends on the hit, out to the corners, easing past") {
    const Rig rig = mandala();
    const HexTopology t = hex_topology(rig);
    const SpokeKickParams p; // Travel half a beat (0.2 s), overshoot 0.1, hold 40 ms.
    const auto w_of = [&](size_t i) { return (t.radius[i] - t.u_min) / (1.0 - t.u_min); };
    std::vector<size_t> spoke_pixels;
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        if (t.ring_of_strip[static_cast<size_t>(rig.pixels[i].strip)] < 0) {
            spoke_pixels.push_back(i);
        }
    }
    // On the kick: the spokes' inner ends are lit, nothing beyond a tenth of the way out.
    const Frame hit = render({p}, rig, kick(0.0));
    bool inner_lit = false;
    for (const size_t i : spoke_pixels) {
        if (w_of(i) < 0.1) {
            inner_lit |= level(hit[i]) > 0.9f;
        } else {
            CHECK(level(hit[i]) == 0.0f);
        }
    }
    CHECK(inner_lit);
    for (size_t i = 0; i < rig.pixels.size(); ++i) { // Rings off: the rings stay dark.
        if (t.ring_of_strip[static_cast<size_t>(rig.pixels[i].strip)] >= 0) {
            CHECK(level(hit[i]) == 0.0f);
        }
    }
    // Past the corner and back: the head beyond 1 for x in 0.6 to 0.9, settling at 1.
    for (double x = 0.6; x <= 0.9; x += 0.1) {
        CHECK(overshoot_ease(x, 0.1) > 1.0);
        const Frame out = render({p}, rig, kick(x * 0.2));
        const size_t outer =
            *std::max_element(spoke_pixels.begin(), spoke_pixels.end(),
                              [&](size_t a, size_t b) { return t.radius[a] < t.radius[b]; });
        CHECK(level(out[outer]) > 0.3f);
    }
    CHECK(overshoot_ease(1.0, 0.1) == doctest::Approx(1.0));
    // Rings on: the middle ring flashes as the head passes it, not before.
    SpokeKickParams rings = p;
    rings.rings = 1;
    const size_t middle = pixels_of(rig, t.rings[1]).front();
    CHECK(level(render({rings}, rig, kick(0.0))[middle]) < 0.05f);
    float most = 0.0f;
    for (int k = 1; k <= 20; ++k) {
        most = std::max(most, level(render({rings}, rig, kick(0.01 * k))[middle]));
    }
    CHECK(most > 0.3f);
    // Kick, else beat: a groove with no kick for two beats still sends one on the beat.
    EngineInputs missed = kick(0.85);
    missed.beat_phase = 0.0;
    const auto brightest = [](const Frame &out) {
        float most = 0.0f;
        for (const Rgb &c : out) {
            most = std::max(most, level(c));
        }
        return most;
    };
    CHECK(brightest(render({p}, rig, missed)) > 0.9f);
    SpokeKickParams kick_only = p;
    kick_only.trigger = 0;
    CHECK(brightest(render({kick_only}, rig, missed)) < 0.1f); // The old kick, decayed.
    check_sane({p}, rig, kick(0.1));
    check_sane({rings}, rig, kick(0.15));
}

namespace {

// Hats every 8th at 150 BPM, the latest `since_s` ago.
EngineInputs hats(long long so_far, double since_s) {
    EngineInputs in = grid_at(40, 0.5);
    in.section = "groove";
    in.hats_so_far = so_far;
    in.since_hat_s = since_s;
    in.hat_ages_s = {since_s, since_s + 0.2, since_s + 0.4, since_s + 0.6};
    return in;
}

} // namespace

TEST_CASE("corner sparks: near the corners and spoke ends, seeded per hat") {
    const Rig rig = mandala();
    const HexTopology t = hex_topology(rig);
    CornerSparksParams p;
    p.keep = 1;
    // Sites: every ring corner pixel and every spoke's outer end.
    std::vector<size_t> sites;
    for (const int r : t.rings) {
        for (int k = 0; k < 6; ++k) {
            const std::vector<size_t> ring = pixels_of(rig, r);
            sites.push_back(*std::min_element(ring.begin(), ring.end(), [&](size_t a, size_t b) {
                return turn_distance(t.turn[a], corner_turn(t.frame, k)) <
                       turn_distance(t.turn[b], corner_turn(t.frame, k));
            }));
        }
    }
    for (const int s : t.spokes) {
        const std::vector<size_t> spoke = pixels_of(rig, s);
        sites.push_back(*std::max_element(spoke.begin(), spoke.end(), [&](size_t a, size_t b) {
            return t.radius[a] < t.radius[b];
        }));
    }
    // Every lit pixel is within `span` pixels along its strip of a site (rings wrap).
    for (long long e = 10; e < 40; ++e) {
        const Frame out = render({p}, rig, hats(e, 0.01));
        CHECK_FALSE(all_black(out));
        for (size_t i = 0; i < out.size(); ++i) {
            if (level(out[i]) == 0.0f) {
                continue;
            }
            bool near = false;
            for (const size_t s : sites) {
                const Pixel &a = rig.pixels[i];
                const Pixel &b = rig.pixels[s];
                if (a.strip != b.strip) {
                    continue;
                }
                const int n = rig.strips[static_cast<size_t>(a.strip)].count;
                int d = std::abs(a.index - b.index);
                if (t.ring_of_strip[static_cast<size_t>(a.strip)] >= 0) {
                    d = std::min(d, n - d);
                }
                near |= d <= p.span;
            }
            CHECK(near);
        }
    }
    // The same hat gives the same sites; another seed, others (over a few hats).
    CHECK(render({p}, rig, hats(17, 0.02)) == render({p}, rig, hats(17, 0.02)));
    int differ = 0;
    for (long long e = 10; e < 20; ++e) {
        differ += render({p}, rig, hats(e, 0.02), 7) != render({p}, rig, hats(e, 0.02), 8);
    }
    CHECK(differ >= 5);
    // Echoes: three hats drawn, the older dimmer.
    CornerSparksParams echoes;
    const Frame three = render({echoes}, rig, hats(30, 0.01));
    CHECK(lit_count(three) >= lit_count(render({p}, rig, hats(30, 0.01))));
    // Long after the last hat: dark.
    CHECK(all_black(render({p}, rig, hats(30, 2.0))));
    check_sane({echoes}, rig, hats(30, 0.03));
}

TEST_CASE("polar noise: seamless round the top, still without flow, never all dark") {
    const Rig rig = mandala();
    const HexTopology t = hex_topology(rig);
    const PolarNoiseParams p; // Rings 2, lobes 3.
    EngineInputs in = grid_at(40, 0.3);
    in.t_s = 16.12;
    const Frame out = render({p}, rig, in);
    // The outer ring's two pixels either side of the top differ by under 0.05.
    const std::vector<size_t> outer = pixels_of(rig, t.rings.back());
    const size_t right = *std::min_element(
        outer.begin(), outer.end(), [&](size_t a, size_t b) { return t.turn[a] < t.turn[b]; });
    const size_t left = *std::max_element(
        outer.begin(), outer.end(), [&](size_t a, size_t b) { return t.turn[a] < t.turn[b]; });
    CHECK(turn_distance(t.turn[left], t.turn[right]) < 0.03);
    CHECK(std::abs(level(out[left]) - level(out[right])) < 0.05f);
    // No flow on a free clock: it does not move.
    PolarNoiseParams still = p;
    still.flow = 0.0;
    still.clock = 0;
    EngineInputs later = in;
    later.t_s += 7.3;
    later.beat_index += 18;
    CHECK(render({still}, rig, in) == render({still}, rig, later));
    // It moves with flow.
    CHECK(render({p}, rig, in) != render({p}, rig, later));
    // Over 60 s, sampled every 0.5 s, the three rings are never all dark at once.
    for (int k = 0; k < 120; ++k) {
        EngineInputs at = grid_at(k * 5 / 4, 0.25 * (k % 4));
        at.t_s = 0.5 * k;
        const Frame frame = render({p}, rig, at);
        float ring_most = 0.0f;
        for (const int r : t.rings) {
            for (const size_t i : pixels_of(rig, r)) {
                ring_most = std::max(ring_most, level(frame[i]));
            }
        }
        CHECK(ring_most > 0.0f);
    }
    check_sane({p}, rig, in);
}

namespace {

EngineInputs in_phrase(double phase, int index = 2) {
    EngineInputs in = grid_at(80, 0.0);
    in.phrase_index = index;
    in.phrase_phase = phase;
    return in;
}

} // namespace

TEST_CASE("phrase fill: fills through the phrase, never unfills, clears on the line") {
    const Rig rig = mandala();
    for (int order = 0; order < 4; ++order) {
        CAPTURE(order);
        PhraseFillParams p;
        p.order = order;
        CHECK(all_black(render({p}, rig, in_phrase(0.0)))); // Nothing at the phrase's start.
        size_t previous = 0;
        for (int k = 0; k <= 200; ++k) {
            const size_t now = lit_count(render({p}, rig, in_phrase(0.97 * k / 200.0)));
            CHECK(now >= previous);
            previous = now;
        }
        p.clear_beats = 0.0;
        CHECK(lit_count(render({p}, rig, in_phrase(0.999))) == rig.pixels.size()); // All in.
        p.clear_beats = 0.5;
        for (const Rgb &c : render({p}, rig, in_phrase(1.0 - 1e-9))) { // Cleared on the line.
            CHECK(level(c) < 1e-3f);
        }
        check_sane({p}, rig, in_phrase(0.6));
    }
    // By side: the first side lit is on the inner ring; the spokes come last.
    const HexTopology t = hex_topology(rig);
    const PhraseFillParams sides;
    CHECK(lit_strips(rig, render({sides}, rig, in_phrase(1.5 / 32.0))) ==
          std::vector<int>{t.rings[0]});
    // Random pixels: the same seed gives the same order, another seed another.
    PhraseFillParams random;
    random.order = 3;
    CHECK(render({random}, rig, in_phrase(0.4), 7) == render({random}, rig, in_phrase(0.4), 7));
    CHECK(render({random}, rig, in_phrase(0.4), 7) != render({random}, rig, in_phrase(0.4), 8));
    // Before the first phrase it runs on bars in eights.
    EngineInputs early = grid_at(10, 0.5); // Bar 2, half way.
    early.phrase_index = -1;
    CHECK_FALSE(all_black(render({sides}, rig, early)));
}
