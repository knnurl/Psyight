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
