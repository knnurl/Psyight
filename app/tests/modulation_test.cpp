// Modulation matrix: sources, curves, hue rotation and routes applied by the scene renderer.

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
