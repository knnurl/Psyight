// Post-effects with frame memory (engine/post.h): afterglow and radial echo.

#include "effect_test_helpers.h"
#include "engine/automation.h"
#include "engine/post.h"
#include "engine/space.h"
#include "io/project.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace psyight;
using test::grid_at;
using test::level;
using test::mandala;

namespace {

EngineInputs at_time(double t_s) {
    const double beats = t_s / 0.4;
    EngineInputs in = grid_at(static_cast<long long>(std::floor(beats)), beats - std::floor(beats));
    in.t_s = t_s;
    in.section = "groove";
    return in;
}

float brightest(const Frame &f) {
    float most = 0.0f;
    for (const Rgb &c : f) {
        most = std::max(most, level(c));
    }
    return most;
}

} // namespace

TEST_CASE("post: afterglow holds the brightest recent colour, fading over its decay") {
    const Rig rig = mandala();
    Scene scene;
    scene.post.push_back({AfterglowParams{1.0, 1.0}, true});
    std::vector<PostState> state;
    Frame lit(rig.pixels.size(), Rgb{1.0f, 0.5f, 0.0f});
    apply_post(scene, rig, at_time(10.0), lit, &state);
    CHECK(brightest(lit) == doctest::Approx(1.0f));
    // Black frames after: the glow decays as exp(-dt / one beat), frame by frame.
    for (int f = 1; f <= 24; ++f) { // 0.4 s at 60 frames a second.
        Frame dark(rig.pixels.size());
        apply_post(scene, rig, at_time(10.0 + f / 60.0), dark, &state);
        if (f == 24) {
            CHECK(dark[0].r == doctest::Approx(std::exp(-1.0)).epsilon(1e-3));
            CHECK(dark[0].g == doctest::Approx(0.5 * std::exp(-1.0)).epsilon(1e-3));
        }
    }
    // Null state: nothing happens; disabled: nothing shows.
    Frame dark(rig.pixels.size());
    apply_post(scene, rig, at_time(11.0), dark, nullptr);
    CHECK(brightest(dark) == 0.0f);
    scene.post[0].enabled = false;
    apply_post(scene, rig, at_time(10.5), dark, &state);
    CHECK(brightest(dark) == 0.0f);
    // A rig of another size starts again from black.
    scene.post[0].enabled = true;
    Frame small_frame(10);
    apply_post(scene, rig, at_time(10.6), small_frame, &state);
    CHECK(brightest(small_frame) == 0.0f);
}

TEST_CASE("post: the radial echo carries the colour outwards") {
    const Rig rig = mandala();
    const std::vector<double> radius = hex_radii(rig, hex_frame(rig));
    Scene scene;
    scene.post.push_back({RadialEchoParams{1.0, 4.0, 1.0, 1.0}, true});
    std::vector<PostState> state;
    // The inner ring lit once (hex radius 1/3), then black.
    Frame first(rig.pixels.size());
    for (size_t i = 0; i < first.size(); ++i) {
        if (radius[i] < 0.4) {
            first[i] = {1.0f, 1.0f, 1.0f};
        }
    }
    apply_post(scene, rig, at_time(5.0), first, &state);
    // Where the echo is brightest, by radius, as time goes on: further out.
    const auto peak_radius = [&](const Frame &f) {
        size_t best = 0;
        for (size_t i = 0; i < f.size(); ++i) {
            if (level(f[i]) > level(f[best])) {
                best = i;
            }
        }
        return radius[best];
    };
    double previous = 0.0;
    for (int f = 1; f <= 9; ++f) {
        Frame dark(rig.pixels.size());
        apply_post(scene, rig, at_time(5.0 + f * 0.02), dark, &state);
        if (f % 3 == 0) {
            const double now = peak_radius(dark);
            CHECK(now >= previous);
            previous = now;
            CHECK(brightest(dark) > 0.1f);
        }
    }
    CHECK(previous > 0.6); // 0.18 s at a reach every 0.4 s: from 1/3 to about 0.78.
}

TEST_CASE("post: 30 and 60 frames a second agree, as the formulas follow time, not frames") {
    const Rig rig = mandala();
    Scene scene;
    scene.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::add));
    HexPulseParams slow;
    slow.trigger = 2; // Bar.
    slow.travel_beats = 4.0;
    slow.width = 0.4;
    scene.layers.push_back(make_layer({slow}, BlendMode::add, 0.5f));
    scene.post.push_back({AfterglowParams{2.0, 0.6}, true});
    scene.post.push_back({RadialEchoParams{0.5, 1.0, 0.5, 0.5}, true});
    const std::vector<Scene> scenes = {scene};
    const std::vector<Palette> palettes = default_palettes();
    const auto run = [&](double fps) {
        PostMemory post;
        Frame last;
        SceneMix mix;
        const auto frames = static_cast<int>(2.0 * fps);
        for (int f = 0; f <= frames; ++f) {
            last = render_mix(scenes, mix, rig, at_time(20.0 + f / fps), palettes, 1, nullptr,
                              nullptr, &post);
        }
        return last;
    };
    const Frame a = run(30.0);
    const Frame b = run(60.0);
    float most = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        most = std::max({most, std::abs(a[i].r - b[i].r), std::abs(a[i].g - b[i].g),
                         std::abs(a[i].b - b[i].b)});
    }
    CHECK(most < 0.02f);
    // Without post memory render_mix is the pure path: no post-effects.
    SceneMix mix;
    const Frame pure = render_mix(scenes, mix, rig, at_time(21.0), palettes, 1);
    Scene bare = scene;
    bare.post.clear();
    CHECK(pure == render_mix({bare}, mix, rig, at_time(21.0), palettes, 1));
}

TEST_CASE("post: post-effects round-trip in the project file; omitted when none") {
    Project p = default_project();
    p.scenes[0].post.push_back({AfterglowParams{2.5, 0.75}, false});
    p.scenes[0].post.push_back({RadialEchoParams{-1.5, 0.5, 0.25, 0.9}, true});
    const std::string text = to_project_json(p).value();
    CHECK(text.find("\"post\"") != std::string::npos);
    const Project q = parse_project_json(text).value();
    REQUIRE(q.scenes[0].post.size() == 2);
    CHECK_FALSE(q.scenes[0].post[0].enabled);
    CHECK(std::get<AfterglowParams>(q.scenes[0].post[0].params).decay_beats == 2.5);
    CHECK(std::get<AfterglowParams>(q.scenes[0].post[0].params).amount == 0.75);
    const auto &e = std::get<RadialEchoParams>(q.scenes[0].post[1].params);
    CHECK(e.speed == -1.5);
    CHECK(e.decay_beats == 0.5);
    CHECK(e.feed == 0.25);
    CHECK(e.mix == 0.9);
    CHECK(q.scenes[1].post.empty());
    CHECK(to_project_json(default_project()).value().find("\"post\"") == std::string::npos);
}
