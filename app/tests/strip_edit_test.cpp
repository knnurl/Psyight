// Strip geometry edits (engine/strip_edit.h): nearest points, moving, inserting and removing
// points, splitting lines and opening rings, moving, rotating, snapping, refilled counts.

#include "engine/strip_edit.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace psyight;

namespace {

StripSpec line_strip() {
    StripSpec s;
    s.name = "Line";
    s.polyline = {{0, 0, 0}, {1, 0, 0}};
    s.pixels_per_m = 60.0;
    s.count = 60;
    return s;
}

StripSpec square_ring() {
    StripSpec s;
    s.name = "Ring";
    s.polyline = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}};
    s.pixels_per_m = 60.0;
    s.count = 240;
    return s;
}

} // namespace

TEST_CASE("strip edit: nearest point, moving points, counts refitted after") {
    const PolylineHit hit = nearest_on_polyline(square_ring().polyline, {1.2, 0.5, 0});
    CHECK(hit.segment == 1);
    CHECK(hit.point.x == doctest::Approx(1.0));
    CHECK(hit.distance_m == doctest::Approx(0.2));

    // The edit leaves the count; fitting fills a strip that fills its path, and keeps one whose
    // count was set by hand.
    std::vector<StripSpec> longer = {with_point_moved(line_strip(), 1, {2, 0, 0})};
    CHECK(longer[0].count == 60);
    fit_counts(longer);
    CHECK(longer[0].count == 120);
    StripSpec part = line_strip();
    part.count = 30;
    part.fills = false;
    std::vector<StripSpec> stretched = {with_point_moved(part, 1, {2, 0, 0})};
    fit_counts(stretched);
    CHECK(stretched[0].count == 30);

    // A ring's first and last points move together.
    const StripSpec ring = with_point_moved(square_ring(), 0, {-0.5, 0, 0});
    CHECK(ring.polyline.back().x == doctest::Approx(-0.5));
    CHECK(is_closed(ring));
}

TEST_CASE("strip edit: insert and remove points; split a line, restart a ring at the cut") {
    StripSpec s = with_point_inserted(line_strip(), 0, {0.5, 0.2, 0});
    REQUIRE(s.polyline.size() == 3);
    CHECK(s.polyline[1].y == doctest::Approx(0.2));
    s = with_point_removed(s, 1);
    CHECK(s.polyline.size() == 2);
    CHECK(with_point_removed(s, 0).polyline.size() == 2); // Never fewer than two.

    const std::vector<StripSpec> halves = split_strip(line_strip(), 0, {0.25, 0, 0});
    REQUIRE(halves.size() == 2);
    CHECK(halves[0].name == "Line a");
    CHECK(halves[0].count == 15);
    CHECK(halves[1].count == 45);
    CHECK(halves[1].polyline.front().x == doctest::Approx(0.25));

    const std::vector<StripSpec> opened = split_strip(square_ring(), 1, {1, 0.5, 0});
    REQUIRE(opened.size() == 1);
    CHECK(is_closed(opened[0])); // Still a ring, now starting (data in) at the cut.
    CHECK(opened[0].polyline.front().y == doctest::Approx(0.5));
    CHECK(opened[0].polyline.back().y == doctest::Approx(0.5));
}

TEST_CASE("strip edit: move, rotate, snap") {
    const StripSpec m = moved(line_strip(), 0.5, 1.0);
    CHECK(m.polyline[0].x == doctest::Approx(0.5));
    CHECK(m.polyline[0].y == doctest::Approx(1.0));
    const StripSpec r = rotated(line_strip(), {0, 0, 0}, 3.14159265358979 / 2.0);
    CHECK(r.polyline[1].x == doctest::Approx(0.0).epsilon(1e-9));
    CHECK(r.polyline[1].y == doctest::Approx(1.0));
    const Vec3 s = snapped({0.123, -0.456, 0.7}, 0.01);
    CHECK(s.x == doctest::Approx(0.12));
    CHECK(s.y == doctest::Approx(-0.46));
    CHECK(s.z == doctest::Approx(0.7));
}
