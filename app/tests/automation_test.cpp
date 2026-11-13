// Scene automation, the scene library and the build and drop effects.

#include "analysis/intensity.h"
#include "engine/automation.h"
#include "engine/effect_spec.h"
#include "engine/inputs.h"
#include "engine/presets.h"
#include "engine/rig.h"
#include "engine/scene.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;
constexpr double kBeatS = 0.4; // 150 BPM.
constexpr double kPhraseS = 12.8;

// Default library indices.
constexpr int kGroove = 0;
constexpr int kStaccato = 1;
constexpr int kDrop = 2;
constexpr int kAmbient = 3;
constexpr int kBuild = 4;

TrackAnalysis arrangement() {
    TrackAnalysis a;
    a.sample_rate_hz = kSampleRateHz;
    a.duration_s = 110.0;
    a.grid = {150.0, 0, kSampleRateHz};
    a.sections = {{0.0, 12.8, "intro", 1.0},      {12.8, 38.4, "groove", 1.0},
                  {38.4, 51.2, "breakdown", 1.0}, {51.2, 57.6, "build", 1.0},
                  {57.6, 83.2, "drop", 1.0},      {83.2, 100.0, "groove", 1.0},
                  {100.0, 110.0, "outro", 1.0}};
    for (double t = 0.0; t < 110.0; t += kPhraseS) {
        a.phrase_starts_s.push_back(t);
    }
    for (double t = 12.8; t < 38.4; t += kBeatS) {
        a.bass.kick_times_s.push_back(t);
    }
    return a;
}

SceneMix mix_at(const std::vector<Scene> &scenes, const TrackAnalysis &a, double t_s,
                int manual = -1) {
    const EngineInputs in = inputs_at(a, a.grid, t_s);
    return automate(scenes, a, a.grid, in, AutomationParams{}, manual);
}

float max_difference(const Frame &a, const Frame &b) {
    float most = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        most = std::max({most, std::abs(a[i].r - b[i].r), std::abs(a[i].g - b[i].g),
                         std::abs(a[i].b - b[i].b)});
    }
    return most;
}

} // namespace

TEST_CASE("automation: each section takes its tagged scene, in rotation") {
    const std::vector<Scene> scenes = default_scenes();
    REQUIRE(scenes.size() == 5);
    CHECK(scenes[kBuild].name == "Build");
    const TrackAnalysis a = arrangement();
    const std::vector<int> expected = {kAmbient, kGroove,   kAmbient, kBuild,
                                       kDrop,    kStaccato, kAmbient};
    for (size_t i = 0; i < a.sections.size(); ++i) {
        CAPTURE(i);
        CHECK(scene_for_section(scenes, a.sections, i) == expected[i]);
    }
    // No drop scene: drops fall back to the groove scenes.
    std::vector<Scene> no_drop = scenes;
    no_drop.erase(no_drop.begin() + kDrop);
    CHECK(no_drop[static_cast<size_t>(scene_for_section(no_drop, a.sections, 4))].name ==
          "Groove"); // Rotation counts drops: the first drop takes the first groove scene.
}

TEST_CASE("automation: cuts into grooves and drops, crossfades into the rest, manual wins") {
    const std::vector<Scene> scenes = default_scenes();
    const TrackAnalysis a = arrangement();
    SceneMix m = mix_at(scenes, a, 12.9);
    CHECK(m.from == -1);
    CHECK(m.to == kGroove);
    m = mix_at(scenes, a, 38.4 + 2 * kBeatS); // Halfway through the 4-beat crossfade.
    CHECK(m.from == kGroove);
    CHECK(m.to == kAmbient);
    CHECK(m.to_weight == doctest::Approx(0.5));
    m = mix_at(scenes, a, 38.4 + 5 * kBeatS);
    CHECK(m.from == -1);
    CHECK(m.to == kAmbient);
    m = mix_at(scenes, a, 51.2 + kBeatS);
    CHECK(m.from == kAmbient);
    CHECK(m.to == kBuild);
    m = mix_at(scenes, a, 57.6 + 0.01);
    CHECK(m.from == -1);
    CHECK(m.to == kDrop);
    m = mix_at(scenes, a, 57.6 + 0.01, kAmbient);
    CHECK(m.from == -1);
    CHECK(m.to == kAmbient);
}

TEST_CASE("automation: the palette shifts every two phrases, slewed over four beats") {
    const std::vector<Scene> scenes = default_scenes();
    const TrackAnalysis a = arrangement();
    SceneMix m = mix_at(scenes, a, kPhraseS + 1.0); // Phrase 1.
    CHECK(m.palette_shift == 0);
    CHECK(m.palette_weight == 1.0);
    m = mix_at(scenes, a, 2 * kPhraseS + 2 * kBeatS); // Phrase 2: halfway through the slew.
    CHECK(m.palette_shift == 1);
    CHECK(m.palette_shift_from == 0);
    CHECK(m.palette_weight == doctest::Approx(0.5));
    m = mix_at(scenes, a, 2 * kPhraseS + 5 * kBeatS);
    CHECK(m.palette_weight == 1.0);
    CHECK(mix_at(scenes, a, 4 * kPhraseS + 5 * kBeatS).palette_shift == 2);
}

TEST_CASE("automation: render_mix crossfades linearly between the scenes") {
    const std::vector<Scene> scenes = default_scenes();
    const std::vector<Palette> palettes = default_palettes();
    const TrackAnalysis a = arrangement();
    const Rig rig = default_rig();
    const EngineInputs in = inputs_at(a, a.grid, 20.05);
    const Frame groove = render_scene(scenes[kGroove], rig, in, palettes, 1);
    const Frame ambient = render_scene(scenes[kAmbient], rig, in, palettes, 1);
    SceneMix m;
    m.from = kGroove;
    m.to = kAmbient;
    m.to_weight = 0.0;
    CHECK(max_difference(render_mix(scenes, m, rig, in, palettes, 1), groove) < 1e-6f);
    m.to_weight = 1.0;
    CHECK(max_difference(render_mix(scenes, m, rig, in, palettes, 1), ambient) < 1e-6f);
    m.to_weight = 0.25;
    const Frame quarter = render_mix(scenes, m, rig, in, palettes, 1);
    for (size_t i = 0; i < rig.pixels.size(); i += 37) {
        CHECK(quarter[i].r == doctest::Approx(0.75f * groove[i].r + 0.25f * ambient[i].r));
    }
    REQUIRE(max_difference(groove, ambient) > 0.05f); // The scenes do differ.
}

TEST_CASE("build compression: continuous, contracting, brightening; silent outside builds") {
    // One strip from the centre outward (the rig's centre is x = 1).
    const Rig rig = build_rig({{"s", {{0, 0, 0}, {2, 0, 0}}, 100.0, 200, {}}});
    const std::vector<Palette> palettes = default_palettes();
    Scene scene;
    scene.layers.push_back({make_build(), BlendMode::add, 1.0f, true});
    EngineInputs in;
    in.section = "build";
    in.section_length_s = 6.4;
    const auto at = [&](double since_s) {
        in.since_section_s = since_s;
        in.section_progress = since_s / in.section_length_s;
        in.t_s = 51.2 + since_s;
        return render_scene(scene, rig, in, palettes, 1);
    };
    float largest_step = 0.0f;
    Frame previous = at(0.0);
    for (double s = 0.001; s < 6.4; s += 0.001) {
        const Frame f = at(s);
        largest_step = std::max(largest_step, max_difference(previous, f));
        previous = f;
    }
    CHECK(largest_step < 0.05f); // At 1 ms steps: no jumps from the speed ramp.
    // A peak on the outward half moves toward the centre.
    const auto peak_distance = [&](const Frame &f) {
        size_t best = 100;
        for (size_t i = 100; i < 150; ++i) { // 0 to 0.5 m from the centre: one wavelength.
            if (f[i].r + f[i].g + f[i].b > f[best].r + f[best].g + f[best].b) {
                best = i;
            }
        }
        return rig.pixels[best].position.x - 1.0;
    };
    const double d0 = peak_distance(at(1.0));
    const double d1 = peak_distance(at(1.05));
    CHECK(d1 < d0);
    const auto total = [](const Frame &f) {
        double sum = 0.0;
        for (const Rgb &c : f) {
            sum += c.r + c.g + c.b;
        }
        return sum;
    };
    CHECK(total(at(6.3)) > 3.0 * total(at(0.1)));
    in.section = "groove";
    CHECK(total(at(1.0)) == 0.0);
}

TEST_CASE("drop burst: a disc from the centre at the drop's start, then nothing") {
    const Rig rig = default_rig();
    const std::vector<Palette> palettes = default_palettes();
    Scene scene;
    scene.layers.push_back({make_drop_burst(), BlendMode::screen, 1.0f, true});
    EngineInputs in;
    in.section = "drop";
    in.section_length_s = 25.6;
    const auto lit_share = [&](double since_s) {
        in.since_section_s = since_s;
        const Frame f = render_scene(scene, rig, in, palettes, 1);
        size_t lit = 0;
        for (const Rgb &c : f) {
            lit += c.r > 0.05f;
        }
        return static_cast<double>(lit) / static_cast<double>(f.size());
    };
    const double early = lit_share(0.05);
    const double later = lit_share(0.25);
    CHECK(early < later); // The disc grows.
    CHECK(later > 0.4);   // A 1 m disc on the 2.5 by 1.2 m frame.
    CHECK(lit_share(1.6) == 0.0);
    in.section = "groove";
    CHECK(lit_share(0.25) == 0.0);
}

TEST_CASE("automation: a section shorter than a phrase keeps the scene before it") {
    const std::vector<Scene> scenes = default_scenes();
    const std::vector<DetectedSection> sections = {
        {0.0, 25.6, "groove", 1.0},  {25.6, 28.8, "breakdown", 1.0}, // A two-bar blip.
        {28.8, 54.4, "groove", 1.0}, {54.4, 80.0, "breakdown", 1.0},
        {80.0, 83.2, "build", 1.0},  {83.2, 86.4, "drop", 1.0}, // Short, but they change.
        {86.4, 120.0, "groove", 1.0}};
    // Without a dwell: groove, ambient, staccato. With one: the blip keeps the groove scene and
    // does not count, so the next groove is the second groove scene.
    CHECK(scene_for_section(scenes, sections, 1) == kAmbient);
    CHECK(scene_for_section(scenes, sections, 1, kPhraseS) == kGroove);
    CHECK(scene_for_section(scenes, sections, 2, kPhraseS) == kStaccato);
    CHECK(scene_for_section(scenes, sections, 3, kPhraseS) == kAmbient); // The first breakdown.
    CHECK(scene_for_section(scenes, sections, 4, kPhraseS) == kBuild);
    CHECK(scene_for_section(scenes, sections, 5, kPhraseS) == kDrop);
    // automate applies it: in the blip, the groove scene with no crossfade (rotation off: the
    // blip starts the groove's third phrase, where it would rotate anyway).
    TrackAnalysis a = arrangement();
    a.sections = sections;
    AutomationParams no_rotation;
    no_rotation.rotate_every_phrases = 0;
    const SceneMix mix = automate(scenes, a, a.grid, inputs_at(a, a.grid, 26.0), no_rotation);
    CHECK(mix.to == kGroove);
    CHECK(mix.from == -1);
}

TEST_CASE("automation: a one-bar build does not keep its scene on through the sections held "
          "after it (tolerant rule)") {
    // Alien Army's labelled lengths at its grid (finding L3): the two 8-bar grooves after the
    // one-bar build come out a few femtoseconds short of the phrase.
    const double bpm = 183.9997623150804;
    const double beat_s = 60.0 / bpm;
    const double bar_s = 4.0 * beat_s;
    const double phrase_s = 8.0 * bar_s;
    std::vector<DetectedSection> sections;
    double t = 0.0;
    for (const auto &[label, length_s] :
         std::vector<std::pair<const char *, double>>{{"groove", 52.2},
                                                      {"breakdown", bar_s},
                                                      {"build", bar_s},
                                                      {"groove", 10.434796088009065},
                                                      {"groove", 10.434796088009051},
                                                      {"groove", 16.0 * bar_s}}) {
        sections.push_back({t, t + length_s, label, 1.0});
        t += length_s;
    }
    REQUIRE(sections[3].end_s - sections[3].start_s < phrase_s); // Short by rounding.
    const std::vector<Scene> scenes = default_scenes();
    // Tolerant: the three grooves after the build show a groove scene; the build its own. The
    // 8-bar grooves are full phrases within the tolerance, so each takes its turn in the groove
    // rotation (Groove, Staccato).
    CHECK(scene_for_section(scenes, sections, 2, phrase_s, HoldRule::tolerant) == kBuild);
    CHECK(scene_for_section(scenes, sections, 3, phrase_s, HoldRule::tolerant) == kStaccato);
    CHECK(scene_for_section(scenes, sections, 4, phrase_s, HoldRule::tolerant) == kGroove);
    CHECK(scene_for_section(scenes, sections, 5, phrase_s, HoldRule::tolerant) == kStaccato);
    // Strict (the older rule): the first two keep the build scene.
    CHECK(scene_for_section(scenes, sections, 3, phrase_s, HoldRule::strict) == kBuild);
    CHECK(scene_for_section(scenes, sections, 4, phrase_s, HoldRule::strict) == kBuild);
    CHECK(scene_for_section(scenes, sections, 5, phrase_s, HoldRule::strict) == kStaccato);

    // A 4-bar groove after a 1-bar build inherits the groove before the build.
    const std::vector<DetectedSection> short_after = {{0.0, 25.6, "groove", 1.0},
                                                      {25.6, 27.2, "build", 1.0},
                                                      {27.2, 33.6, "groove", 1.0},
                                                      {33.6, 60.0, "breakdown", 1.0}};
    CHECK(scene_for_section(scenes, short_after, 2, kPhraseS, HoldRule::tolerant) == kGroove);
    CHECK(scene_for_section(scenes, short_after, 2, kPhraseS, HoldRule::strict) == kBuild);

    // automate follows the project's rule (tolerant by default).
    TrackAnalysis a = arrangement();
    a.sections = short_after;
    a.duration_s = 60.0;
    AutomationParams params;
    params.rotate_every_phrases = 0;
    params.fill = PhraseFill::off;
    params.vary_every_phrases = 0;
    REQUIRE(params.hold_rule == HoldRule::tolerant);
    CHECK(automate(scenes, a, a.grid, inputs_at(a, a.grid, 26.0), params).to == kBuild);
    CHECK(automate(scenes, a, a.grid, inputs_at(a, a.grid, 30.0), params).to == kGroove);
    params.hold_rule = HoldRule::strict;
    CHECK(automate(scenes, a, a.grid, inputs_at(a, a.grid, 30.0), params).to == kBuild);
}

TEST_CASE("automation: palette rotation through every palette, or each scene's own list") {
    Scene s;
    s.palette = 2;
    CHECK(scene_palette(s, 0, 6, PaletteRotation::all) == 2);
    CHECK(scene_palette(s, 5, 6, PaletteRotation::all) == 1);
    CHECK(scene_palette(s, -1, 6, PaletteRotation::all) == 1);
    CHECK(scene_palette(s, 7, 6, PaletteRotation::scene_list) == 2); // No list: its own.
    s.palette_list = {4, 0, 3};
    CHECK(scene_palette(s, 0, 6, PaletteRotation::all) == 2); // The list only counts in its mode.
    CHECK(scene_palette(s, 0, 6, PaletteRotation::scene_list) == 4);
    CHECK(scene_palette(s, 1, 6, PaletteRotation::scene_list) == 0);
    CHECK(scene_palette(s, 5, 6, PaletteRotation::scene_list) == 3);
    CHECK(scene_palette(s, -1, 6, PaletteRotation::scene_list) == 3);
    CHECK(scene_palette(s, 0, 3, PaletteRotation::scene_list) == 1); // Wrapped to the palettes.
}

TEST_CASE("automation: with every palette, render_mix shows each built-in pack as before") {
    const Rig rig = default_rig();
    for (const ShowPack &pack : builtin_show_packs()) {
        CAPTURE(pack.name);
        if (pack.automation.palette_rotation != PaletteRotation::all) {
            continue; // Hexagon 2: each scene's own list.
        }
        const TrackAnalysis a = arrangement();
        for (int k = 0; k < 20; ++k) {
            const double t = 5.0 * k + 0.37;
            const EngineInputs in = inputs_at(a, a.grid, t);
            SceneMix mix;
            mix.to = k % static_cast<int>(pack.scenes.size());
            mix.palette_shift = k;
            const Scene &scene = pack.scenes[static_cast<size_t>(mix.to)];
            const int n = static_cast<int>(pack.palettes.size());
            const Palette &before =
                pack.palettes[static_cast<size_t>(((scene.palette + k) % n + n) % n)];
            CHECK(render_mix(pack.scenes, mix, rig, in, pack.palettes, 1) ==
                  render_scene(scene, rig, in, before, 1));
        }
    }
}

TEST_CASE("automation: energy tags pick a section's scene by its energy") {
    std::vector<Scene> scenes(4);
    scenes[0].sections = {"groove"};
    scenes[1].sections = {"groove", "high"};
    scenes[2].sections = {"groove", "low"};
    scenes[3].sections = {"breakdown"};
    CHECK(scene_for_label(scenes, "groove", 0, 2) == 1);
    CHECK(scene_for_label(scenes, "groove", 5, 2) == 1); // The only high one, in any rotation.
    CHECK(scene_for_label(scenes, "groove", 0, 0) == 2);
    CHECK(scene_for_label(scenes, "groove", 0, 1) == 0);    // No mid scene: the untagged one.
    CHECK(scene_for_label(scenes, "groove", 1, -1) == 1);   // Energy ignored: plain rotation.
    CHECK(scene_for_label(scenes, "breakdown", 0, 2) == 3); // No energy tags: unchanged.
    // Only tagged scenes and no match: any of them, in rotation.
    std::vector<Scene> tagged_only(2);
    tagged_only[0].sections = {"groove", "high"};
    tagged_only[1].sections = {"groove", "low"};
    CHECK(scene_for_label(tagged_only, "groove", 0, 1) == 0);
    CHECK(scene_for_label(tagged_only, "groove", 1, 1) == 1);
    // Sections carry their energy into the rotation.
    std::vector<DetectedSection> sections = {{0.0, 30.0, "groove", 1.0},
                                             {30.0, 60.0, "groove", 1.0}};
    sections[0].energy = 0;
    sections[1].energy = 2;
    CHECK(scene_for_section(scenes, sections, 0) == 2);
    CHECK(scene_for_section(scenes, sections, 1) == 1);
}

namespace {

// A groove of six phrases, then a breakdown.
TrackAnalysis long_groove() {
    TrackAnalysis a;
    a.sample_rate_hz = kSampleRateHz;
    a.duration_s = 90.0;
    a.grid = {150.0, 0, kSampleRateHz};
    a.sections = {{0.0, 76.8, "groove", 1.0}, {76.8, 90.0, "breakdown", 1.0}};
    for (double t = 0.0; t < 90.0; t += kPhraseS) {
        a.phrase_starts_s.push_back(t);
    }
    return a;
}

SceneMix rotated_at(const TrackAnalysis &a, double t_s, const AutomationParams &params) {
    return automate(default_scenes(), a, a.grid, inputs_at(a, a.grid, t_s), params);
}

} // namespace

TEST_CASE("automation: grooves by phase: A B A shows the first look again") {
    // Three groove scenes, so taking turns gives three different looks.
    std::vector<Scene> scenes(3, default_scenes()[static_cast<size_t>(kGroove)]);
    TrackAnalysis a = long_groove(); // Three phrases a groove.
    a.duration_s = 128.0;
    a.phrase_starts_s.clear();
    for (double t = 0.0; t < 128.0; t += kPhraseS) {
        a.phrase_starts_s.push_back(t);
    }
    DetectedSection g1{0.0, 38.4, "groove", 1.0};
    g1.phase = 0;
    DetectedSection g2{38.4, 76.8, "groove", 1.0};
    g2.phase = 1;
    DetectedSection g3{76.8, 115.2, "groove", 1.0};
    g3.phase = 0;
    a.sections = {g1, g2, g3, {115.2, 128.0, "breakdown", 1.0}};
    AutomationParams p;
    const auto scene_at = [&](double t) {
        return automate(scenes, a, a.grid, inputs_at(a, a.grid, t), p).to;
    };
    CHECK(scene_at(39.0) != scene_at(1.0));
    CHECK(scene_at(77.0) != scene_at(1.0)); // Taking turns: the third groove, the third scene.
    p.scene_by = SceneBy::phase;
    CHECK(scene_at(77.0) == scene_at(1.0)); // A again.
    CHECK(scene_at(39.0) != scene_at(1.0)); // B.
    // Rotation inside a section is unchanged: two phrases into A, the next scene.
    CHECK(scene_at(77.0 + 2 * kPhraseS) != scene_at(77.0));
    // Sections without a phase take turns as before.
    for (DetectedSection &sec : a.sections) {
        sec.phase = -1;
    }
    const int by_phase = scene_at(77.0);
    p.scene_by = SceneBy::occurrence;
    CHECK(scene_at(77.0) == by_phase);
}

TEST_CASE("automation: a short breakdown between kicks takes the break scenes, when tagged") {
    TrackAnalysis a = long_groove();
    a.duration_s = 128.0;
    a.phrase_starts_s.clear();
    for (double t = 0.0; t < 128.0; t += kPhraseS) {
        a.phrase_starts_s.push_back(t);
    }
    // A 12-bar breakdown between two grooves, then a 16-bar one before a build.
    a.sections = {{0.0, 25.6, "groove", 1.0},  {25.6, 44.8, "breakdown", 1.0},
                  {44.8, 70.4, "groove", 1.0}, {70.4, 96.0, "breakdown", 1.0},
                  {96.0, 102.4, "build", 1.0}, {102.4, 128.0, "drop", 1.0}};
    std::vector<Scene> scenes = default_scenes();
    const auto scene_at = [&](double t) {
        return automate(scenes, a, a.grid, inputs_at(a, a.grid, t), AutomationParams{}).to;
    };
    const int breakdown = scene_at(30.0);
    CHECK(breakdown == scene_at(80.0)); // No scene tagged break: as before.
    Scene tagged = scenes[static_cast<size_t>(kStaccato)];
    tagged.sections = {"break"};
    scenes.push_back(tagged);
    const int break_scene = static_cast<int>(scenes.size()) - 1;
    CHECK(scene_at(30.0) == break_scene);
    CHECK(scene_at(80.0) == breakdown); // 16 bars, and a build after it: a main breakdown.
    // The breakdown crossfades in from the groove's look as before, to the break scene.
    const SceneMix m = automate(scenes, a, a.grid, inputs_at(a, a.grid, 25.7), AutomationParams{});
    CHECK(m.to == break_scene);
    CHECK(m.from == scene_at(25.5));
}

TEST_CASE("automation: fills from events and over breaks") {
    TrackAnalysis a = long_groove(); // Phrase-end fills at 25.6 and 51.2 (gap, 2 beats).
    a.events = {{"fill", 30.0, 1.6}, {"break", 40.0, 3.2}, {"fill", 50.8, 1.6}};
    AutomationParams p;
    p.fill = PhraseFill::gap;
    p.detected_events = true;
    const auto fill_at = [&](double t) { return rotated_at(a, t, p); };
    // Phrase ends only (the default): the events change nothing.
    CHECK(fill_at(25.2).fill == PhraseFill::gap);
    CHECK(fill_at(30.5).fill == PhraseFill::off);
    CHECK(fill_at(41.0).fill == PhraseFill::off);
    CHECK(fill_at(50.5).fill == PhraseFill::gap);
    // Events: the fill events over their own start and length, no phrase-end fills.
    p.fill_source = FillSource::events;
    CHECK(fill_at(25.2).fill == PhraseFill::off);
    SceneMix m = fill_at(30.5);
    CHECK(m.fill == PhraseFill::gap);
    CHECK(m.fill_s == doctest::Approx(1.6));
    CHECK(m.fill_progress == doctest::Approx(0.5 / 1.6));
    CHECK(fill_at(31.7).fill == PhraseFill::off);
    // Both: phrase ends too, but one overlapping a fill event gives way to it.
    p.fill_source = FillSource::both;
    CHECK(fill_at(25.2).fill == PhraseFill::gap);
    CHECK(fill_at(30.5).fill == PhraseFill::gap);
    CHECK(fill_at(50.5).fill == PhraseFill::off); // The phrase-end fill from 50.4 is dropped.
    m = fill_at(51.0);
    CHECK(m.fill == PhraseFill::gap);
    CHECK(m.fill_progress == doctest::Approx(0.2 / 1.6));
    // Mix picks one of the five for an event as for a phrase end.
    p.fill = PhraseFill::mix;
    CHECK(fill_at(30.5).fill != PhraseFill::off);
    CHECK(fill_at(30.5).fill != PhraseFill::mix);
    // Detected events are read only when asked.
    p.fill = PhraseFill::gap;
    p.detected_events = false;
    CHECK(fill_at(30.5).fill == PhraseFill::off);
    // A gap over a two-bar break, for its whole length.
    p = AutomationParams{};
    p.detected_events = true;
    CHECK(fill_at(41.0).fill == PhraseFill::off);
    p.break_fill = PhraseFill::gap;
    m = fill_at(41.0);
    CHECK(m.fill == PhraseFill::gap);
    CHECK(m.fill_s == doctest::Approx(3.2));
    CHECK(m.fill_progress == doctest::Approx(1.0 / 3.2));
    CHECK(fill_at(43.3).fill == PhraseFill::off);
    CHECK(fill_at(39.9).fill == PhraseFill::off);
}

TEST_CASE("automation: long sections rotate their scenes on phrase downbeats") {
    const TrackAnalysis a = long_groove();
    AutomationParams p; // The next scene every 2 phrases, cut.
    CHECK(rotated_at(a, 5.0, p).to == kGroove);
    CHECK(rotated_at(a, kPhraseS + 5.0, p).to == kGroove);
    SceneMix m = rotated_at(a, 2 * kPhraseS + 0.01, p); // Phrase 2: the cut.
    CHECK(m.to == kStaccato);
    CHECK(m.from == -1);
    CHECK(rotated_at(a, 4 * kPhraseS + 1.0, p).to == kGroove); // And round again.
    // Crossfaded instead: halfway through 4 beats.
    p.rotate_fade_beats = 4.0;
    m = rotated_at(a, 2 * kPhraseS + 2 * kBeatS, p);
    CHECK(m.from == kGroove);
    CHECK(m.to == kStaccato);
    CHECK(m.to_weight == doctest::Approx(0.5));
    CHECK(rotated_at(a, 2 * kPhraseS + 5 * kBeatS, p).from == -1);
    // The breakdown crossfades from the scene the groove ended on (every phrase: 6 rotations).
    p.rotate_every_phrases = 1;
    m = rotated_at(a, 76.8 + 2 * kBeatS, p);
    CHECK(m.from == kStaccato);
    CHECK(m.to == kAmbient);
    // Off: one scene a section.
    p.rotate_every_phrases = 0;
    CHECK(rotated_at(a, 3 * kPhraseS, p).to == kGroove);
    // Builds keep theirs.
    TrackAnalysis build = a;
    build.sections = {{0.0, 76.8, "build", 1.0}};
    p.rotate_every_phrases = 1;
    CHECK(rotated_at(build, 4 * kPhraseS + 1.0, p).to == kBuild);
}

TEST_CASE("automation: live mode rotates by phrase_bars phrases from the section's start") {
    LiveState s;
    s.tempo_known = true;
    s.locked = true;
    s.period_s = kBeatS;
    s.phrase_bars = 8; // 12.8 s.
    s.section = LiveSection::groove;
    s.section_start_s = 10.0;
    s.section_count = 1;
    s.section_occurrences[static_cast<size_t>(LiveSection::groove)] = 1;
    const std::vector<Scene> scenes = default_scenes();
    const auto at = [&](double t) {
        return automate_live(scenes, s, inputs_at(s, t), AutomationParams{});
    };
    CHECK(at(10.0 + kPhraseS + 1.0).to == kGroove);
    CHECK(at(10.0 + 2 * kPhraseS + 0.01).to == kStaccato);
    CHECK(at(10.0 + 4 * kPhraseS + 0.01).to == kGroove);
}

namespace {

// A live state locked at 150 BPM, phrases of 8 bars (12.8 s).
LiveState live_locked() {
    LiveState s;
    s.tempo_known = true;
    s.locked = true;
    s.period_s = kBeatS;
    s.phrase_bars = 8;
    return s;
}

// A live section change, as LiveBeat::set_section makes it.
void enter(LiveState &s, LiveSection section, double start_s) {
    s.earlier_look = s.previous_look;
    s.previous_look = s.look;
    if (s.previous_look.energy < 0) {
        s.previous_look.energy = s.section_energy;
    }
    s.has_earlier = s.has_previous;
    s.earlier_section = s.previous_section;
    s.earlier_occurrence = s.previous_occurrence;
    s.earlier_energy = s.previous_energy;
    s.earlier_length_s = s.previous_length_s;
    s.has_previous = s.section_count > 0;
    s.previous_section = s.section;
    s.previous_occurrence = std::max(0, s.section_occurrences[static_cast<size_t>(s.section)] - 1);
    s.previous_energy = s.section_energy;
    s.previous_length_s = std::max(0.0, start_s - s.section_start_s);
    ++s.section_occurrences[static_cast<size_t>(section)];
    s.section = section;
    s.section_start_s = start_s;
    ++s.section_count;
    const double phrase_s = 4.0 * s.phrase_bars * s.period_s;
    if (section == LiveSection::groove && s.has_previous &&
        s.previous_section == LiveSection::breakdown && s.previous_length_s < phrase_s - 1e-6 &&
        s.has_earlier &&
        (s.earlier_section == LiveSection::groove || s.earlier_section == LiveSection::drop)) {
        s.look = s.earlier_look;
        s.look.phrases_before +=
            static_cast<long long>(std::floor(std::max(0.0, s.earlier_length_s / phrase_s - 1e-6)));
        s.look.continued = true;
    } else {
        s.look = {section, std::max(0, s.section_occurrences[static_cast<size_t>(section)] - 1),
                  -1,      s.section_count,
                  0,       false};
    }
}

constexpr double kBarS = 4 * kBeatS;

} // namespace

TEST_CASE("automation: live breakdown commit delay") {
    const std::vector<Scene> scenes = default_scenes();
    // A groove of two phrases from 10 s, then a breakdown from 35.6 s.
    LiveState groove = live_locked();
    enter(groove, LiveSection::groove, 10.0);
    LiveState breakdown = groove;
    enter(breakdown, LiveSection::breakdown, 35.6);
    for (const int bars : {0, 4}) {
        CAPTURE(bars);
        AutomationParams p;
        p.live_breakdown_commit_bars = bars;
        const auto at = [&](const LiveState &s, double t) {
            return automate_live(scenes, s, inputs_at(s, t), p);
        };
        const SceneMix ended = at(groove, 35.59); // The look the groove ended on.
        const SceneMix early = at(breakdown, 35.6 + 0.1);
        if (bars == 0) { // As before: the breakdown's scene at once, crossfading in.
            CHECK(early.to != ended.to);
            CHECK(early.from == ended.to);
        } else { // The groove's look stays through the wait, no fade.
            for (const double into : {0.1, 2.0 * kBarS, 4.0 * kBarS - 0.05}) {
                const SceneMix m = at(breakdown, 35.6 + into);
                CHECK(m.to == ended.to);
                CHECK(m.variant == ended.variant);
                CHECK(m.from == -1);
            }
            // Then the breakdown's scene crossfades in from the groove's.
            const SceneMix after = at(breakdown, 35.6 + 4.0 * kBarS + 0.1);
            CHECK(after.to == at(breakdown, 35.6 + 6.0 * kBarS).to);
            CHECK(after.to != ended.to);
            CHECK(after.from == ended.to);
            CHECK(after.to_weight < 0.2);
        }
        // A build declared in the breakdown's third bar takes over at once, from the look that
        // was showing.
        LiveState build = breakdown;
        enter(build, LiveSection::build, 35.6 + 3.0 * kBarS);
        const SceneMix b = at(build, 35.6 + 3.0 * kBarS + 0.1);
        CHECK(b.to == kBuild);
        CHECK(b.from == (bars == 0 ? early.to : ended.to));
    }
}

TEST_CASE("automation: live resumed groove") {
    const std::vector<Scene> scenes = default_scenes();
    // Groove 1 from 10 s for a phrase and a half (its last look: phrase 1), a breakdown of
    // `break_bars` bars, then groove 2.
    const auto run = [&](double break_bars, AutomationParams p) {
        LiveState s = live_locked();
        enter(s, LiveSection::groove, 10.0);
        const double g1_end = 10.0 + 1.5 * kPhraseS;
        const SceneMix last = automate_live(scenes, s, inputs_at(s, g1_end - 0.01), p);
        enter(s, LiveSection::breakdown, g1_end);
        const double g2 = g1_end + break_bars * kBarS;
        enter(s, LiveSection::groove, g2);
        return std::pair{last, [=, &scenes](double into_s) {
                             return automate_live(scenes, s, inputs_at(s, g2 + into_s), p);
                         }};
    };
    AutomationParams resume;
    resume.live_resume = true;
    {
        // Six bars of breakdown: groove 2 takes up groove 1's look, and its rotation goes on
        // from groove 1's phrases (rotate every 2: groove 1 ended in phrase 1, step 0; groove 2's
        // phrase 1 is phrase 2, step 1).
        const auto [last, at] = run(6.0, resume);
        CHECK(at(0.1).to == last.to);
        CHECK(at(0.1).variant == last.variant);
        LiveState g1 = live_locked();
        enter(g1, LiveSection::groove, 10.0);
        const SceneMix g1_phrase2 =
            automate_live(scenes, g1, inputs_at(g1, 10.0 + 2 * kPhraseS + 0.1), resume);
        CHECK(g1_phrase2.to != last.to);
        CHECK(at(kPhraseS + 0.1).to == g1_phrase2.to);
        CHECK(at(kPhraseS + 0.1).variant == g1_phrase2.variant);
        // Off (the default): the next groove look.
        const auto [last_off, at_off] = run(6.0, AutomationParams{});
        CHECK(last_off.to == last.to);
        CHECK(at_off(0.1).to != last.to);
    }
    {
        // A breakdown of two phrases is a real one: not resumed.
        const auto [last, at] = run(16.0, resume);
        const auto [last_off, at_off] = run(16.0, AutomationParams{});
        CHECK(at(0.1).to == at_off(0.1).to);
        CHECK(at(0.1).variant == at_off(0.1).variant);
    }
    {
        // With the commit delay at 4 bars and resume on, a 3-bar breakdown never changes the
        // scene: groove, breakdown, groove all show groove 1's last look.
        AutomationParams both = resume;
        both.live_breakdown_commit_bars = 4;
        LiveState s = live_locked();
        enter(s, LiveSection::groove, 10.0);
        const double g1_end = 10.0 + 1.5 * kPhraseS;
        const SceneMix last = automate_live(scenes, s, inputs_at(s, g1_end - 0.01), both);
        enter(s, LiveSection::breakdown, g1_end);
        for (double t = g1_end; t < g1_end + 3 * kBarS; t += 0.2) {
            CHECK(automate_live(scenes, s, inputs_at(s, t), both).to == last.to);
        }
        enter(s, LiveSection::groove, g1_end + 3 * kBarS);
        CHECK(automate_live(scenes, s, inputs_at(s, g1_end + 3 * kBarS + 0.1), both).to == last.to);
    }
}

TEST_CASE("automation: live resume through a chain of short breaks keeps the look shown") {
    const std::vector<Scene> scenes = default_scenes();
    AutomationParams resume;
    resume.live_resume = true;
    // Groove 1 a phrase and a half from 10 s, then breakdown, groove, breakdown, groove, each
    // break 3 bars and each groove between them 4 bars.
    LiveState s = live_locked();
    enter(s, LiveSection::groove, 10.0);
    double t = 10.0 + 1.5 * kPhraseS;
    const SceneMix shown = automate_live(scenes, s, inputs_at(s, t - 0.01), resume);
    for (int k = 0; k < 2; ++k) {
        enter(s, LiveSection::breakdown, t);
        t += 3 * kBarS;
        enter(s, LiveSection::groove, t);
        CAPTURE(k);
        const SceneMix m = automate_live(scenes, s, inputs_at(s, t + 0.1), resume);
        CHECK(m.to == shown.to); // The second break resumes it too (E2 alone did not).
        CHECK(s.look.continued);
        CHECK(s.look.key == 1);
        t += 4 * kBarS;
    }
    CHECK(s.look.phrases_before == 1); // Groove 1 ended in its phrase 1; the 4-bar groove adds 0.
}

TEST_CASE("automation: phrase-end fills in long grooves, before rotations, not at the end") {
    const TrackAnalysis a = long_groove(); // Six phrases of groove, then a breakdown.
    AutomationParams p;
    p.fill = PhraseFill::gap;
    p.fill_every_phrases = 3; // Phrase 2's end (38.4); and before each rotation (25.6, 51.2).
    p.fill_beats = 2.0;
    const auto fill_at = [&](double t) { return rotated_at(a, t, p); };
    CHECK(fill_at(12.5).fill == PhraseFill::off); // Phrase 0's end: neither.
    SceneMix m = fill_at(25.6 - kBeatS);          // Before the rotation at phrase 2.
    CHECK(m.fill == PhraseFill::gap);
    CHECK(m.fill_progress == doctest::Approx(0.5));
    CHECK(fill_at(38.4 - 0.1).fill == PhraseFill::gap);        // Every third phrase.
    CHECK(fill_at(38.4 - 3 * kBeatS).fill == PhraseFill::off); // Before the fill.
    CHECK(fill_at(51.2 - 0.1).fill == PhraseFill::gap);
    CHECK(fill_at(76.8 - 0.1).fill == PhraseFill::off); // The section's last phrase.
    CHECK(fill_at(80.0).fill == PhraseFill::off);       // Breakdowns have none.
    // Mix picks one of the five per phrase; off has none.
    p.fill = PhraseFill::mix;
    const PhraseFill picked = fill_at(25.6 - 0.1).fill;
    CHECK(picked != PhraseFill::off);
    CHECK(picked != PhraseFill::mix);
    p.fill = PhraseFill::off;
    CHECK(fill_at(25.6 - 0.1).fill == PhraseFill::off);
    // A held scene has none.
    p.fill = PhraseFill::gap;
    CHECK(automate(default_scenes(), a, a.grid, inputs_at(a, a.grid, 25.5), p, kGroove).fill ==
          PhraseFill::off);
    // The gap darkens the frame; the others change it.
    const Rig rig = default_rig();
    const EngineInputs in = inputs_at(a, a.grid, 25.6 - 1.5 * kBeatS); // Mid-beat.
    SceneMix plain = rotated_at(a, 25.6 - 1.5 * kBeatS, AutomationParams{});
    plain.fill = PhraseFill::off;
    const Frame base = render_mix(default_scenes(), plain, rig, in, default_palettes(), 1);
    for (const PhraseFill f :
         {PhraseFill::gap, PhraseFill::sweep, PhraseFill::implode, PhraseFill::pulse}) {
        const std::string id = phrase_fill_id(f);
        CAPTURE(id);
        SceneMix filled = plain;
        filled.fill = f;
        filled.fill_s = 2 * kBeatS;
        filled.fill_progress = 0.5;
        const Frame out = render_mix(default_scenes(), filled, rig, in, default_palettes(), 1);
        CHECK(max_difference(base, out) > 0.05f);
        if (f == PhraseFill::gap) {
            CHECK(std::all_of(out.begin(), out.end(),
                              [](const Rgb &c) { return c.r + c.g + c.b == 0.0f; }));
        }
    }
}

namespace {

bool same_look(const Scene &a, const Scene &b) {
    if (a.layers.size() != b.layers.size()) {
        return false;
    }
    for (size_t l = 0; l < a.layers.size(); ++l) {
        const Layer &x = a.layers[l];
        const Layer &y = b.layers[l];
        if (x.opacity != y.opacity || x.spec.type() != y.spec.type()) {
            return false;
        }
        for (size_t i = 0; i < effect_param_infos(x.spec.type()).size(); ++i) {
            if (effect_param(x.spec, i) != effect_param(y.spec, i)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("variations: the scene first, then seeded changes of the kinds allowed") {
    Scene scene;
    WaveParams wave;
    wave.rate = 1.0;
    scene.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::alpha));
    scene.layers.push_back(make_layer({wave}, BlendMode::add));
    scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen));
    const std::vector<Scene> v = scene_variations(scene, VariationKinds{}, 7);
    REQUIRE(v.size() == kSceneVariants);
    CHECK(same_look(v[0], scene));
    for (int k = 1; k < kSceneVariants; ++k) {
        CAPTURE(k);
        CHECK_FALSE(same_look(v[static_cast<size_t>(k)], scene));
    }
    CHECK(same_look(scene_variations(scene, VariationKinds{}, 7)[2], v[2])); // Deterministic.
    // Only reversal allowed: the wave runs backwards, nothing else changes.
    VariationKinds reverse_only{true, false, false, false, false};
    const Scene back = scene_variations(scene, reverse_only, 7)[1];
    CHECK(std::get<WaveParams>(back.layers[1].spec.params).rate == -1.0);
    CHECK(back.layers[2].opacity == 1.0f);
    // Nothing allowed: every variant is the scene.
    for (const Scene &s :
         scene_variations(scene, VariationKinds{false, false, false, false, false}, 7)) {
        CHECK(same_look(s, scene));
    }
}

TEST_CASE("automation: variations each phrase within a rotation, the scene itself first") {
    const TrackAnalysis a = long_groove();
    AutomationParams p; // Next scene every 2 phrases, a variation every phrase.
    const auto at = [&](int phrase) { return rotated_at(a, phrase * kPhraseS + 1.0, p); };
    CHECK(at(0).to == kGroove);
    CHECK(at(0).variant == 0);
    CHECK(at(1).to == kGroove);
    CHECK(at(1).variant != 0);
    CHECK(at(2).to == kStaccato);
    CHECK(at(2).variant == 0);
    CHECK(at(3).variant != 0);
    // No rotation: a new variation every phrase, never the same twice running.
    p.rotate_every_phrases = 0;
    for (int phrase = 1; phrase < 5; ++phrase) {
        CAPTURE(phrase);
        CHECK(at(phrase).variant != 0);
        CHECK(at(phrase + 1).variant != at(phrase).variant);
        CHECK(at(phrase).variant == at(phrase).variant); // Deterministic.
    }
    p.vary_every_phrases = 0;
    CHECK(at(3).variant == 0);
    // Builds keep theirs.
    TrackAnalysis build = a;
    build.sections = {{0.0, 76.8, "build", 1.0}};
    p.vary_every_phrases = 1;
    CHECK(rotated_at(build, 3 * kPhraseS + 1.0, p).variant == 0);
    // render_mix draws the variant.
    const std::vector<Scene> scenes = default_scenes();
    const SceneVariants variants = all_variations(scenes, VariationKinds{}, 1);
    const Rig rig = default_rig();
    const EngineInputs in = inputs_at(a, a.grid, 20.0);
    SceneMix m;
    m.to = kGroove;
    const Frame plain = render_mix(scenes, m, rig, in, default_palettes(), 1, nullptr, &variants);
    float most = 0.0f;
    for (int k = 1; k < kSceneVariants; ++k) {
        m.variant = k;
        most =
            std::max(most, max_difference(plain, render_mix(scenes, m, rig, in, default_palettes(),
                                                            1, nullptr, &variants)));
    }
    CHECK(most > 0.05f);
}

TEST_CASE("intensity: ranked bar features, smoothed; read between bar centres") {
    std::vector<SectionBar> bars(8);
    for (size_t i = 0; i < bars.size(); ++i) {
        bars[i].start_s = 1.6 * static_cast<double>(i);
        bars[i].level_db = -30.0 + 2.0 * static_cast<double>(i); // Filling up bar by bar.
        bars[i].high_db = -40.0 + static_cast<double>(i);
        bars[i].centroid_hz = 1000.0 + 100.0 * static_cast<double>(i);
        bars[i].kick_beats = 4;
    }
    const std::vector<float> curve = intensity_curve(bars, {});
    REQUIRE(curve.size() == bars.size());
    for (size_t i = 1; i < curve.size(); ++i) {
        CHECK(curve[i] >= curve[i - 1]);
    }
    CHECK(curve.front() < 0.3f);
    CHECK(curve.back() > 0.6f);
    for (const float v : curve) {
        CHECK(v >= 0.0f);
        CHECK(v <= 1.0f);
    }
    // Between bar centres, linear; before the first and after the last, the end values.
    CHECK(intensity_at(bars, curve, 0.0) == doctest::Approx(curve[0]));
    CHECK(intensity_at(bars, curve, 1.6) == doctest::Approx(0.5 * (curve[0] + curve[1])));
    CHECK(intensity_at(bars, curve, 100.0) == doctest::Approx(curve.back()));
    CHECK(intensity_at({}, {}, 3.0) == 0.5);
}

TEST_CASE("automation: following the energy: dims when quieter, phrases pick scenes by energy") {
    TrackAnalysis a = long_groove();
    // A groove thin for three phrases, then full.
    for (double t = 0.0; t < 76.8; t += 4 * kBeatS) {
        SectionBar bar;
        bar.start_s = t;
        a.section_bars.push_back(bar);
        a.intensity.push_back(t < 3 * kPhraseS ? 0.1f : 0.9f);
    }
    std::vector<Scene> scenes(2);
    scenes[0].sections = {"groove", "low"};
    scenes[1].sections = {"groove", "high"};
    AutomationParams p;
    p.rotate_every_phrases = 0;
    p.vary_every_phrases = 0;
    const auto at = [&](double t) {
        return automate(scenes, a, a.grid, inputs_at(a, a.grid, t), p);
    };
    CHECK(at(5.0).to == 0);
    CHECK(at(2 * kPhraseS + 5.0).to == 0);
    CHECK(at(3 * kPhraseS + 5.0).to == 1); // The phrase where it fills up.
    CHECK(at(5.0).brightness == doctest::Approx(1.0 - 0.25 * 0.9));
    CHECK(at(4 * kPhraseS + 5.0).brightness == doctest::Approx(1.0 - 0.25 * 0.1));
    // Off: the section's energy (mid; neither tag matches, so the rotation over both).
    p.energy_scenes = false;
    CHECK(at(3 * kPhraseS + 5.0).to == at(5.0).to);
    p.energy_dim = 0.0;
    CHECK(at(5.0).brightness == 1.0);
    // render_mix applies it.
    const Rig rig = default_rig();
    const std::vector<Scene> library = default_scenes();
    const EngineInputs in = inputs_at(a, a.grid, 5.0);
    SceneMix full;
    full.to = kGroove;
    SceneMix dim = full;
    dim.brightness = 0.5;
    const Frame bright = render_mix(library, full, rig, in, default_palettes(), 1);
    const Frame half = render_mix(library, dim, rig, in, default_palettes(), 1);
    for (size_t i = 0; i < bright.size(); ++i) {
        CHECK(half[i].r == doctest::Approx(0.5f * bright[i].r).epsilon(1e-4));
    }
}
