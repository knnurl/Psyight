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
