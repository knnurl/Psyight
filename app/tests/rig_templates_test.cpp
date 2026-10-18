// Rig templates (engine/rig_templates.h): geometry, pixel counts, wiring direction and groups.

#include "engine/rig_templates.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
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
