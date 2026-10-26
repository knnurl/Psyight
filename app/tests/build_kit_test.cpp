// The build kit (io/build_kit.h): islands, bolt and wire holes, the acrylic lattice and the four
// parts' files, on a square ring and on the hexagon mandala preset.

#include "engine/rig_templates.h"
#include "io/build_kit.h"

#include <doctest/doctest.h>

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
        for (const BuildKitPart &part : kit.parts) {
            CHECK(part.dxf.find("$INSUNITS\n70\n4") != std::string::npos);
            CHECK(part.dxf.find("\nEOF\n") != std::string::npos);
        }
    }
}

TEST_CASE("build kit: one DXF with the parts stacked at their heights, as thick as their sheets") {
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
    // The spacer's outlines start on the back panel (6 mm up) and are 9 mm thick; the front's sit
    // at 18 mm, 3 mm thick.
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
