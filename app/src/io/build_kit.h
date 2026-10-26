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
//
// Open lattice (style lattice): no panel, the wall shows through. The back and front are each one
// lattice, every channel widened by wall_m each side (the openings between the rings and spokes
// left open); the spacer is the walls (the lattice less the channels), engraved on the back where
// they glue. A wall too thin for a bolt gets an ear: a round tab beyond the acrylic's edge on the
// back, spacer and front, holding the bolt with 3 mm of wood around it.
#pragma once

#include "engine/rig.h"
#include "io/rig_export.h"

#include <string>
#include <vector>

namespace psyight {

enum class BuildKitStyle { panel, lattice };

struct BuildKitOptions {
    BuildKitStyle style = BuildKitStyle::panel;
    double channel_width_m = 0.012; // The spacer's slots: a 10 mm strip with a little room.
    double window_width_m = 0.010;  // The front mask's openings.
    double overlap_m = 0.004;       // The acrylic past each side of a channel.
    double margin_m = 0.015;        // Panel beyond the outermost channels.
    double wall_m = 0.005;          // Lattice: each side of a channel.
    double bolt_hole_m = 0.0034;    // M3 clearance.
    double wire_hole_m = 0.005;
    // Sheet thicknesses, for the assembly's heights.
    double back_thickness_m = 0.006;
    double spacer_thickness_m = 0.009;
    double acrylic_thickness_m = 0.003;
    double front_thickness_m = 0.003;
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
    int ears = 0; // Lattice: bolt tabs beyond the walls.
    // All four parts in one DXF, stacked: each part on its own layers, named with the heights it
    // spans in mm (1_back_z0-6mm_cut, 1_back_z0-6mm_engrave, 2_spacer_z6-15mm_cut and so on; a
    // fraction as 4p5, since R12 names allow no dot), at its height from the back panel's
    // underside (z 0) and extruded by its sheet's thickness (DXF entity thickness), engraving on
    // its top.
    std::string assembly_dxf;
    double height_m = 0.0; // The stack's total thickness.
};

// rig: when given, the assembly also holds the rig export (its centre lines, channels, pixels
// and wiring, as Export drawing writes them, with `notes` in the wiring labels) on layers
// 0_<layer>_z<height>mm, at the LEDs' plane: the back panel's top face, where the strips sit.
BuildKit build_kit(const std::vector<StripSpec> &strips, const BuildKitOptions &options,
                   const RigExportOptions *rig = nullptr,
                   const std::vector<std::string> &notes = {});

} // namespace psyight
