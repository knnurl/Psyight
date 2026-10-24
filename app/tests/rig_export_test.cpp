// The rig as a drawing (io/rig_export.h): read back by the SVG and DXF importers, the centre
// lines are the rig's paths; channels, pixels and wiring marks have the expected geometry.

#include "engine/rig_templates.h"
#include "io/dxf_rig.h"
#include "io/rig_export.h"
#include "io/svg_rig.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
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
