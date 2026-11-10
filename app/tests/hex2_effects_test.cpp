// The hexagon-visuals effects (docs/PLAN_HEX_VISUALS.md phases 5 to 9) and the hexagon topology
// they share (engine/space.h hex_topology), on the hexagon mandala preset.

#include "effect_test_helpers.h"
#include "engine/shape.h"
#include "engine/space.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace psyight;
using test::grid_at;
using test::level;
using test::mandala;
using test::pixels_of;
using test::render;

namespace {

// Circular distance in turns.
double turn_distance(double a, double b) {
    const double d = std::fabs(a - b);
    return std::min(d, 1.0 - d);
}

} // namespace

TEST_CASE("hex topology: the mandala's rings innermost first, its spokes, and the corners") {
    const Rig rig = mandala();
    const HexTopology t = hex_topology(rig);
    // Strips: outer, middle, inner hexagon, then six spokes.
    REQUIRE(t.rings == std::vector<int>{2, 1, 0});
    CHECK(t.spokes == std::vector<int>{3, 4, 5, 6, 7, 8});
    CHECK(t.ring_of_strip[0] == 2);
    CHECK(t.ring_of_strip[2] == 0);
    CHECK(t.ring_of_strip[3] == -1);
    CHECK(t.u_min == doctest::Approx(1.0 / 3.0).epsilon(0.05));
    REQUIRE(t.radius.size() == rig.pixels.size());
    for (size_t s = 0; s < rig.strips.size(); ++s) {
        CHECK(rig.pixels[static_cast<size_t>(t.first_pixel[s])].strip == static_cast<int>(s));
        CHECK(rig.pixels[static_cast<size_t>(t.first_pixel[s])].index == 0);
    }
    // The outer-ring pixel nearest each corner's turn is the one nearest the polyline's corner.
    const std::vector<size_t> outer = pixels_of(rig, 0);
    const std::vector<Vec3> &corners = rig.strips[0].polyline;
    for (int k = 0; k < 6; ++k) {
        CAPTURE(k);
        const double at = corner_turn(t.frame, k);
        const size_t by_turn =
            *std::min_element(outer.begin(), outer.end(), [&](size_t a, size_t b) {
                return turn_distance(t.turn[a], at) < turn_distance(t.turn[b], at);
            });
        const Vec3 corner =
            *std::min_element(corners.begin(), corners.end(), [&](const Vec3 &a, const Vec3 &b) {
                return turn_distance(hex_turn(t.frame, a.x, a.y), at) <
                       turn_distance(hex_turn(t.frame, b.x, b.y), at);
            });
        CHECK(turn_distance(hex_turn(t.frame, corner.x, corner.y), at) < 1e-6);
        const size_t by_place =
            *std::min_element(outer.begin(), outer.end(), [&](size_t a, size_t b) {
                const Vec3 &p = rig.pixels[a].position;
                const Vec3 &q = rig.pixels[b].position;
                return std::hypot(p.x - corner.x, p.y - corner.y) <
                       std::hypot(q.x - corner.x, q.y - corner.y);
            });
        // The same pixel, or its neighbour equally near (a corner's joint gap leaves a pair).
        const auto distance = [&](size_t i) {
            return std::hypot(rig.pixels[i].position.x - corner.x,
                              rig.pixels[i].position.y - corner.y);
        };
        CHECK(distance(by_turn) <= distance(by_place) + 1e-3);
    }
}

TEST_CASE("hex topology: a rig without closed strips has no rings") {
    const HexTopology t = hex_topology(default_rig());
    CHECK(t.rings.empty());
    CHECK(t.spokes.size() == default_rig().strips.size());
}
