// A build kit for a flat rig (Rig > Export drawing > Build kit): the parts of a layered panel,
// each a drawing in millimetres with a Cut and an Engrave layer, centred as the rig export:
// 1. back: the panel outline, a wire hole at each strip end, the bolt holes; engraved: each
//    spacer island's outline and number (where to glue it);
// 2. spacer: the islands left between the channels (each strip's path widened to channel_width_m,
//    with round ends), numbered by an engraved digit, each with its bolt holes;
// 3. acrylic: one lattice over every channel (widened by overlap_m each side), cut from one sheet;
// 4. front: the panel outline with narrower windows (window_width_m) that clamp the acrylic, and
//    the bolt holes.
// The panel is every channel widened by margin_m, its holes filled. The shapes come from a grid
// (cells of 0.2 mm, coarser for rigs over about 80 cm so a grid stays under 4,000 cells across),
// traced and straightened to within about one and a half cells. Bolt holes sit at each island's
// deepest point (two on islands longer than 12 cm), on islands deep enough to hold one with 3 mm
// of wood around it. Uses x and y only (a flat rig).
#pragma once

#include "engine/rig.h"
#include "io/rig_export.h"

#include <string>
#include <vector>

namespace psyight {

struct BuildKitOptions {
    double channel_width_m = 0.012; // The spacer's slots: a 10 mm strip with a little room.
    double window_width_m = 0.010;  // The front mask's openings.
    double overlap_m = 0.004;       // The acrylic past each side of a channel.
    double margin_m = 0.015;        // Panel beyond the outermost channels.
    double bolt_hole_m = 0.0034;    // M3 clearance.
    double wire_hole_m = 0.005;
    ExportColour cut_colour = ExportColour::black;
    ExportColour engrave_colour = ExportColour::red;
};

struct BuildKitPart {
    std::string name; // "back", "spacer", "acrylic", "front".
    std::string svg;
    std::string dxf;
};

struct BuildKit {
    std::vector<BuildKitPart> parts; // In stacking order, back to front.
    int islands = 0;                 // Spacer pieces.
    int bolts = 0;
};

BuildKit build_kit(const std::vector<StripSpec> &strips, const BuildKitOptions &options);

} // namespace psyight
