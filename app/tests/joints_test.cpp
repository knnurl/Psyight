// Joints (engine/rig.h): pieces at corners, where strips end on others, where they cross; pixel
// layout on the pieces; counts fitted and refitted.

#include "engine/rig.h"
#include "engine/rig_templates.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace psyight;

namespace {

StripSpec strip(const char *name, std::vector<Vec3> points, double per_m, int count) {
    return {name, std::move(points), per_m, count, {}};
}

double gap_m(const Pixel &a, const Pixel &b) {
    return std::hypot(a.position.x - b.position.x, a.position.y - b.position.y,
                      a.position.z - b.position.z);
}

} // namespace

TEST_CASE("joints: a lone straight strip is one piece, laid out as before") {
    const std::vector<StripSpec> one = {strip("s", {{0, 0, 0}, {2.5, 0, 0}}, 60.0, 150)};
    const auto pieces = strip_pieces(one);
    REQUIRE(pieces[0].size() == 1);
    CHECK(pieces[0][0].to_m == doctest::Approx(2.5));
    CHECK(pixels_fitting(pieces[0], 60.0) == 150);
    const Rig rig = build_rig(one);
    REQUIRE(rig.pixels.size() == 150);
    CHECK(rig.pixels[0].position.x == doctest::Approx(0.5 / 60.0));
    CHECK(rig.pixels[149].position.x == doctest::Approx(149.5 / 60.0));
    CHECK(rig.fits[0] == 150);
}

TEST_CASE("joints: sharp corners split a strip; gentle bends do not") {
    // A corner in 3D (5 m, then up 1 m): 8 mm out each side.
    std::vector<StripSpec> bent = {strip("s", {{0, 0, 0}, {3, 4, 0}, {3, 4, 1}}, 30.0, 0)};
    CHECK(strip_pieces(bent)[0].size() == 2);
    fit_counts(bent);
    CHECK(bent[0].count == 149 + 29); // 4.992 m and 0.992 m at 30 px/m.

    // A square ring: four corners, its start among them.
    const std::vector<StripSpec> ring = {
        strip("r", {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}}, 60.0, 0)};
    const auto pieces = strip_pieces(ring)[0];
    REQUIRE(pieces.size() == 4);
    for (const Span &p : pieces) {
        CHECK(p.to_m - p.from_m == doctest::Approx(1.0 - 2 * kJointClearanceM));
    }

    // A 24-sided circle turns 15 degrees at each point: one piece.
    std::vector<Vec3> circle;
    for (int i = 0; i <= 24; ++i) {
        const double a = 2.0 * 3.14159265358979323846 * i / 24;
        circle.push_back({std::cos(a), std::sin(a), 0.0});
    }
    circle.back() = circle.front();
    CHECK(strip_pieces({strip("c", circle, 60.0, 0)})[0].size() == 1);
}

TEST_CASE("joints: an end on another strip stops short; ends that meet both do") {
    // A T: the stem ends on the bar's middle.
    const std::vector<StripSpec> t = {strip("bar", {{-1, 0, 0}, {1, 0, 0}}, 60.0, 0),
                                      strip("stem", {{0, 1, 0}, {0, 0, 0}}, 60.0, 0)};
    const auto pieces = strip_pieces(t);
    CHECK(pieces[0].size() == 1); // The bar runs on.
    REQUIRE(pieces[1].size() == 1);
    CHECK(pieces[1][0].to_m == doctest::Approx(1.0 - kJointClearanceM));
    // Two ends meeting in line: both stop short.
    const std::vector<StripSpec> line = {strip("a", {{0, 0, 0}, {1, 0, 0}}, 60.0, 0),
                                         strip("b", {{1, 0, 0}, {2, 0, 0}}, 60.0, 0)};
    const auto ends = strip_pieces(line);
    CHECK(ends[0].back().to_m == doctest::Approx(1.0 - kJointClearanceM));
    CHECK(ends[1].front().from_m == doctest::Approx(kJointClearanceM));
}

TEST_CASE("joints: at a crossing the open strip runs through and the ring is cut") {
    const std::vector<StripSpec> cross = {
        strip("ring", {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}, {-1, -1, 0}}, 60.0, 0),
        strip("spoke", {{0, -2, 0}, {0, 2, 0}}, 60.0, 0)};
    const auto pieces = strip_pieces(cross);
    CHECK(pieces[1].size() == 1); // The spoke runs through both sides of the ring.
    CHECK(pieces[0].size() == 6); // Four corners, and a cut where the spoke crosses each side.
}

TEST_CASE("joints: the owner's 30 cm hexagon with its centre Y keeps every pixel clear") {
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name != "Hexagon mandala") {
            continue;
        }
        std::vector<double> v;
        for (const RigPresetParam &q : p.params) {
            v.push_back(q.id == "radius_m"   ? 0.3
                        : q.id == "scale"    ? 0.6736
                        : q.id == "density"  ? 2.4
                        : q.id == "centre_y" ? 1.0
                                             : q.value);
        }
        const std::vector<StripSpec> strips = build_rig_preset(p, v);
        REQUIRE(strips.size() == 12);
        const Rig rig = build_rig(strips);
        double nearest = 1.0;
        for (size_t a = 0; a < rig.pixels.size(); ++a) {
            for (size_t b = a + 1; b < rig.pixels.size(); ++b) {
                if (rig.pixels[a].strip != rig.pixels[b].strip) {
                    nearest = std::min(nearest, gap_m(rig.pixels[a], rig.pixels[b]));
                }
            }
        }
        MESSAGE("pixels " << rig.pixels.size() << ", nearest of two strips " << nearest * 1000
                          << " mm");
        CHECK(nearest >= kJointClearanceM - 1e-6);
        // Counts fill the pieces exactly: no pixel past a piece.
        for (size_t i = 0; i < strips.size(); ++i) {
            CHECK(strips[i].count == rig.fits[i]);
        }
        // The spokes run through the middle hexagon (one piece each); it is cut at all six.
        const auto pieces = strip_pieces(strips);
        CHECK(pieces[1].size() == 6);
        for (size_t i = 3; i < 9; ++i) {
            CHECK(pieces[i].size() == 1);
            // Centred on its piece, which reaches into the outer ring's corner gap: it stops the
            // clearance short of the corner at both ends, as the ring's pieces do.
            std::vector<Pixel> on;
            for (const Pixel &px : rig.pixels) {
                if (px.strip == static_cast<int>(i)) {
                    on.push_back(px);
                }
            }
            REQUIRE(on.size() >= 2);
            const Vec3 a = strips[i].polyline.front();
            const Vec3 b = strips[i].polyline.back();
            const double to_start =
                std::hypot(on.front().position.x - a.x, on.front().position.y - a.y);
            const double to_end =
                std::hypot(on.back().position.x - b.x, on.back().position.y - b.y);
            const double length = std::hypot(b.x - a.x, b.y - a.y);
            const Span piece = pieces[i][0];
            // At the inner corner: short of a centre spoke's end (spokes 1, 3 and 5), or from the
            // corner itself (the inner ring's edges lead away, its corner cut out).
            const bool meets_centre = (i - 3) % 2 == 0;
            CHECK(piece.from_m == doctest::Approx(meets_centre ? kJointClearanceM : 0.0));
            CHECK(length - piece.to_m == doctest::Approx(kJointClearanceM).epsilon(1e-6));
            if (i == 3) { // Spoke 1 meets the outer hexagon's first corner, its start.
                CHECK(pieces[0].front().from_m == doctest::Approx(kJointClearanceM));
            }
            CHECK(to_start - piece.from_m == doctest::Approx(piece.to_m - (length - to_end)));
            CHECK(to_start - piece.from_m >= 0.5 / strips[i].pixels_per_m - 1e-9);
        }
    }
}

TEST_CASE("joints: refitting keeps counts set by hand and fills strips that were full") {
    const StripSpec bar = strip("bar", {{-1, 0, 0}, {1, 0, 0}}, 60.0, 120);
    StripSpec stem = strip("stem", {{0, 1, 0}, {0, 0.5, 0}}, 60.0, 30);
    std::vector<StripSpec> before = {bar, stem};
    // The stem lengthened down onto the bar: full before, so it fills its piece now.
    std::vector<StripSpec> after = before;
    after[1].polyline.back() = {0, 0, 0};
    refit_counts(after, before);
    CHECK(after[1].count == 59); // 1 m less 8 mm at 60 px/m.
    CHECK(after[0].count == 120);
    // A count set by hand in the same edit stands.
    after = before;
    after[1].polyline.back() = {0, 0, 0};
    after[1].count = 10;
    refit_counts(after, before);
    CHECK(after[1].count == 10);
    // A new strip whose count fills its path is fitted.
    after = before;
    after.push_back(strip("new", {{-1, 0, 0}, {-1, 1, 0}}, 60.0, 60));
    refit_counts(after, before);
    CHECK(after[2].count == 59);
}
