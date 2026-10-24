// A rig from an SVG drawing (io/svg_rig.h): units, placement, groups from layers, path commands,
// transforms and what is left out.

#include "io/svg_rig.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr double kPi = 3.14159265358979323846;

bool has(const StripSpec &s, const char *group) {
    return std::find(s.groups.begin(), s.groups.end(), group) != s.groups.end();
}

double length_m(const StripSpec &s) {
    double length = 0.0;
    for (size_t i = 1; i < s.polyline.size(); ++i) {
        length += std::hypot(s.polyline[i].x - s.polyline[i - 1].x,
                             s.polyline[i].y - s.polyline[i - 1].y);
    }
    return length;
}

// A 140 by 75 mm document (Inkscape's usual header) holding `body`.
std::string drawing(const std::string &body) {
    return R"svg(<?xml version="1.0" encoding="UTF-8"?>
<!-- Made in a test -->
<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
     width="140mm" height="75mm" viewBox="0 0 140 75">
  <defs><path id="hidden-in-defs" d="M 0 0 L 100 0"/></defs>
)svg" + body +
           "\n</svg>\n";
}

const StripSpec *find(const SvgRig &rig, const char *name) {
    for (const StripSpec &s : rig.strips) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("svg rig: real units, centred, bottom on base, y up, drawn direction, layer groups") {
    const std::string svg = drawing(R"svg(
  <g inkscape:groupmode="layer" inkscape:label="Contour">
    <path id="bottom" d="M 0 75 L 140 75"/>
    <path id="top-left" d="M 70 0 L 0 0"/>
  </g>)svg");
    const Result<SvgRig> made = rig_from_svg(svg, {});
    REQUIRE(made);
    const SvgRig &rig = made.value();
    CHECK(rig.width_m == doctest::Approx(0.14));
    CHECK(rig.height_m == doctest::Approx(0.075));
    REQUIRE(rig.strips.size() == 2);
    const StripSpec &bottom = *find(rig, "bottom");
    CHECK(bottom.polyline.front().x == doctest::Approx(-0.07)); // Drawn left to right.
    CHECK(bottom.polyline.back().x == doctest::Approx(0.07));
    CHECK(bottom.polyline.front().y == doctest::Approx(1.0)); // The bottom, on base_m.
    CHECK(bottom.count == 8);                                 // 0.14 m at 60 px/m.
    CHECK(has(bottom, "contour"));
    CHECK(has(bottom, "left"));
    CHECK(has(bottom, "right"));
    const StripSpec &top = *find(rig, "top-left");
    CHECK(top.polyline.front().x == doctest::Approx(0.0)); // From the middle outwards.
    CHECK(top.polyline.front().y == doctest::Approx(1.075));
    CHECK(has(top, "left"));
    CHECK_FALSE(has(top, "right"));

    // Width scales the whole drawing; density sets the pixels.
    const Result<SvgRig> wide = rig_from_svg(svg, {2.8, 0.5, 30.0});
    REQUIRE(wide);
    CHECK(wide.value().natural_width_m == doctest::Approx(0.14));
    CHECK(wide.value().width_m == doctest::Approx(2.8));
    CHECK(find(wide.value(), "bottom")->count == 84); // 2.8 m at 30 px/m.
    CHECK(find(wide.value(), "bottom")->polyline.front().y == doctest::Approx(0.5));
}

TEST_CASE("svg rig: shapes, curves, arcs, relative commands and transforms") {
    const std::string svg = drawing(R"svg(
  <g inkscape:label="Rings" transform="translate(20,0)">
    <circle id="ring" cx="10" cy="37.5" r="20"/>
  </g>
  <g inkscape:label="Ornaments">
    <g inkscape:label="Gems"><rect id="box" x="100" y="10" width="20" height="10"/></g>
    <path inkscape:label="Bow" d="M 60 60 A 10 10 0 0 1 80 60"/>
    <path id="tri" d="m 60 20 10 0 0 10 z"/>
    <path id="wave" d="M 0 70 C 10 60 20 80 30 70 S 50 60 60 70"/>
    <polyline id="zig" points="0,5 10,15 20,5"/>
    <line id="tick" x1="135" y1="0" x2="135" y2="10" transform="rotate(90 135 5)"/>
  </g>)svg");
    const Result<SvgRig> made = rig_from_svg(svg, {0.0, 1.0, 1000.0});
    REQUIRE(made);
    const SvgRig &rig = made.value();

    const StripSpec &ring = *find(rig, "ring");
    CHECK(length_m(ring) == doctest::Approx(2.0 * kPi * 0.02).epsilon(0.001));
    CHECK(has(ring, "rings"));
    CHECK(ring.polyline.front().x == doctest::Approx(ring.polyline.back().x)); // Closed.

    const StripSpec &box = *find(rig, "box");
    CHECK(length_m(box) == doctest::Approx(0.06));
    CHECK(has(box, "ornaments")); // Every labelled group holding it.
    CHECK(has(box, "gems"));

    const StripSpec &bow = *find(rig, "Bow");                           // Named by its label.
    CHECK(length_m(bow) == doctest::Approx(kPi * 0.01).epsilon(0.001)); // A half circle.
    CHECK(bow.polyline.back().y == doctest::Approx(bow.polyline.front().y));

    const StripSpec &tri = *find(rig, "tri"); // Implicit lines after m, then closed.
    CHECK(length_m(tri) == doctest::Approx(0.01 + 0.01 + std::sqrt(0.0002)));

    CHECK(find(rig, "wave")->polyline.size() > 10); // Curves become short segments.
    CHECK(length_m(*find(rig, "zig")) == doctest::Approx(2.0 * std::sqrt(0.0002)));

    const StripSpec &tick = *find(rig, "tick"); // Rotated a quarter turn: now horizontal.
    CHECK(tick.polyline.front().y == doctest::Approx(tick.polyline.back().y));
    CHECK(length_m(tick) == doctest::Approx(0.01));
}

TEST_CASE("svg rig: hidden layers, defs and dots are left out; several subpaths, several strips") {
    const std::string svg = drawing(R"svg(
  <g inkscape:label="Photo" style="display:none"><path id="trace" d="M 0 0 L 140 75"/></g>
  <path id="pair" d="M 0 0 L 50 0 M 0 10 L 50 10"/>
  <path id="dot" d="M 100 50 L 100.001 50"/>)svg");
    const Result<SvgRig> made = rig_from_svg(svg, {});
    REQUIRE(made);
    const SvgRig &rig = made.value();
    REQUIRE(rig.strips.size() == 2);
    CHECK(rig.strips[0].name == "pair 1");
    CHECK(rig.strips[1].name == "pair 2");
    CHECK(rig.too_short == 1);
    CHECK(find(rig, "trace") == nullptr);
    CHECK(find(rig, "hidden-in-defs") == nullptr);
}

TEST_CASE("svg rig: pixel documents and errors") {
    // No units: CSS pixels, 96 per inch.
    const Result<SvgRig> px = rig_from_svg(
        R"svg(<svg width="960" height="96"><line x1="0" y1="0" x2="960" y2="0"/></svg>)svg", {});
    REQUIRE(px);
    CHECK(px.value().width_m == doctest::Approx(0.254));

    CHECK_FALSE(rig_from_svg("not a drawing", {}));
    CHECK_FALSE(rig_from_svg(drawing(""), {}));
    CHECK_FALSE(rig_from_svg(drawing(R"svg(<path d="M 0 0 L 0.001 0"/>)svg"), {}));
}
