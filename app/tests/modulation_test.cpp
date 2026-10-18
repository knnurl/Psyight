// Modulation matrix: sources, curves, hue rotation and routes applied by the scene renderer.

#include "engine/effect_spec.h"
#include "engine/limiters.h"
#include "engine/modulation.h"
#include "engine/rig.h"
#include "engine/scene.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace psyight;

namespace {

EngineInputs sample_inputs(double t) {
    EngineInputs in;
    in.t_s = t;
    in.beat_phase = std::fmod(t, 1.0);
    in.bar_phase = std::fmod(t / 4.0, 1.0);
    in.bar_index = static_cast<long long>(t / 4.0);
    in.phrase_phase = std::fmod(t / 32.0, 1.0);
    in.section_progress = t / 100.0;
    in.since_kick_s = std::fmod(t, 0.4);
    in.bass_active = std::fmod(t, 0.1) < 0.05;
    in.bass_velocity = 0.8;
    in.bass_gate_ratio = 0.6;
    in.bass_midi = 28;
    in.residual_level = 0.3;
    in.residual_centroid_hz = 9000.0; // Above the range: clamped.
    return in;
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

TEST_CASE("modulation: every source stays in 0 to 1 and every curve keeps its ends") {
    for (double t = 0.0; t < 100.0; t += 0.37) {
        const EngineInputs in = sample_inputs(t);
        for (const ModSource s : kModSources) {
            const double v = source_value(s, in, 1);
            CHECK(v >= 0.0);
            CHECK(v <= 1.0);
        }
    }
    EngineInputs in;
    CHECK(source_value(ModSource::kick, in, 1) == 0.0); // No kick yet.
    CHECK(source_value(ModSource::constant, in, 1) == 1.0);
    in.beat_phase = 0.5;
    CHECK(source_value(ModSource::lfo_beat, in, 1) == doctest::Approx(1.0));
    in.bass_midi = 42;
    CHECK(source_value(ModSource::bass_pitch, in, 1) == doctest::Approx(0.5));
    in.bar_index = 7;
    const double r = source_value(ModSource::random_bar, in, 1);
    CHECK(source_value(ModSource::random_bar, in, 1) == r); // Seeded, per bar.
    CHECK(source_value(ModSource::random_bar, in, 2) != r);
    for (const ModCurve c : kModCurves) {
        CHECK(shaped(0.0, c, false) == doctest::Approx(0.0));
        CHECK(shaped(1.0, c, false) == doctest::Approx(1.0));
        CHECK(shaped(0.0, c, true) == doctest::Approx(1.0));
        CHECK(shaped(0.25, c, false) <= shaped(0.5, c, false)); // Monotonic.
    }
    CHECK(shaped(0.5, ModCurve::exponential, false) == doctest::Approx(0.25));
    CHECK(shaped(0.25, ModCurve::logarithmic, false) == doctest::Approx(0.5));
}

TEST_CASE("modulation: macros are sources") {
    EngineInputs in;
    in.macros[2] = 0.75;
    in.macros[7] = 2.0; // Out of range: clamped.
    CHECK(source_value(ModSource::macro_3, in, 1) == 0.75);
    CHECK(source_value(ModSource::macro_8, in, 1) == 1.0);
    CHECK(source_value(ModSource::macro_1, in, 1) == 0.0);
    CHECK(mod_source_from_id("macro_5") == ModSource::macro_5);
}

TEST_CASE("modulation: hold per beat samples at the division's first frame") {
    ModRoute route;
    route.source = ModSource::beat_phase;
    route.quantise = ModQuantise::beat;
    ModRouteState state;
    EngineInputs in;
    // 60 frames a beat: the value is the phase at each beat's first frame, held through it.
    for (int frame = 0; frame < 180; ++frame) {
        in.t_s = frame / 60.0;
        in.beat_index = frame / 60;
        in.beat_phase = (frame % 60) / 60.0 + 0.01;
        CHECK(step_route(route, state, in, 1) == doctest::Approx(0.01));
    }
    // Sixteenths: four divisions a beat.
    route.quantise = ModQuantise::sixteenth;
    in.beat_index = 5;
    in.beat_phase = 0.3;
    CHECK(mod_division(ModQuantise::sixteenth, in) == 21);
    in.beat_phase = 0.99;
    CHECK(mod_division(ModQuantise::sixteenth, in) == 23);
}

TEST_CASE("modulation: attack and release smooth a step; time jumps restart") {
    ModRoute route;
    route.source = ModSource::macro_1;
    route.attack_s = 0.1;
    route.release_s = 0.4;
    ModRouteState state;
    EngineInputs in;
    const double dt = 0.001;
    in.t_s = 0.0;
    CHECK(step_route(route, state, in, 1) == 0.0);
    in.macros[0] = 1.0; // Step up: one attack time constant reaches 1 - 1/e.
    for (int k = 1; k <= 100; ++k) {
        in.t_s = k * dt;
        step_route(route, state, in, 1);
    }
    CHECK(state.value == doctest::Approx(1.0 - std::exp(-1.0)).epsilon(1e-6));
    // The same render time again changes nothing.
    CHECK(step_route(route, state, in, 1) == doctest::Approx(1.0 - std::exp(-1.0)).epsilon(1e-6));
    const double top = state.value;
    in.macros[0] = 0.0; // Step down: the release time constant.
    for (int k = 101; k <= 500; ++k) {
        in.t_s = k * dt;
        step_route(route, state, in, 1);
    }
    CHECK(state.value == doctest::Approx(top * std::exp(-1.0)).epsilon(1e-6));
    // A seek (or a scene not rendered for a while) restarts at the current value.
    in.macros[0] = 0.9;
    in.t_s = 10.0;
    CHECK(step_route(route, state, in, 1) == 0.9);
    in.t_s = 2.0; // Backwards.
    in.macros[0] = 0.2;
    CHECK(step_route(route, state, in, 1) == 0.2);
}

TEST_CASE("modulation: rendering with memory follows the same render times identically") {
    const Rig rig = default_rig();
    Scene scene = default_scene();
    ModRoute route;
    route.source = ModSource::random_bar;
    route.target = ModTarget::opacity;
    route.layer = 0;
    route.amount = 0.5;
    route.quantise = ModQuantise::beat;
    route.attack_s = 0.2;
    route.release_s = 0.2;
    scene.routes.push_back(route);
    const std::vector<Palette> palettes = default_palettes();
    std::vector<ModRouteState> a;
    std::vector<ModRouteState> b;
    for (int frame = 0; frame < 240; ++frame) {
        EngineInputs in = sample_inputs(frame / 60.0);
        in.beat_index = frame / 30;
        const Frame fa = render_scene(scene, rig, in, palettes, 7, &a);
        const Frame fb = render_scene(scene, rig, in, palettes, 7, &b);
        REQUIRE(max_difference(fa, fb) == 0.0f);
    }
    REQUIRE(a.size() == scene.routes.size());
    CHECK(a.back().value == b.back().value);
}

TEST_CASE("modulation: hue rotation keeps grey, wraps at 360 and keeps luminance roughly") {
    const Rgb grey{0.4f, 0.4f, 0.4f};
    const Rgb g = rotate_hue(grey, 123.0);
    CHECK(g.r == doctest::Approx(0.4f).epsilon(0.01));
    CHECK(g.b == doctest::Approx(0.4f).epsilon(0.01));
    const Rgb violet{0.5f, 0.0f, 1.0f};
    const Rgb full = rotate_hue(violet, 360.0);
    CHECK(full.r == doctest::Approx(violet.r).epsilon(0.001));
    CHECK(full.b == doctest::Approx(violet.b).epsilon(0.001));
    const Rgb shifted = rotate_hue(violet, 60.0);
    CHECK(std::abs(shifted.r - violet.r) + std::abs(shifted.g - violet.g) > 0.1f);
    const auto luma = [](const Rgb &c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; };
    CHECK(luma(shifted) == doctest::Approx(luma(violet)).epsilon(0.15));
}

TEST_CASE("modulation: removing and swapping layers keeps routes on their layers") {
    Scene scene = default_scene(); // Plasma, conveyor, shimmer, sparkle, shockwave.
    scene.routes = {
        {ModSource::kick, ModCurve::linear, false, ModTarget::opacity, 1, 0.5},
        {ModSource::lfo_bar, ModCurve::linear, false, ModTarget::hue_shift, 3, 30.0},
        {ModSource::constant, ModCurve::linear, false, ModTarget::brightness, 1, 0.2},
    };
    swap_layers(scene, 1, 3);
    CHECK(scene.layers[1].spec.type() == EffectType::sparkle);
    CHECK(scene.routes[0].layer == 3); // Still the conveyor.
    CHECK(scene.routes[1].layer == 1); // Still the sparkle.
    remove_layer(scene, 1);            // The sparkle and its hue route go.
    REQUIRE(scene.layers.size() == 4);
    REQUIRE(scene.routes.size() == 2);
    CHECK(scene.routes[0].layer == 2); // The conveyor moved down one.
    CHECK(scene.layers[2].spec.type() == EffectType::conveyor);
    CHECK(scene.routes[1].target == ModTarget::brightness); // Not tied to a layer.
}

TEST_CASE("scene: a group mask limits a layer to the strips with that tag") {
    const Rig rig =
        default_rig(); // Strips bottom, right, top, left; each tagged frame and its side.
    const std::vector<Palette> palettes = default_palettes();
    Scene scene;
    scene.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::add));
    EngineInputs in;
    in.t_s = 3.0;
    in.section = "breakdown";
    const Frame all = render_scene(scene, rig, in, palettes, 1);
    scene.layers[0].group = "top";
    const Frame top = render_scene(scene, rig, in, palettes, 1);
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        CAPTURE(i);
        if (rig.pixels[i].strip == 2) {
            CHECK(top[i] == all[i]);
        } else {
            CHECK(luminance(top[i]) == 0.0);
        }
    }
    scene.layers[0].group = "frame";
    CHECK(render_scene(scene, rig, in, palettes, 1) == all);
    scene.layers[0].group = "nowhere";
    for (const Rgb &c : render_scene(scene, rig, in, palettes, 1)) {
        CHECK(luminance(c) == 0.0);
    }
}

TEST_CASE("modulation: routes change opacity, hue and brightness, and rendering stays pure") {
    const Rig rig = default_rig();
    const std::vector<Palette> palettes = default_palettes();
    Scene scene = default_scene();
    EngineInputs in = sample_inputs(20.05);
    in.section = "breakdown";
    const Frame plain = render_scene(scene, rig, in, palettes, 1);

    // A constant route of -1 on a layer's opacity removes it.
    Scene no_plasma = scene;
    no_plasma.layers[0].enabled = false;
    Scene routed = scene;
    routed.routes.push_back(
        {ModSource::constant, ModCurve::linear, false, ModTarget::opacity, 0, -1.0});
    CHECK(max_difference(render_scene(routed, rig, in, palettes, 1),
                         render_scene(no_plasma, rig, in, palettes, 1)) < 1e-6f);

    // Brightness from a source at 0 with amount 1: black. Amount 0.5: half.
    Scene dark = scene;
    dark.routes.push_back(
        {ModSource::constant, ModCurve::linear, true, ModTarget::brightness, 0, 1.0});
    for (const Rgb &c : render_scene(dark, rig, in, palettes, 1)) {
        CHECK(luminance(c) == 0.0);
    }
    dark.routes[0].amount = 0.5;
    const Frame half = render_scene(dark, rig, in, palettes, 1);
    for (size_t i = 0; i < plain.size(); i += 41) {
        CHECK(half[i].r == doctest::Approx(0.5f * plain[i].r));
    }

    // A hue shift changes colours; a full turn does not.
    Scene hue = scene;
    hue.routes.push_back(
        {ModSource::constant, ModCurve::linear, false, ModTarget::hue_shift, 0, 90.0});
    CHECK(max_difference(render_scene(hue, rig, in, palettes, 1), plain) > 0.02f);
    hue.routes[0].amount = 360.0;
    CHECK(max_difference(render_scene(hue, rig, in, palettes, 1), plain) < 1e-3f);

    // Same inputs, same frame.
    const Scene ambient = default_scenes()[3];
    REQUIRE(ambient.routes.size() == 1);
    CHECK(render_scene(ambient, rig, in, palettes, 1) ==
          render_scene(ambient, rig, in, palettes, 1));
}
