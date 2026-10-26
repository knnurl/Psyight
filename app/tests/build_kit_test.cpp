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
