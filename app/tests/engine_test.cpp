// Engine core: rig, colour, inputs, effects and scene rendering.

#include "engine/colour.h"
#include "engine/inputs.h"
#include "engine/rig.h"
#include "engine/scene.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;

bool near_colour(Rgb a, Rgb b, float tolerance = 1e-5f) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

float luminance(Rgb c) { return c.r + c.g + c.b; }

// 150 BPM from 1.0 s: beats every 0.4 s, 16ths every 0.1 s. Kicks on every beat; bass notes on
// slots 1 and 2 of the first beat.
TrackAnalysis small_analysis() {
    TrackAnalysis a;
    a.sample_rate_hz = kSampleRateHz;
    a.duration_s = 20.0;
    a.grid = {150.0, seconds_to_samples(1.0, kSampleRateHz), kSampleRateHz};
    for (int k = 0; k < 40; ++k) {
        a.bass.kick_times_s.push_back(1.0 + 0.4 * k);
    }
    BassNote n1;
    n1.onset_s = 1.1;
    n1.offset_s = 1.19;
    n1.pitch_midi = 28;
    n1.gate_ratio = 0.9;
    n1.velocity = 1.0;
    n1.decay_tau_ms = 200.0;
    BassNote n2 = n1;
    n2.onset_s = 1.2;
    n2.offset_s = 1.25;
    n2.pitch_midi = 31;
    n2.gate_ratio = 0.5;
    a.bass.notes = {n1, n2};
    a.sections = {{0.0, 1.0, "intro", 1.0}, {1.0, 20.0, "groove", 1.0}};
    a.phrase_starts_s = {1.0, 13.8};
    a.residual.hats = {{1.2, 1.0}, {1.6, 1.0}};
    return a;
}

} // namespace

TEST_CASE("rig: strips laid along polylines, half a pitch in, with a bounding box") {
    const Rig rig = default_rig();
    CHECK(rig.pixels.size() == 444);
    CHECK(rig.pixels[0].position.x == doctest::Approx(0.5 / 60.0));
    CHECK(rig.pixels[0].position.y == 0.0);
    CHECK(rig.pixels[149].position.x == doctest::Approx(2.5 - 0.5 / 60.0));
    CHECK(rig.pixels[150].strip == 1);
    CHECK(rig.pixels[150].index == 0);
    CHECK(rig.min.x == doctest::Approx(0.0).epsilon(0.01));
    CHECK(rig.max.y == doctest::Approx(1.2).epsilon(0.01));
    // A strip longer than its polyline continues along the last segment.
    const Rig longer = build_rig({{"long", {{0, 0, 0}, {1, 0, 0}}, 10.0, 15, {}}});
    CHECK(longer.pixels.back().position.x == doctest::Approx(1.45));
}

TEST_CASE("colour: blend modes, hue wrapping and palette interpolation") {
    const Rgb dst{0.5f, 0.2f, 0.0f};
    const Rgb src{0.4f, 0.4f, 1.0f};
    CHECK(near_colour(blend(dst, src, BlendMode::add, 1.0f), {0.9f, 0.6f, 1.0f}));
    CHECK(near_colour(blend({0.8f, 0, 0}, {0.8f, 0, 0}, BlendMode::add, 1.0f), {1.0f, 0, 0}));
    CHECK(near_colour(blend(dst, src, BlendMode::max, 1.0f), {0.5f, 0.4f, 1.0f}));
    CHECK(near_colour(blend(dst, src, BlendMode::alpha, 0.5f), {0.45f, 0.3f, 0.5f}));
    CHECK(near_colour(blend(dst, src, BlendMode::multiply, 1.0f), {0.2f, 0.08f, 0.0f}));
    CHECK(near_colour(blend(dst, src, BlendMode::screen, 1.0f), {0.7f, 0.52f, 1.0f}));
    CHECK(near_colour(blend(dst, src, BlendMode::screen, 0.0f), dst));
    CHECK(near_colour(hsv(0.0, 1.0, 1.0), {1, 0, 0}));
    CHECK(near_colour(hsv(120.0, 1.0, 1.0), {0, 1, 0}));
    CHECK(near_colour(hsv(-120.0, 1.0, 1.0), {0, 0, 1}));
    CHECK(near_colour(hsv(0.0, 0.0, 0.5), {0.5f, 0.5f, 0.5f}));
    const Palette p{"test", {{1, 0, 0}, {0, 1, 0}}};
    CHECK(near_colour(p.at(0.0), {1, 0, 0}));
    CHECK(near_colour(p.at(0.25), {0.5f, 0.5f, 0}));
    CHECK(near_colour(p.at(1.5), {0, 1, 0})); // Wrapped.
}

TEST_CASE("engine inputs: grid phases, kicks, the sounding bass note, section and hats") {
    const TrackAnalysis a = small_analysis();
    const EngineInputs in = inputs_at(a, a.grid, 1.15);
    CHECK(in.beat_index == 0);
    CHECK(in.beat_phase == doctest::Approx(0.375));
    CHECK(in.slot_phase == doctest::Approx(0.5));
    CHECK(in.bar_index == 0);
    CHECK(in.since_kick_s == doctest::Approx(0.15));
    CHECK(in.bass_active);
    CHECK(in.bass_midi == 28);
    CHECK(in.bass_notes_so_far == 1);
    CHECK(in.bass_note_phase == doctest::Approx(0.05 / 0.09));
    CHECK(in.section == "groove");
    CHECK(in.phrase_index == 0);
    CHECK(in.since_hat_s == kNever);

    const EngineInputs later = inputs_at(a, a.grid, 1.27);
    CHECK_FALSE(later.bass_active);
    CHECK(later.bass_midi == 31);
    CHECK(later.bass_notes_so_far == 2);
    CHECK(later.since_bass_offset_s == doctest::Approx(0.02));
    CHECK(later.since_hat_s == doctest::Approx(0.07));
    CHECK(later.hats_so_far == 1);

    const EngineInputs before = inputs_at(a, a.grid, 0.5);
    CHECK(before.section == "intro");
    CHECK(before.since_kick_s == kNever);
    CHECK(before.bass_notes_so_far == 0);
    CHECK(before.beat_index == -2);
    CHECK(before.beat_in_bar == 2);
}

TEST_CASE("shockwave: a ring at speed times time since the kick, nothing before a kick") {
    const Rig rig = default_rig();
    const Palette palette = default_palettes()[0];
    EngineInputs in;
    std::vector<Rgb> out(rig.pixels.size());
    const auto effect = make_shockwave({6.0, 0.25, 0.25});
    effect->render({rig, in, palette, 1}, out);
    for (const Rgb &c : out) {
        CHECK(luminance(c) == 0.0f);
    }
    in.since_kick_s = 0.1; // Radius 0.6 m from the frame's centre (1.25, 0.6).
    effect->render({rig, in, palette, 1}, out);
    // The bottom strip's pixel under the centre is 0.6 m away: on the ring. Its far corner is not.
    size_t under_centre = 0;
    for (size_t i = 0; i < 150; ++i) {
        if (std::abs(rig.pixels[i].position.x - 1.25) <
            std::abs(rig.pixels[under_centre].position.x - 1.25)) {
            under_centre = i;
        }
    }
    CHECK(luminance(out[under_centre]) > 2.0f * 0.6f);
    CHECK(luminance(out[0]) < 0.01f);
}

TEST_CASE("conveyor: lit only while a note sounds; the pattern steps per note; gate sets length") {
    const Rig rig = default_rig();
    const Palette palette = default_palettes()[0];
    const auto effect = make_conveyor({12, 5, 28.0, 270.0, 0.03});
    std::vector<Rgb> out(rig.pixels.size());
    EngineInputs in;
    effect->render({rig, in, palette, 1}, out);
    for (const Rgb &c : out) {
        CHECK(luminance(c) == 0.0f); // No notes yet.
    }
    in.since_bass_onset_s = 0.01;
    in.bass_active = true;
    in.bass_velocity = 1.0;
    in.bass_decay_tau_ms = 200.0;
    in.bass_midi = 28;
    in.bass_gate_ratio = 1.0;
    in.bass_notes_so_far = 1;
    effect->render({rig, in, palette, 1}, out);
    std::vector<bool> first;
    int lit = 0;
    for (size_t i = 0; i < 150; ++i) {
        first.push_back(luminance(out[i]) > 0.0f);
        lit += first.back();
    }
    // Gate 1.0 lights one 12-pixel segment in each 60: 24 to 36 of 150 pixels (2.5 periods).
    CHECK(lit >= 24);
    CHECK(lit <= 36);
    in.bass_notes_so_far = 2;
    effect->render({rig, in, palette, 1}, out);
    int newly_lit = 0;
    for (size_t i = 0; i + 12 < 150; ++i) {
        CHECK((luminance(out[i]) > 0.0f) == first[i + 12]); // One step: one segment.
        newly_lit += luminance(out[i]) > 0.0f && !first[i];
    }
    CHECK(newly_lit < 150 / 4); // A step never lights a quarter of the strip.
    in.bass_gate_ratio = 0.5;
    effect->render({rig, in, palette, 1}, out);
    lit = 0;
    for (size_t i = 0; i < 150; ++i) {
        lit += luminance(out[i]) > 0.0f;
    }
    CHECK(lit < 24);
    in.bass_active = false;
    in.since_bass_offset_s = 0.3; // Ten release time constants later.
    effect->render({rig, in, palette, 1}, out);
    float total = 0.0f;
    for (const Rgb &c : out) {
        total += luminance(c);
    }
    CHECK(total < 0.01f);
}

TEST_CASE("sparkle and shimmer: hats light a seeded few; the residual lights only rests") {
    const Rig rig = default_rig();
    const Palette palette = default_palettes()[0];
    std::vector<Rgb> out(rig.pixels.size());
    EngineInputs in;
    in.since_hat_s = 0.0;
    in.hats_so_far = 5;
    make_sparkle({0.1, 0.06})->render({rig, in, palette, 3}, out);
    int lit = 0;
    for (const Rgb &c : out) {
        lit += luminance(c) > 0.0f;
    }
    CHECK(lit > 20);
    CHECK(lit < 70); // About 10% of 444.

    in.residual_level = 0.8;
    in.residual_centroid_hz = 2000.0;
    in.bass_active = true;
    const auto shimmer = make_shimmer();
    shimmer->render({rig, in, palette, 3}, out);
    float total = 0.0f;
    for (const Rgb &c : out) {
        total += luminance(c);
    }
    CHECK(total == 0.0f);
    in.bass_active = false;
    in.since_bass_offset_s = 0.0; // Fades in after the note ends.
    shimmer->render({rig, in, palette, 3}, out);
    total = 0.0f;
    for (const Rgb &c : out) {
        total += luminance(c);
    }
    CHECK(total == 0.0f);
    in.since_bass_offset_s = 0.1;
    shimmer->render({rig, in, palette, 3}, out);
    total = 0.0f;
    lit = 0;
    for (const Rgb &c : out) {
        total += luminance(c);
        lit += luminance(c) > 0.0f;
    }
    CHECK(total > 2.0f);
    CHECK(lit < 444 / 4); // A sparse subset, never a quarter of the field.
}

TEST_CASE("scene: rendering is a pure function of the inputs and seed") {
    const Rig rig = default_rig();
    const std::vector<Palette> palettes = default_palettes();
    const TrackAnalysis a = small_analysis();
    const Scene scene = default_scene();
    const EngineInputs in = inputs_at(a, a.grid, 1.65);
    const Frame f1 = render_scene(scene, rig, in, palettes, 42);
    const Frame f2 = render_scene(scene, rig, in, palettes, 42);
    REQUIRE(f1.size() == rig.pixels.size());
    CHECK(f1 == f2);
    const Frame f3 = render_scene(scene, rig, in, palettes, 43);
    CHECK_FALSE(f1 == f3); // The sparkle's pixels follow the seed.
    for (const Rgb &c : f1) {
        CHECK(c.r >= 0.0f);
        CHECK(c.r <= 1.0f);
        CHECK(c.b <= 1.0f);
    }
}
