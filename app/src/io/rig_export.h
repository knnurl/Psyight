// The rig as a drawing to build from (Rig > Export drawing...): SVG for Inkscape, DXF for Fusion
// and laser cutters, the same content in both, in millimetres, y up as the rig (the SVG flips
// it). Layers by kind, each optional:
// - Centre lines: every strip's path, the line its LEDs sit on.
// - Channels: each path offset by half the channel width either side (mitred corners), a closed
//   slot outline per open strip and an inner and outer outline per closed one, to cut grooves or
//   a mount.
// - Pixels: a 3 mm circle at every LED.
// - Wiring: an arrow at each strip's data-in end and a label (its place on the data line, its
//   name and the note given: the node line and pin).
// The DXF is AutoCAD R12 text (POLYLINE, CIRCLE, TEXT, LINE), which old and new readers open;
// the SVG keeps Inkscape layers. Importing one back (From drawing) reads every visible layer:
// hide all but Centre lines first.
#pragma once

#include "engine/rig.h"

#include <string>
#include <vector>

namespace psyight {

struct RigExportOptions {
    bool centre_lines = true;
    bool channels = true;
    double channel_width_m = 0.012; // A 10 mm strip with a little room.
    bool pixels = true;
    bool wiring = true;
};

// strips in data-line order; notes: one per strip (or none), shown in the wiring labels.
std::string rig_to_svg(const std::vector<StripSpec> &strips, const std::vector<std::string> &notes,
                       const RigExportOptions &options);
std::string rig_to_dxf(const std::vector<StripSpec> &strips, const std::vector<std::string> &notes,
                       const RigExportOptions &options);

} // namespace psyight
