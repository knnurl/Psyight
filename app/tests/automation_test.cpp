// Scene automation, the scene library and the build and drop effects.

#include "engine/automation.h"
#include "engine/inputs.h"
#include "engine/rig.h"
#include "engine/scene.h"

#include <doctest/doctest.h>

#include <cmath>
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
    // automate applies it: in the blip, the groove scene with no crossfade.
    TrackAnalysis a = arrangement();
    a.sections = sections;
    const SceneMix mix = mix_at(scenes, a, 26.0);
    CHECK(mix.to == kGroove);
    CHECK(mix.from == -1);
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
