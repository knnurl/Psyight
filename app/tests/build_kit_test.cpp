// The build kit (io/build_kit.h): islands, bolt and wire holes, the acrylic lattice and the four
// parts' files, on a square ring and on the hexagon mandala preset.

#include "engine/rig_templates.h"
#include "io/build_kit.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace psyight;

namespace {

size_t count_of(const std::string &text, const std::string &what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

// The nearest any bolt hole (a part's circles of radius r_mm) comes to a strip's centre line, in
// mm: the part's drawing is centred on the strips' middle.
double nearest_bolt_mm(const std::string &dxf, double r_mm, const std::vector<StripSpec> &strips) {
    double lo_x = 1e18, hi_x = -1e18, lo_y = 1e18, hi_y = -1e18;
    for (const StripSpec &s : strips) {
        for (const Vec3 &v : s.polyline) {
            lo_x = std::min(lo_x, v.x * 1000.0);
            hi_x = std::max(hi_x, v.x * 1000.0);
            lo_y = std::min(lo_y, v.y * 1000.0);
            hi_y = std::max(hi_y, v.y * 1000.0);
        }
    }
    double nearest = 1e18;
    for (size_t at = dxf.find("\nCIRCLE\n"); at != std::string::npos;
         at = dxf.find("\nCIRCLE\n", at + 1)) {
        const auto value = [&](const char *code) {
            return std::atof(dxf.c_str() + dxf.find(code, at) + std::string(code).size());
        };
        if (std::abs(value("\n40\n") - r_mm) > 1e-3) {
            continue;
        }
        const double x = value("\n10\n") + 0.5 * (lo_x + hi_x);
        const double y = value("\n20\n") + 0.5 * (lo_y + hi_y);
        for (const StripSpec &s : strips) {
            for (size_t k = 0; k + 1 < s.polyline.size(); ++k) {
                const double ax = s.polyline[k].x * 1000.0, ay = s.polyline[k].y * 1000.0;
                const double dx = s.polyline[k + 1].x * 1000.0 - ax;
                const double dy = s.polyline[k + 1].y * 1000.0 - ay;
                const double len2 = dx * dx + dy * dy;
                const double t =
                    len2 > 0.0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / len2, 0.0, 1.0) : 0.0;
                nearest = std::min(nearest, std::hypot(ax + t * dx - x, ay + t * dy - y));
            }
        }
    }
    return nearest;
}

StripSpec square_ring() {
    StripSpec ring;
    ring.name = "Ring";
    ring.polyline = {{-0.1, 0.9, 0}, {0.1, 0.9, 0}, {0.1, 1.1, 0}, {-0.1, 1.1, 0}, {-0.1, 0.9, 0}};
    ring.pixels_per_m = 60.0;
    ring.count = 48;
    return ring;
}

} // namespace

TEST_CASE("build kit: a square ring gives two islands, bolts in each, a lattice and four parts") {
    StripSpec ring;
    ring.name = "Ring";
    ring.polyline = {{-0.1, 0.9, 0}, {0.1, 0.9, 0}, {0.1, 1.1, 0}, {-0.1, 1.1, 0}, {-0.1, 0.9, 0}};
    ring.pixels_per_m = 60.0;
    ring.count = 48;
    const BuildKit kit = build_kit({ring}, {});
    REQUIRE(kit.parts.size() == 4);
    CHECK(kit.parts[0].name == "back");
    CHECK(kit.parts[3].name == "front");
    CHECK(kit.islands == 2); // Inside the ring, and the frame round it.
    CHECK(kit.bolts == 4);   // Both over 12 cm long: two each.
    const std::string &spacer = kit.parts[1].dxf;
    CHECK(count_of(spacer, "\nCIRCLE\n") == 4);
    CHECK(count_of(spacer, "\nPOLYLINE\n8\nCut\n") ==
          3); // The inner island; the frame's two edges.
    CHECK(count_of(kit.parts[0].dxf, "\nCIRCLE\n") == 5); // Bolts and the ring's one wire hole.
    CHECK(count_of(kit.parts[2].dxf, "\nPOLYLINE\n8\nCut\n") == 2); // The lattice: outside, inside.
    CHECK(kit.parts[1].svg.find("inkscape:label=\"Engrave\"") != std::string::npos); // Numbers.
}

TEST_CASE("build kit: the hexagon mandala's islands between rings and spokes") {
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name != "Hexagon mandala") {
            continue;
        }
        const BuildKit kit = build_kit(p.strips, {});
        MESSAGE("islands " << kit.islands << ", bolts " << kit.bolts);
        CHECK(kit.islands >= 13);
        CHECK(kit.bolts >= kit.islands);
        // Every bolt clear of the acrylic (6 mm channel half-width, 4 mm overlap, 1.7 mm
        // hole).
        CHECK(nearest_bolt_mm(kit.parts[1].dxf, 1.7, p.strips) >= 11.7 - 0.3);
        for (const BuildKitPart &part : kit.parts) {
            CHECK(part.dxf.find("$INSUNITS\n70\n4") != std::string::npos);
            CHECK(part.dxf.find("\nEOF\n") != std::string::npos);
        }
    }
}

TEST_CASE("build kit: one DXF with the parts stacked at their heights, as thick as their "
          "sheets") {
    StripSpec ring;
    ring.name = "Ring";
    ring.polyline = {{-0.1, 0.9, 0}, {0.1, 0.9, 0}, {0.1, 1.1, 0}, {-0.1, 1.1, 0}, {-0.1, 0.9, 0}};
    ring.pixels_per_m = 60.0;
    ring.count = 48;
    const BuildKit kit = build_kit({ring}, {}); // Sheets 6, 9, 3 and 3 mm.
    CHECK(kit.height_m == doctest::Approx(0.021));
    const std::string &dxf = kit.assembly_dxf;
    for (const char *layer : {"1_back_cut", "1_back_engrave", "2_spacer_cut", "2_spacer_engrave",
                              "3_acrylic_cut", "4_front_cut"}) {
        CAPTURE(layer);
        CHECK(dxf.find(std::string("\n2\n") + layer + "\n") != std::string::npos); // Declared.
    }
    // The spacer's outlines start on the back panel (6 mm up) and are 9 mm thick; the
    // front's sit at 18 mm, 3 mm thick.
    CHECK(dxf.find("POLYLINE\n8\n2_spacer_cut\n39\n9.000\n66\n1\n10\n0.0\n20\n0.0\n30\n6.000\n") !=
          std::string::npos);
    CHECK(dxf.find("POLYLINE\n8\n4_front_cut\n39\n3.000\n66\n1\n10\n0.0\n20\n0.0\n30\n18.000\n") !=
          std::string::npos);
    CHECK(dxf.find("\nEOF\n") != std::string::npos);
}

TEST_CASE("build kit: the one DXF can hold the rig export on the LEDs' plane") {
    StripSpec ring;
    ring.name = "Ring";
    ring.polyline = {{-0.1, 0.9, 0}, {0.1, 0.9, 0}, {0.1, 1.1, 0}, {-0.1, 1.1, 0}, {-0.1, 0.9, 0}};
    ring.pixels_per_m = 60.0;
    ring.count = 48;
    const RigExportOptions rig;
    const BuildKit kit = build_kit({ring}, {}, &rig, {"line 1, PD0"});
    const std::string &dxf = kit.assembly_dxf;
    for (const char *layer : {"0_centre_lines", "0_channels", "0_pixels", "0_wiring"}) {
        CAPTURE(layer);
        CHECK(dxf.find(std::string("\n2\n") + layer + "\n") != std::string::npos);
    }
    // 48 pixel dots on the back panel's top face (6 mm up).
    size_t dots = 0;
    for (size_t at = dxf.find("CIRCLE\n8\n0_pixels\n"); at != std::string::npos;
         at = dxf.find("CIRCLE\n8\n0_pixels\n", at + 1)) {
        CHECK(dxf.substr(dxf.find("\n30\n", at) + 4, 5) == "6.000");
        ++dots;
    }
    CHECK(dots == 48);
    CHECK(dxf.find("1. Ring, line 1, PD0") != std::string::npos);
    // Without the rig, none of its layers.
    CHECK(build_kit({ring}, {}).assembly_dxf.find("0_pixels") == std::string::npos);
}

TEST_CASE("build kit: an open lattice, walls for a spacer, bolts in ears past the acrylic") {
    BuildKitOptions o;
    o.style = BuildKitStyle::lattice;
    const StripSpec ring = square_ring();
    const BuildKit kit = build_kit({ring}, o);
    CHECK(kit.islands == 2); // The walls inside and outside the ring.
    CHECK(kit.ears == 4);    // 5 mm walls hold no bolt: two ears each, both over 12 cm long.
    CHECK(kit.bolts == 4);
    // The back is the lattice itself, open in the middle: an outside and an inside edge.
    CHECK(count_of(kit.parts[0].dxf, "\nPOLYLINE\n8\nCut\n") == 2);
    CHECK(count_of(kit.parts[3].dxf, "\nPOLYLINE\n8\nCut\n") == 4); // Front: and the window.
    // Each bolt 0.5 mm past the acrylic's edge: 6 + 4 + 0.5 + 1.7 mm from the centre line.
    CHECK(nearest_bolt_mm(kit.parts[1].dxf, 1.7, {ring}) == doctest::Approx(12.2).epsilon(0.03));
    // The panel style keeps a solid back.
    CHECK(count_of(build_kit({ring}, {}).parts[0].dxf, "\nPOLYLINE\n8\nCut\n") == 1);
}

TEST_CASE("build kit: the hexagon mandala as an open lattice") {
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name != "Hexagon mandala") {
            continue;
        }
        BuildKitOptions o;
        o.style = BuildKitStyle::lattice;
        const BuildKit kit = build_kit(p.strips, o);
        MESSAGE("walls " << kit.islands << ", bolts " << kit.bolts << ", ears " << kit.ears);
        CHECK(kit.bolts >= kit.islands);
        CHECK(kit.ears == kit.bolts);
        CHECK(nearest_bolt_mm(kit.parts[1].dxf, 1.7, p.strips) >= 12.2 - 0.3);
    }
}
