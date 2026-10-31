// Test patterns (output/test_patterns.h) and the show renderer drawing them.

#include "engine/rig_templates.h"
#include "output/eye_candy.h"
#include "output/show.h"
#include "output/test_patterns.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace psyight;

namespace {

// Two pillars of 10 pixels (6 px/m over 1.7 m: floor(10.2)).
Rig two_strips() {
    PillarsTemplate t;
    t.count = 2;
    t.height_m = 1.7;
    t.pixels_per_m = 6.0;
    return build_rig(pillars_strips(t));
}

bool lit(const Rgb &c) { return c.r > 0.0f || c.g > 0.0f || c.b > 0.0f; }

} // namespace

TEST_CASE("test patterns: the chase runs from each strip's first pixel") {
    const Rig rig = two_strips();
    REQUIRE(rig.pixels.size() == 20);
    const Patch patch = build_patch(rig);
    TestPattern t;
    t.kind = TestPatternKind::chase;
    t.chase_pixels_per_s = 10.0;
    t.chase_length = 3;
    // At 0.25 s the head is at 2.5: pixels 0 to 2 of both strips lit, the head brightest.
    Frame f = render_test_pattern(t, rig, patch, 0.25);
    for (int strip = 0; strip < 2; ++strip) {
        const size_t base = static_cast<size_t>(strip) * 10;
        CHECK(lit(f[base + 0]));
        CHECK(lit(f[base + 2]));
        CHECK_FALSE(lit(f[base + 3]));
        CHECK(std::max({f[base + 2].r, f[base + 2].g, f[base + 2].b}) >
              std::max({f[base + 0].r, f[base + 0].g, f[base + 0].b}));
    }
    // Later the head has moved along.
    f = render_test_pattern(t, rig, patch, 0.75);
    CHECK_FALSE(lit(f[0]));
    CHECK(lit(f[7]));
    // One strip only.
    t.strip = 1;
    f = render_test_pattern(t, rig, patch, 0.25);
    CHECK_FALSE(lit(f[0]));
    CHECK(lit(f[10]));
}

TEST_CASE("test patterns: colour order steps red, green, blue") {
    const Rig rig = two_strips();
    const Patch patch = build_patch(rig);
    TestPattern t;
    t.kind = TestPatternKind::colour_order;
    t.level = 1.0;
    CHECK(colour_order_step(0.5) == 0);
    CHECK(colour_order_step(1.5) == 1);
    CHECK(colour_order_step(2.5) == 2);
    CHECK(colour_order_step(3.5) == 0);
    const Frame red = render_test_pattern(t, rig, patch, 0.5);
    const Frame blue = render_test_pattern(t, rig, patch, 2.5);
    CHECK(red[3].r == 1.0f);
    CHECK(red[3].g == 0.0f);
    CHECK(blue[19].b == 1.0f);
    CHECK(blue[19].r == 0.0f);
}

TEST_CASE("test patterns: identify lights one universe's pixels only") {
    const Rig rig = two_strips();
    const Patch patch = build_patch(rig, 5, 15); // 15 pixels a universe: 5 and 6.
    REQUIRE(patch.universes == std::vector<int>{5, 6});
    TestPattern t;
    t.kind = TestPatternKind::identify_universe;
    t.universe = 6;
    const Frame f = render_test_pattern(t, rig, patch, 0.0);
    for (size_t i = 0; i < f.size(); ++i) {
        CAPTURE(i);
        CHECK(lit(f[i]) == (i >= 15));
    }
}

TEST_CASE("test patterns: eye candy's spiral lights every pixel in many colours, and moves") {
    const Rig rig = two_strips();
    const Patch patch = build_patch(rig);
    TestPattern t;
    t.kind = TestPatternKind::eye_candy;
    t.candy = 7;
    REQUIRE(std::string(eye_candy_name(t.candy)) == "Spiral");
    const Frame f = render_test_pattern(t, rig, patch, 3.0);
    float darkest = 1.0f;
    float reddest = 0.0f;
    float bluest = 0.0f;
    for (const Rgb &c : f) {
        darkest = std::min(darkest, std::max({c.r, c.g, c.b}));
        CHECK(std::max({c.r, c.g, c.b}) <= t.level + 1e-6f);
        reddest = std::max(reddest, c.r - std::max(c.g, c.b));
        bluest = std::max(bluest, c.b - std::max(c.r, c.g));
    }
    CHECK(darkest > 0.1f); // Every pixel lit (the rings dim it to a quarter at least).
    CHECK(reddest > 0.1f);
    CHECK(bluest > 0.1f);
    CHECK(f != render_test_pattern(t, rig, patch, 3.5));
    CHECK(f == render_test_pattern(t, rig, patch, 3.0));
}

TEST_CASE("test patterns: every eye candy show lights the hexagon, moves, and keeps the level") {
    Rig rig;
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name == "Hexagon mandala") {
            rig = build_rig(p.strips);
        }
    }
    REQUIRE(!rig.pixels.empty());
    const Patch patch = build_patch(rig);
    TestPattern t;
    t.kind = TestPatternKind::eye_candy;
    t.level = 0.6;
    for (int show = 0; show < kEyeCandyCount; ++show) {
        t.candy = show;
        const std::string name = eye_candy_name(show);
        CAPTURE(name);
        int lit_frames = 0;
        for (const double s : {1.0, 2.5, 4.0, 7.0, 9.5}) {
            const Frame f = render_test_pattern(t, rig, patch, s);
            CHECK(f == render_test_pattern(t, rig, patch, s)); // Deterministic.
            CAPTURE(s);
            CHECK(f != render_test_pattern(t, rig, patch, s + 0.25)); // Moving.
            float most = 0.0f;
            for (const Rgb &c : f) {
                CHECK(std::isfinite(c.r));
                most = std::max({most, c.r, c.g, c.b});
            }
            CHECK(most <= 0.6f + 1e-5f);
            lit_frames += most > 0.1f ? 1 : 0;
        }
        CHECK(lit_frames >= 4);
    }
}

TEST_CASE("show renderer: a test pattern replaces the scenes, through the limiters") {
    ShowConfig config;
    config.rig = two_strips();
    config.patch = build_patch(config.rig);
    config.limiter.brightness_cap = 0.5;
    config.limiter.flash_limiter = false;
    config.test.kind = TestPatternKind::all_on;
    config.test.send = false; // Only the virtual node.
    config.version = 1;
    ShowRenderer renderer;
    const ShowFrame frame = renderer.render(config, 0.0); // No track: the pattern still shows.
    REQUIRE(frame.frame.size() == 20);
    CHECK(frame.frame[0].r == doctest::Approx(0.5f)); // Capped.
    CHECK(frame.section == "test");
    config.blackout = true; // Blackout wins.
    config.version = 2;
    CHECK_FALSE(lit(renderer.render(config, 1.0 / 60.0).frame[0]));
}

TEST_CASE("show renderer: eye candy's speed runs its clock, without a jump when it changes") {
    ShowConfig config;
    config.rig = two_strips();
    config.patch = build_patch(config.rig);
    config.limiter.flash_limiter = false;
    config.test.kind = TestPatternKind::eye_candy;
    config.test.candy = 7; // Spiral.
    // Twice the speed for 61 frames reaches the show's 2 s; at 1x that takes 121 frames.
    const auto run = [&](ShowRenderer &r, double speed, int frames) {
        config.test.candy_speed = speed;
        ShowFrame f;
        for (int k = 0; k < frames; ++k) {
            f = r.render(config, 0.0);
        }
        return f.frame;
    };
    const auto close = [](const Frame &a, const Frame &b) { // Clocks summed frame by frame.
        float most = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) {
            most = std::max({most, std::abs(a[i].r - b[i].r), std::abs(a[i].g - b[i].g),
                             std::abs(a[i].b - b[i].b)});
        }
        return most;
    };
    ShowRenderer fast;
    ShowRenderer steady;
    CHECK(close(run(fast, 2.0, 61), run(steady, 1.0, 121)) < 1e-4f);
    CHECK(close(run(fast, 2.0, 1), run(steady, 1.0, 1)) > 1e-3f); // Each at its own pace.
    // A new speed carries on from where the show is: 2 s plus a frame, then half speed.
    ShowRenderer changed;
    ShowRenderer reference;
    run(changed, 2.0, 61);
    const Frame after = run(changed, 0.5, 3); // Clock 2 + 2/60, then + 0.5/60 twice.
    CHECK(close(after, run(reference, 1.0, 121 + 2 + 1)) < 1e-4f); // 2 + 3/60 at 1x.
}

TEST_CASE("show renderer: a track's first second fades up from black") {
    ShowConfig config;
    config.limiter.flash_limiter = false;
    TrackAnalysis analysis;
    analysis.sample_rate_hz = 48000;
    analysis.duration_s = 30.0;
    analysis.grid = {150.0, 0, 48000};
    analysis.sections = {{0.0, 30.0, "intro", 1.0}};
    config.analysis = std::make_shared<const TrackAnalysis>(analysis);
    config.grid = analysis.grid;
    const auto brightest = [&](double t) {
        ShowRenderer renderer;
        float most = 0.0f;
        for (const Rgb &c : renderer.render(config, t).frame) {
            most = std::max({most, c.r, c.g, c.b});
        }
        return most;
    };
    CHECK(brightest(0.0) == 0.0f);
    CHECK(brightest(0.5) > 0.0f);
    CHECK(brightest(0.5) < brightest(1.5));
}

TEST_CASE("show renderer: the chase carries on from where it is when its speed changes") {
    ShowConfig config;
    config.rig = two_strips();
    config.patch = build_patch(config.rig);
    config.limiter.flash_limiter = false;
    config.test.kind = TestPatternKind::chase;
    config.test.chase_length = 1;
    config.test.chase_pixels_per_s = 60.0; // A pixel a frame.
    const auto head = [](const Frame &f) { // The lit pixel of strip 0.
        for (size_t i = 0; i < 10; ++i) {
            if (f[i].r + f[i].g + f[i].b > 0.0f) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    ShowRenderer renderer;
    ShowFrame f;
    for (int k = 0; k < 5; ++k) { // Frames at 0, 1, 2, 3, 4 pixels.
        f = renderer.render(config, 0.0);
    }
    CHECK(head(f.frame) == 4);
    config.test.chase_pixels_per_s = 30.0; // Half speed: on from 5, half a pixel a frame.
    f = renderer.render(config, 0.0);
    CHECK(head(f.frame) == 5);
    f = renderer.render(config, 0.0);
    CHECK(head(f.frame) == 5);
    f = renderer.render(config, 0.0);
    CHECK(head(f.frame) == 6);
}
