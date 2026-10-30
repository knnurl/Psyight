// The aurora (engine/aurora.h) and the show renderer's Ambient step drawing it.

#include "engine/aurora.h"
#include "engine/rig_templates.h"
#include "output/show.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

using namespace psyight;

namespace {

Rig mandala() {
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name == "Hexagon mandala") {
            return build_rig(p.strips);
        }
    }
    FAIL("no hexagon mandala");
    return {};
}

float level(const Rgb &c) { return std::max({c.r, c.g, c.b}); }

} // namespace

TEST_CASE("aurora: deterministic, moving, soft, and every strip lit over time") {
    const Rig rig = mandala();
    const Frame a = render_aurora(rig, 12.0, 1);
    CHECK(a == render_aurora(rig, 12.0, 1));
    CHECK(a != render_aurora(rig, 12.0, 2));
    CHECK(a != render_aurora(rig, 12.5, 1)); // Drifting.

    std::vector<float> brightest(rig.strips.size(), 0.0f);
    int lit = 0;
    int pixels = 0;
    for (double t = 0.0; t < 30.0; t += 1.0) {
        const Frame f = render_aurora(rig, t, 1);
        for (size_t i = 0; i < f.size(); ++i) {
            const Rgb &c = f[i];
            CHECK(level(c) <= 1.0f);
            // Aurora colours: never white (the smallest channel well below the largest).
            if (level(c) > 0.2f) {
                CHECK(std::min({c.r, c.g, c.b}) < 0.6f * level(c));
            }
            const auto strip = static_cast<size_t>(rig.pixels[i].strip);
            brightest[strip] = std::max(brightest[strip], level(c));
            lit += level(c) > 0.05f ? 1 : 0;
            ++pixels;
        }
    }
    for (size_t s = 0; s < brightest.size(); ++s) {
        CAPTURE(s);
        CHECK(brightest[s] > 0.3f);
    }
    // Waves with dark between them, not a wash.
    CAPTURE(lit);
    CHECK(lit > pixels / 5);
    CHECK(lit < pixels * 9 / 10);
}

TEST_CASE("show renderer: Ambient draws the aurora with no track, moving while stopped") {
    ShowConfig config;
    config.ambient = true;
    config.limiter.flash_limiter = false;
    ShowRenderer renderer;
    const ShowFrame first = renderer.render(config, 0.0);
    CHECK(first.section == "ambient");
    ShowFrame later;
    for (int k = 0; k < 120; ++k) { // Two seconds of frames, the transport at 0.
        later = renderer.render(config, 0.0);
    }
    CHECK(later.frame != first.frame);
    CHECK(std::any_of(later.frame.begin(), later.frame.end(),
                      [](const Rgb &c) { return level(c) > 0.05f; }));
    config.blackout = true;
    for (const Rgb &c : renderer.render(config, 0.0).frame) {
        CHECK(level(c) == 0.0f);
    }
}
