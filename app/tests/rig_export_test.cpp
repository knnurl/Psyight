// The rig as a drawing (io/rig_export.h): read back by the SVG and DXF importers, the centre
// lines are the rig's paths; channels, pixels and wiring marks have the expected geometry.

#include "engine/rig_templates.h"
#include "io/dxf_rig.h"
#include "io/rig_export.h"
#include "io/svg_rig.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace psyight;

namespace {

double length_m(const std::vector<Vec3> &p) {
    double length = 0.0;
    for (size_t i = 1; i < p.size(); ++i) {
        length += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    }
    return length;
}

std::vector<StripSpec> hexagon_mandala() {
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name == "Hexagon mandala") {
            return p.strips;
        }
    }
    FAIL("no Hexagon mandala preset");
    return {};
}

size_t count_of(const std::string &text, const std::string &what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("rig export: the centre lines read back as the rig's paths, in SVG and DXF") {
    const std::vector<StripSpec> strips = hexagon_mandala();
    REQUIRE_FALSE(strips.empty());
    RigExportOptions only_lines;
    only_lines.channels = only_lines.pixels = only_lines.wiring = false;
    for (const bool dxf : {false, true}) {
        CAPTURE(dxf);
        const std::string text =
            dxf ? rig_to_dxf(strips, {}, only_lines) : rig_to_svg(strips, {}, only_lines);
        const Result<SvgRig> back = dxf ? rig_from_dxf(text, {}) : rig_from_svg(text, {});
        REQUIRE(back);
        REQUIRE(back.value().strips.size() == strips.size());
        for (size_t i = 0; i < strips.size(); ++i) {
            CAPTURE(i);
            CHECK(length_m(back.value().strips[i].polyline) ==
                  doctest::Approx(length_m(strips[i].polyline)).epsilon(0.002));
        }
    }
}

TEST_CASE("rig export: channels are slots or ring outlines; a mark per pixel; wiring labels") {
    StripSpec line;
    line.name = "Bar";
    line.polyline = {{0, 1, 0}, {1, 1, 0}};
    line.pixels_per_m = 60.0;
    line.count = 60;
    StripSpec ring;
    ring.name = "Ring";
    for (int i = 0; i <= 64; ++i) {
        const double a = 2.0 * 3.14159265358979 * i / 64;
        ring.polyline.push_back({2.0 + 0.2 * std::cos(a), 1.0 + 0.2 * std::sin(a), 0.0});
    }
    ring.pixels_per_m = 60.0;
    ring.count = 70;
    const std::vector<StripSpec> strips = {line, ring};

    RigExportOptions channels;
    channels.centre_lines = channels.pixels = channels.wiring = false;
    channels.channel_width_m = 0.012;
    const Result<SvgRig> slots = rig_from_svg(rig_to_svg(strips, {}, channels), {});
    REQUIRE(slots);
    REQUIRE(slots.value().strips.size() == 3); // The bar's slot; the ring's two outlines.
    CHECK(length_m(slots.value().strips[0].polyline) == doctest::Approx(2.0 + 2 * 0.012));
    const double outer = length_m(slots.value().strips[1].polyline);
    const double inner = length_m(slots.value().strips[2].polyline);
    CHECK(std::max(outer, inner) - std::min(outer, inner) ==
          doctest::Approx(2.0 * 3.14159 * 0.012).epsilon(0.05));

    RigExportOptions marks;
    marks.centre_lines = marks.channels = false;
    const std::string svg = rig_to_svg(strips, {"line 1, PD0", "line 1, PD0"}, marks);
    CHECK(count_of(svg, "<circle") == 130); // 60 + 70 pixels.
    CHECK(svg.find("1. Bar, line 1, PD0") != std::string::npos);
    CHECK(svg.find("2. Ring, line 1, PD0") != std::string::npos);
    const std::string dxf = rig_to_dxf(strips, {}, marks);
    CHECK(count_of(dxf, "\nCIRCLE\n") == 130);
    CHECK(count_of(dxf, "\nTEXT\n") == 2);
    CHECK(dxf.find("$INSUNITS\n70\n4") != std::string::npos);
}

TEST_CASE("rig export: centred on the rig's middle, in DXF and SVG") {
    std::vector<StripSpec> strips = hexagon_mandala(); // Its centre is well above the floor.
    RigExportOptions only_lines;
    only_lines.channels = only_lines.pixels = only_lines.wiring = false;
    const std::string dxf = rig_to_dxf(strips, {}, only_lines);
    // The DXF's coordinates: x and y each span about as far below 0 as above.
    double lo_x = 1e9, hi_x = -1e9, lo_y = 1e9, hi_y = -1e9;
    std::istringstream in(dxf);
    std::string code;
    std::string value;
    while (std::getline(in, code) && std::getline(in, value)) {
        if (code == "10" && std::stod(value) != 0.0) {
            lo_x = std::min(lo_x, std::stod(value));
            hi_x = std::max(hi_x, std::stod(value));
        } else if (code == "20" && std::stod(value) != 0.0) {
            lo_y = std::min(lo_y, std::stod(value));
            hi_y = std::max(hi_y, std::stod(value));
        }
    }
    CHECK(lo_x == doctest::Approx(-hi_x).epsilon(0.01));
    CHECK(lo_y == doctest::Approx(-hi_y).epsilon(0.01));
    // The SVG: the paths sit in the middle of the page (as far from its left edge as its right).
    const std::string svg = rig_to_svg(strips, {}, only_lines);
    const size_t box = svg.find("viewBox=\"0 0 ");
    REQUIRE(box != std::string::npos);
    const double page_width = std::stod(svg.substr(box + 13));
    double lo = 1e9;
    double hi = -1e9;
    for (size_t at = svg.find("d=\"M "); at != std::string::npos; at = svg.find("d=\"M ", at + 1)) {
        std::istringstream path(svg.substr(at + 5, svg.find('"', at + 5) - at - 5));
        double x = 0.0;
        double y = 0.0;
        while (path >> x >> y) {
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
    }
    CHECK(lo == doctest::Approx(page_width - hi).epsilon(0.01));
}

TEST_CASE("rig export: layer colours for laser software, pixel dots filled to engrave") {
    StripSpec line;
    line.name = "Bar";
    line.polyline = {{0, 1, 0}, {1, 1, 0}};
    line.pixels_per_m = 10.0;
    line.count = 10;
    RigExportOptions o; // Defaults: channels black, pixels red.
    const std::string svg = rig_to_svg({line}, {}, o);
    CHECK(svg.find("inkscape:label=\"Channels\" fill=\"none\" stroke=\"#000000\"") !=
          std::string::npos);
    CHECK(svg.find("fill=\"#ff0000\"/>") != std::string::npos); // Filled red dots.
    const std::string dxf = rig_to_dxf({line}, {}, o);
    CHECK(dxf.find("Channels\n70\n0\n62\n7\n") != std::string::npos); // Black: index 7.
    CHECK(dxf.find("Pixels\n70\n0\n62\n1\n") != std::string::npos);   // Red: index 1.
    o.channel_colour = ExportColour::blue;
    CHECK(rig_to_dxf({line}, {}, o).find("Channels\n70\n0\n62\n5\n") != std::string::npos);
}

TEST_CASE("rig export: wiring order numbers drawn in lines beside each arrow") {
    std::vector<StripSpec> strips;
    for (int i = 0; i < 12; ++i) { // Numbers 1 to 12: one and two digits.
        StripSpec s;
        s.name = "S" + std::to_string(i + 1);
        s.polyline = {{0.0, 0.1 * i, 0.0}, {1.0, 0.1 * i, 0.0}};
        s.pixels_per_m = 10.0;
        s.count = 10;
        strips.push_back(s);
    }
    RigExportOptions only_wiring;
    only_wiring.centre_lines = only_wiring.channels = only_wiring.pixels = false;
    const std::string dxf = rig_to_dxf(strips, {}, only_wiring);
    // Arrows: 12 closed triangles. Strokes: 1-9 (1,1,2,2,1,1,1,2,1 strokes: 12), then 10, 11, 12
    // (1+1, 1+1, 1+1 strokes: 6). 12 + 12 + 6 polylines on the Wiring layer.
    size_t polylines = 0;
    for (size_t at = dxf.find("\nPOLYLINE\n8\nWiring\n"); at != std::string::npos;
         at = dxf.find("\nPOLYLINE\n8\nWiring\n", at + 1)) {
        ++polylines;
    }
    CHECK(polylines == 12 + 12 + 6);
}

TEST_CASE("rig export: the wiring number sits on the strip's line, past the arrow, upright") {
    StripSpec bar;
    bar.name = "Bar";
    bar.polyline = {{1.0, 1.0, 0}, {0.0, 1.0, 0}}; // Running right to left.
    bar.pixels_per_m = 10.0;
    bar.count = 10;
    RigExportOptions only_wiring;
    only_wiring.centre_lines = only_wiring.channels = only_wiring.pixels = false;
    const std::vector<ExportShape> shapes = rig_export_shapes({bar}, {}, only_wiring);
    // The digit "1": one stroke, after the arrow (a closed triangle) and the text label.
    const ExportShape &digit = shapes.back();
    REQUIRE(digit.kind == ExportShape::Kind::polyline);
    REQUIRE_FALSE(digit.closed);
    for (const Vec3 &q : digit.points) {
        CHECK(std::abs(q.y) <= 4.0 + 1e-6); // On the line (y 0 once centred), within its height.
    }
    // Past the arrow along the strip: the strip runs to -x from its start at +500 mm.
    CHECK(digit.points.front().x < 500.0 - 8.0);
    // Kept upright although the strip runs right to left: the stroke of "1" points up.
    CHECK(std::abs(digit.points.front().x - digit.points.back().x) < 1e-6);
    CHECK(digit.points.back().y > digit.points.front().y);
}
