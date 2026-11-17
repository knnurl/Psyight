// The engine additions for the Cagan piece (docs/PLAN_CAGAN_PACK.md section 3): radial per strip
// (engine/space.h strip_radial, Wave's "Radial from"), Wave's turn offset and the Arm notes
// effect, on a small flower rig built here (twelve straight arms round a centre, half wired
// inwards, inside a closed ring).

#include "effect_test_helpers.h"
#include "engine/space.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace psyight;
using test::grid_at;
using test::level;
using test::pixels_of;
using test::render;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kArmPixels = 30;

// Twelve arms from 5 cm to 50 cm at 30 degree steps from the top, clockwise; odd arms wired from
// the outer end in. Tagged "arms". A ring of 80 cm round them sets the bounding box's centre.
Rig flower() {
    std::vector<StripSpec> strips;
    for (int k = 0; k < 12; ++k) {
        const double a = 2.0 * kPi * k / 12.0;
        const Vec3 inner{0.05 * std::sin(a), 0.05 * std::cos(a), 0.0};
        const Vec3 outer{0.5 * std::sin(a), 0.5 * std::cos(a), 0.0};
        StripSpec s;
        s.name = "arm";
        s.polyline = k % 2 == 0 ? std::vector<Vec3>{inner, outer} : std::vector<Vec3>{outer, inner};
        s.pixels_per_m = kArmPixels / 0.45 + 1e-7; // Exactly full: both wirings place alike.
        s.count = kArmPixels;
        s.fills = false;
        s.groups = {"arms"};
        strips.push_back(s);
    }
    StripSpec ring;
    ring.name = "ring";
    for (int k = 0; k <= 36; ++k) {
        const double a = 2.0 * kPi * k / 36.0;
        ring.polyline.push_back({0.8 * std::sin(a), 0.8 * std::cos(a), 0.0});
    }
    ring.count = 150; // Exactly full, so the bounding box is centred on the origin.
    ring.pixels_per_m = ring.count / (36 * 2.0 * 0.8 * std::sin(kPi / 36.0)) + 1e-7;
    ring.fills = false;
    strips.push_back(ring);
    return build_rig(strips);
}

// A bass note at 150 BPM (16th 0.1 s): sounding `since_s` after its onset, or ended.
EngineInputs bass(int midi, double gate_ratio, double since_s, bool sounding) {
    EngineInputs in = grid_at(40, 0.25);
    in.section = "groove";
    in.bass_notes_so_far = 100;
    in.bass_active = sounding;
    in.bass_midi = midi;
    in.bass_velocity = 1.0;
    in.bass_confidence = 1.0;
    in.bass_decay_tau_ms = 100.0;
    in.bass_gate_ratio = gate_ratio;
    in.since_bass_onset_s = since_s;
    in.since_bass_offset_s = sounding ? 0.0 : std::max(0.0, since_s - gate_ratio * 0.1);
    in.notes[0].age_s = since_s;
    in.notes[0].midi = midi;
    in.notes[0].sounding = sounding;
    in.notes[0].length_s = sounding ? since_s : gate_ratio * 0.1;
    for (size_t k = 1; k < in.notes.size(); ++k) {
        in.notes[k].age_s = since_s + 0.25 * static_cast<double>(k);
        in.notes[k].midi = midi;
        in.notes[k].length_s = 0.08;
        in.notes[k].velocity = 1.0;
        in.notes[k].confidence = 1.0;
        in.notes[k].decay_tau_ms = 100.0;
    }
    return in;
}

// The arm strips (0 to 11, by turn from the top) with a lit pixel.
std::vector<int> lit_arms(const Rig &rig, const Frame &out) {
    std::vector<int> arms;
    for (size_t i = 0; i < out.size(); ++i) {
        const int s = rig.pixels[i].strip;
        if (s < 12 && level(out[i]) > 0.01f &&
            std::find(arms.begin(), arms.end(), s) == arms.end()) {
            arms.push_back(s);
        }
    }
    std::sort(arms.begin(), arms.end());
    return arms;
}

// How far out an arm is lit: the farthest lit pixel's place from its innermost pixel (0) to its
// outermost (1), by distance from the centre (the origin).
double lit_reach(const Rig &rig, const Frame &out, int strip) {
    const auto d = [&](size_t i) {
        return std::hypot(rig.pixels[i].position.x, rig.pixels[i].position.y);
    };
    const std::vector<size_t> px = pixels_of(rig, strip);
    double lo = 1e9;
    double hi = 0.0;
    for (const size_t i : px) {
        lo = std::min(lo, d(i));
        hi = std::max(hi, d(i));
    }
    double reach = -1.0;
    for (const size_t i : px) {
        if (level(out[i]) > 0.5f) {
            reach = std::max(reach, (d(i) - lo) / (hi - lo));
        }
    }
    return reach;
}

} // namespace

TEST_CASE("space: radial, per strip runs from each strip's centroid to its farthest pixel") {
    // Two rings of one strip, as a dome: 12 pixels at 3 cm and 24 at 9 cm, about (1, 2).
    Rig rig;
    rig.strips.resize(2);
    rig.strips[0].count = 36;
    for (int k = 0; k < 36; ++k) {
        const bool inner = k < 12;
        const double a = 2.0 * kPi * (inner ? k / 12.0 : (k - 12) / 24.0);
        const double r = inner ? 0.03 : 0.09;
        rig.pixels.push_back({{1.0 + r * std::sin(a), 2.0 + r * std::cos(a), 0.0}, 0, k});
    }
    // A second strip far away: a line, 0 in its middle.
    rig.strips[1].count = 11;
    for (int k = 0; k < 11; ++k) {
        rig.pixels.push_back({{-1.0 + 0.01 * k, 0.0, 0.0}, 1, k});
    }
    rig.min = {-1.0, 0.0, 0.0};
    rig.max = {1.09, 2.09, 0.0};
    const std::vector<double> u = strip_radial(rig);
    for (int k = 0; k < 12; ++k) {
        CHECK(u[static_cast<size_t>(k)] == doctest::Approx(1.0 / 3.0).epsilon(1e-9));
    }
    for (int k = 12; k < 36; ++k) {
        CHECK(u[static_cast<size_t>(k)] == doctest::Approx(1.0).epsilon(1e-9));
    }
    CHECK(u[36 + 5] == doctest::Approx(0.0).epsilon(1e-9));
    CHECK(u[36] == doctest::Approx(1.0).epsilon(1e-9));
    CHECK(u[36 + 10] == doctest::Approx(1.0).epsilon(1e-9));
}

TEST_CASE("wave: radial from each strip's centre, off by default") {
    const Rig rig = flower();
    WaveParams p;
    p.mapping = static_cast<int>(Mapping::radial);
    p.shape = 2; // Ramp: the level is the phase, so it shows u.
    p.clock = 0;
    p.rate = 0.0;
    p.spread = 0.999;
    p.colour_spread = 0.0;
    p.level = 1.0;
    const EngineInputs in = grid_at(40, 0.0);
    WaveParams centre = p;
    centre.radial_from = 0;
    CHECK(render({centre}, rig, in) == render({p}, rig, in));
    WaveParams own = p;
    own.radial_from = 1;
    const Frame out = render({own}, rig, in);
    // Every arm alike: dark in its middle, bright at both ends.
    for (int s = 0; s < 12; ++s) {
        const std::vector<size_t> px = pixels_of(rig, s);
        CHECK(level(out[px.front()]) == doctest::Approx(0.999).epsilon(1e-3));
        CHECK(level(out[px.back()]) == doctest::Approx(0.999).epsilon(1e-3));
        CHECK(level(out[px[px.size() / 2]]) < 0.05f);
    }
    const std::vector<EffectParamInfo> infos = effect_param_infos(EffectType::wave);
    const auto from = std::find_if(infos.begin(), infos.end(), [](const EffectParamInfo &i) {
        return std::string(i.id) == "radial_from";
    });
    REQUIRE(from != infos.end());
    CHECK(effect_option_name(*from, 1) == "Each strip's centre");
}

TEST_CASE("wave: turn offset, off by default, steps the strips round the centre") {
    const Rig rig = flower();
    WaveParams p;
    p.mapping = static_cast<int>(Mapping::by_strip);
    p.spread = 0.0; // Every pixel the same phase but for the offset.
    p.shape = 0;
    p.colour_spread = 0.0;
    const EngineInputs in = grid_at(40, 0.0); // A bar line: the top arm at the trough.
    const Frame base = render({p}, rig, in);
    WaveParams zero = p;
    zero.turn_offset = 0.0;
    CHECK(render({zero}, rig, in) == base);
    // One cycle per turn: the arm at the top and the one opposite are half a cycle apart.
    WaveParams turning = p;
    turning.turn_offset = 1.0;
    const Frame out = render({turning}, rig, in);
    const float top = level(out[pixels_of(rig, 0)[10]]);
    const float bottom = level(out[pixels_of(rig, 6)[10]]);
    const float side = level(out[pixels_of(rig, 3)[10]]);
    CHECK(std::abs(top - bottom) > 0.5f);
    CHECK(side == doctest::Approx(0.5f * (top + bottom)).epsilon(0.02));
    // Every pixel of an arm shares its phase.
    for (const size_t i : pixels_of(rig, 3)) {
        CHECK(level(out[i]) == doctest::Approx(side));
    }
}

TEST_CASE("arm notes: pitch picks the arm, gate ratio the reach, wiring does not matter") {
    const Rig rig = flower();
    ArmNotesParams p;
    p.confidence = 0.0;
    // Pitch class: C on arm 0 (the top), D on arm 2, B on arm 11.
    CHECK(lit_arms(rig, render({p}, rig, bass(36, 1.0, 0.2, false))) == std::vector<int>{0});
    CHECK(lit_arms(rig, render({p}, rig, bass(38, 1.0, 0.2, false))) == std::vector<int>{2});
    CHECK(lit_arms(rig, render({p}, rig, bass(47, 1.0, 0.2, false))) == std::vector<int>{11});
    ArmNotesParams top = p;
    top.top_pitch = 2;
    CHECK(lit_arms(rig, render({top}, rig, bass(38, 1.0, 0.2, false))) == std::vector<int>{0});
    // Reach: the gate ratio of the arm, within one pixel, just after the note ends; on an arm
    // wired outwards (C, arm 0) and one wired inwards (C sharp, arm 1).
    const double pixel = 1.0 / (kArmPixels - 1);
    for (const double gate : {0.25, 0.5, 0.75}) {
        CAPTURE(gate);
        const Frame out = render({p}, rig, bass(36, gate, gate * 0.1 + 0.001, false));
        CHECK(std::abs(lit_reach(rig, out, 0) - gate) <= pixel);
        const Frame in_wired = render({p}, rig, bass(37, gate, gate * 0.1 + 0.001, false));
        CHECK(std::abs(lit_reach(rig, in_wired, 1) - gate) <= pixel);
        // The same picture, pixel for pixel by distance from the centre.
        const std::vector<size_t> a = pixels_of(rig, 0);
        std::vector<size_t> b = pixels_of(rig, 1);
        std::reverse(b.begin(), b.end());
        for (size_t k = 0; k < a.size(); ++k) {
            CHECK(level(out[a[k]]) == doctest::Approx(level(in_wired[b[k]])).epsilon(1e-6));
        }
    }
    // While it sounds, the head travels out a 16th at a time.
    const Frame half = render({p}, rig, bass(36, 1.0, 0.05, true));
    CHECK(std::abs(lit_reach(rig, half, 0) - 0.5) <= pixel);
    // All arms.
    ArmNotesParams all = p;
    all.arm_mode = 2;
    CHECK(lit_arms(rig, render({all}, rig, bass(36, 1.0, 0.2, false))).size() == 12);
}

TEST_CASE("arm notes: a slide lights two arms, a rest leaves the arms dark") {
    const Rig rig = flower();
    ArmNotesParams p;
    p.confidence = 0.0;
    EngineInputs slide = bass(40, 1.0, 0.05, true); // E, sliding from C.
    slide.bass_slide = true;
    slide.notes[1].midi = 36;
    CHECK(lit_arms(rig, render({p}, rig, slide)) == std::vector<int>{0, 4});
    slide.since_bass_onset_s = 0.1; // The slide done: only E.
    CHECK(lit_arms(rig, render({p}, rig, slide)) == std::vector<int>{4});
    // A rest: two seconds after a note ended, nothing lit.
    const Frame rest = render({p}, rig, bass(36, 1.0, 2.1, false));
    CHECK(std::all_of(rest.begin(), rest.end(), [](const Rgb &c) { return level(c) <= 0.01f; }));
    // Before any note, black.
    EngineInputs none = grid_at(4, 0.0);
    const Frame dark = render({p}, rig, none);
    CHECK(std::all_of(dark.begin(), dark.end(), [](const Rgb &c) { return level(c) == 0.0f; }));
}

TEST_CASE("arm notes: each note in turn, arms a note, palette by pitch") {
    const Rig rig = flower();
    ArmNotesParams p;
    p.confidence = 0.0;
    p.arm_mode = 3;
    p.step = 5;
    // The same pitch over and over still moves on: note 100 (the 100th) on arm 99 % 12 * 5 % 12.
    EngineInputs in = bass(36, 1.0, 0.2, false);
    CHECK(lit_arms(rig, render({p}, rig, in)) == std::vector<int>{3});
    in.bass_notes_so_far = 101;
    CHECK(lit_arms(rig, render({p}, rig, in)) == std::vector<int>{8});
    // Twelve notes in a row at step 5 visit every arm once.
    std::vector<int> seen;
    for (long long n = 1; n <= 12; ++n) {
        in.bass_notes_so_far = n;
        const std::vector<int> lit = lit_arms(rig, render({p}, rig, in));
        REQUIRE(lit.size() == 1);
        seen.push_back(lit[0]);
    }
    std::sort(seen.begin(), seen.end());
    CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    // Three arms a note, evenly round.
    ArmNotesParams three = p;
    three.arms_per_note = 3;
    in.bass_notes_so_far = 1;
    CHECK(lit_arms(rig, render({three}, rig, in)) == std::vector<int>{0, 4, 8});
    // Palette by pitch: the colour follows the pitch, not the arm.
    ArmNotesParams by_pitch = p;
    by_pitch.colours = 2;
    in.bass_notes_so_far = 1;
    const Frame c1 = render({by_pitch}, rig, in);
    in.bass_notes_so_far = 2;
    const Frame c2 = render({by_pitch}, rig, in);
    const auto lit_colour = [&](const Frame &f) { // The brightest pixel's.
        Rgb best{};
        for (const Rgb &c : f) {
            if (level(c) > level(best)) {
                best = c;
            }
        }
        return best;
    };
    CHECK(lit_colour(c1) == lit_colour(c2)); // Same pitch on another arm: the same colour.
    in.bass_midi = 42;
    CHECK(!(lit_colour(render({by_pitch}, rig, in)) == lit_colour(c2)));
    // The defaults stay: pitch class, one arm, pitch hue.
    const ArmNotesParams d;
    CHECK(d.arm_mode == 0);
    CHECK(d.arms_per_note == 1);
    CHECK(d.colours == 0);
}

TEST_CASE("arm notes: scale step, trail, and the same frame twice") {
    const Rig rig = flower();
    ArmNotesParams p;
    p.confidence = 0.0;
    p.arm_mode = 1;
    // History E, G, A (40, 43, 45): ranks 0, 1, 2 spread round 12 arms as 0, 4 and 8.
    EngineInputs in = bass(43, 1.0, 0.2, false);
    for (size_t k = 1; k < in.notes.size(); ++k) {
        in.notes[k].midi = k % 3 == 0 ? 40 : (k % 3 == 1 ? 43 : 45);
    }
    CHECK(lit_arms(rig, render({p}, rig, in)) == std::vector<int>{4});
    in.bass_midi = 45;
    CHECK(lit_arms(rig, render({p}, rig, in)) == std::vector<int>{8});
    // Trail: two earlier notes stay on their arms, dimmer.
    ArmNotesParams trail;
    trail.confidence = 0.0;
    trail.trail = 2;
    EngineInputs t = bass(36, 1.0, 0.11, false);
    t.notes[1].midi = 38;
    t.notes[1].age_s = 0.35;
    t.notes[2].midi = 41;
    t.notes[2].age_s = 0.6;
    const Frame out = render({trail}, rig, t);
    CHECK(lit_arms(rig, out) == std::vector<int>{0, 2, 5});
    // Their innermost pixels (arm 5 is wired inwards): the older dimmer.
    CHECK(level(out[pixels_of(rig, 2).front()]) > level(out[pixels_of(rig, 5).back()]));
    CHECK(level(out[pixels_of(rig, 5).back()]) > 0.01f);
    CHECK(render({trail}, rig, t) == out);
}
