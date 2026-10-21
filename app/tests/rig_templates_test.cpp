// Rig templates (engine/rig_templates.h): geometry, pixel counts, wiring direction and groups.

#include "engine/rig_templates.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using namespace psyight;

namespace {

bool has(const StripSpec &s, const char *group) {
    return std::find(s.groups.begin(), s.groups.end(), group) != s.groups.end();
}

} // namespace

TEST_CASE("rig templates: pillars in a row, centred, bottom to top or serpentine") {
    PillarsTemplate t;
    t.count = 5;
    t.height_m = 2.0;
    t.base_m = 0.3;
    t.spacing_m = 1.5;
    t.pixels_per_m = 60.0;
    std::vector<StripSpec> strips = pillars_strips(t);
    REQUIRE(strips.size() == 5);
    CHECK(strips[0].name == "Pillar 1");
    CHECK(strips[0].polyline.front().x == doctest::Approx(-3.0));
    CHECK(strips[4].polyline.front().x == doctest::Approx(3.0));
    CHECK(strips[1].polyline.front().y == doctest::Approx(0.3)); // Bottom first.
    CHECK(strips[1].polyline.back().y == doctest::Approx(2.3));
    CHECK(strips[0].count == 120);
    CHECK(has(strips[0], "pillars"));
    CHECK(has(strips[0], "left"));
    CHECK_FALSE(has(strips[0], "right"));
    CHECK(has(strips[2], "left")); // The middle one is both.
    CHECK(has(strips[2], "right"));
    CHECK(has(strips[2], "even"));
    CHECK(has(strips[3], "odd"));
    // The rig builds: 5 strips of 120 pixels.
    const Rig rig = build_rig(strips);
    CHECK(rig.pixels.size() == 600);

    t.serpentine = true;
    strips = pillars_strips(t);
    CHECK(strips[0].polyline.front().y == doctest::Approx(0.3));
    CHECK(strips[1].polyline.front().y == doctest::Approx(2.3)); // Top first.
}

TEST_CASE("rig templates: pillars around a circle keep their radius") {
    PillarsTemplate t;
    t.count = 8;
    t.circle = true;
    t.radius_m = 3.0;
    const std::vector<StripSpec> strips = pillars_strips(t);
    REQUIRE(strips.size() == 8);
    for (const StripSpec &s : strips) {
        const Vec3 &p = s.polyline.front();
        CHECK(std::sqrt(p.x * p.x + p.z * p.z) == doctest::Approx(3.0));
    }
}

TEST_CASE("rig templates: a matrix panel zigzags row by row") {
    MatrixTemplate t;
    t.rows = 4;
    t.width_m = 1.0;
    t.row_spacing_m = 0.1;
    t.base_m = 1.0;
    t.pixels_per_m = 30.0;
    const std::vector<StripSpec> strips = matrix_strips(t);
    REQUIRE(strips.size() == 4);
    CHECK(strips[0].count == 30);
    CHECK(strips[0].polyline.front().x == doctest::Approx(-0.5)); // Left to right.
    CHECK(strips[1].polyline.front().x == doctest::Approx(0.5));  // Right to left.
    CHECK(strips[3].polyline.front().y == doctest::Approx(1.3));
    CHECK(has(strips[0], "bottom"));
    CHECK(has(strips[3], "top"));
    CHECK(has(strips[1], "odd"));
    CHECK(has(strips[2], "matrix"));
    const Rig rig = build_rig(strips);
    CHECK(rig.pixels.size() == 120);
}

TEST_CASE("rig templates: counts are clamped") {
    PillarsTemplate p;
    p.count = 0;
    CHECK(pillars_strips(p).size() == 1);
    MatrixTemplate m;
    m.rows = 10000;
    CHECK(matrix_strips(m).size() == static_cast<size_t>(kMaxTemplateStrips));
}

TEST_CASE("rig presets: named, within the pixel budget, strips fit their polylines") {
    const std::vector<RigPreset> presets = builtin_rig_presets();
    REQUIRE(presets.size() >= 8);
    for (size_t i = 0; i < presets.size(); ++i) {
        const RigPreset &p = presets[i];
        CAPTURE(p.name);
        CHECK_FALSE(p.name.empty());
        CHECK_FALSE(p.description.empty());
        for (size_t j = i + 1; j < presets.size(); ++j) {
            CHECK(p.name != presets[j].name);
        }
        REQUIRE_FALSE(p.strips.empty());
        for (const StripSpec &s : p.strips) {
            CAPTURE(s.name);
            REQUIRE(s.polyline.size() >= 2);
            CHECK(s.count > 0);
            double length_m = 0.0;
            for (size_t k = 1; k < s.polyline.size(); ++k) {
                const double dx = s.polyline[k].x - s.polyline[k - 1].x;
                const double dy = s.polyline[k].y - s.polyline[k - 1].y;
                const double dz = s.polyline[k].z - s.polyline[k - 1].z;
                length_m += std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            CHECK(s.count <= length_m * s.pixels_per_m + 1e-6); // Never past the polyline's end.
            CHECK((has(s, "left") || has(s, "right")));
        }
        const Rig rig = build_rig(p.strips);
        CHECK(rig.pixels.size() <= 1500); // SPEC 2.1.
    }
}

TEST_CASE("rig presets: mirrored halves run from the centre outwards, closed shapes close") {
    const std::vector<RigPreset> presets = builtin_rig_presets();
    const auto find = [&](const char *name) -> const RigPreset & {
        const auto it = std::find_if(presets.begin(), presets.end(),
                                     [&](const RigPreset &p) { return p.name == name; });
        REQUIRE(it != presets.end());
        return *it;
    };

    const RigPreset &chevron = find("DJ booth chevron");
    REQUIRE(chevron.strips.size() == 3);
    const StripSpec &left_wing = chevron.strips[0];
    const StripSpec &right_wing = chevron.strips[1];
    CHECK(left_wing.polyline.front().x == doctest::Approx(0.0)); // From the apex.
    CHECK(left_wing.polyline.back().x == doctest::Approx(-right_wing.polyline.back().x));
    CHECK(left_wing.count == right_wing.count);
    CHECK(has(left_wing, "left"));
    CHECK_FALSE(has(left_wing, "right"));
    CHECK(has(chevron.strips[2], "left")); // The centred booth front is both.
    CHECK(has(chevron.strips[2], "right"));
    CHECK(build_rig(chevron.strips).pixels.size() == 408);

    const RigPreset &hexagon = find("Hexagon mandala");
    const StripSpec &outer = hexagon.strips[0];
    CHECK(outer.polyline.front().x == doctest::Approx(outer.polyline.back().x));
    CHECK(outer.polyline.front().y == doctest::Approx(outer.polyline.back().y));
    CHECK(outer.count == 432); // Six 1.2 m sides at 60 px/m.
    CHECK(build_rig(hexagon.strips).pixels.size() == 1152);

    const RigPreset &tunnel = find("Triple arch tunnel");
    REQUIRE(tunnel.strips.size() == 6);
    for (size_t i = 0; i < tunnel.strips.size(); i += 2) {
        const StripSpec &l = tunnel.strips[i];
        const StripSpec &r = tunnel.strips[i + 1];
        CHECK(l.polyline.front().y == doctest::Approx(0.0)); // From the floor...
        CHECK(l.polyline.back().x == doctest::Approx(0.0));  // ...to the apex.
        CHECK(r.polyline.back().x == doctest::Approx(0.0));
        CHECK(l.count == r.count);
    }
}

TEST_CASE("rig presets: hive shapes keep their structure") {
    const std::vector<RigPreset> presets = builtin_rig_presets();
    const auto find = [&](const char *name) -> const RigPreset & {
        const auto it = std::find_if(presets.begin(), presets.end(),
                                     [&](const RigPreset &p) { return p.name == name; });
        REQUIRE(it != presets.end());
        return *it;
    };
    const auto count_in = [](const RigPreset &p, const char *group) {
        return std::count_if(p.strips.begin(), p.strips.end(),
                             [&](const StripSpec &s) { return has(s, group); });
    };

    // A hive of 19 cells: one core, six around it, twelve outside.
    const RigPreset &comb = find("Honeycomb");
    CHECK(comb.strips.size() == 19);
    CHECK(count_in(comb, "core") == 1);
    CHECK(count_in(comb, "ring1") == 6);
    CHECK(count_in(comb, "ring2") == 12);

    // Portal halves mirror each other and run from the bottom tip to the top.
    const RigPreset &portal = find("Hive portal");
    REQUIRE(portal.strips.size() == 6);
    for (size_t i = 0; i < portal.strips.size(); i += 2) {
        const StripSpec &l = portal.strips[i];
        const StripSpec &r = portal.strips[i + 1];
        CHECK(l.polyline.front().y < l.polyline.back().y);
        CHECK(r.polyline.front().y < r.polyline.back().y);
        CHECK(l.polyline[8].x == doctest::Approx(-r.polyline[8].x)); // Widest point, mirrored.
        CHECK(l.polyline[8].x < 0.0);
        CHECK(l.count == r.count);
    }

    // Every branch of the tree starts where its parent ends.
    const RigPreset &tree = find("Fractal tree");
    REQUIRE(tree.strips.size() == 31);
    CHECK(tree.strips[0].polyline.front().y == doctest::Approx(0.0));
    for (size_t i = 1; i < tree.strips.size(); ++i) {
        const Vec3 &start = tree.strips[i].polyline.front();
        const bool attached =
            std::any_of(tree.strips.begin(), tree.strips.begin() + static_cast<std::ptrdiff_t>(i),
                        [&](const StripSpec &parent) {
                            return std::hypot(parent.polyline.back().x - start.x,
                                              parent.polyline.back().y - start.y) < 1e-9;
                        });
        CHECK(attached);
    }

    // Vortex arms run from the centre outwards.
    const RigPreset &vortex = find("Vortex");
    REQUIRE(vortex.strips.size() == 6);
    for (const StripSpec &arm : vortex.strips) {
        const auto radius = [](const Vec3 &p) { return std::hypot(p.x, p.y - 1.9); };
        CHECK(radius(arm.polyline.front()) == doctest::Approx(0.15));
        CHECK(radius(arm.polyline.back()) == doctest::Approx(1.6));
    }

    // Closed shapes end where they start: petals, the pupil.
    for (const StripSpec &s : find("Lotus").strips) {
        CAPTURE(s.name);
        CHECK(s.polyline.front().x == doctest::Approx(s.polyline.back().x));
        CHECK(s.polyline.front().y == doctest::Approx(s.polyline.back().y));
    }
}
