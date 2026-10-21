// Rig templates (SPEC section 7.1): generators that turn a few numbers into strips, so a rig
// that fits the real build is set up in seconds. The result is ordinary strips (saved in the
// project and editable like any other); nothing remembers the template.
//
// Coordinates as everywhere in the rig: metres, x across (centred on 0), y up, z towards the
// audience. The preview draws x and y.
// - Pillars: `count` vertical strips of `height_m`, their first pixel `base_m` above the floor,
//   in a row `spacing_m` apart or around a circle of `radius_m`. Each runs bottom to top, or,
//   with serpentine, alternate pillars top to bottom (one data line chained up and down).
//   Groups: "pillars", "left" or "right" (by x; the middle pillar of an odd row is both),
//   "even" or "odd" (by index from 0).
// - Matrix panel: `rows` horizontal strips of `width_m`, `row_spacing_m` apart, the bottom row
//   `base_m` above the floor. Each runs left to right, or with serpentine, alternate rows right
//   to left (the usual zigzag wiring). Groups: "matrix", "top" or "bottom" (by half),
//   "even" or "odd" rows.
// Pixels per strip: the length times pixels_per_m, rounded down.
//
// Rig presets: named, fixed stage shapes, each within the 1,500-pixel budget (SPEC 2.1). Shapes
// with mirror symmetry run each half from the centre outwards, so mirrored and radial mappings
// line up. Every strip is tagged "left" and/or "right" by the centre of its bounding box (both
// when centred), plus its shape's own groups, listed in each preset's description.
#pragma once

#include "engine/rig.h"

#include <string>
#include <vector>

namespace psyight {

struct PillarsTemplate {
    int count = 6;
    double height_m = 2.0;
    double base_m = 0.0;
    double spacing_m = 1.0;
    bool circle = false;
    double radius_m = 2.0;
    double pixels_per_m = 60.0;
    bool serpentine = false;
};

struct MatrixTemplate {
    int rows = 16;
    double width_m = 1.0;
    double row_spacing_m = 1.0 / 30.0;
    double base_m = 0.5;
    double pixels_per_m = 30.0;
    bool serpentine = true;
};

inline constexpr int kMaxTemplateStrips = 128;

std::vector<StripSpec> pillars_strips(const PillarsTemplate &t);
std::vector<StripSpec> matrix_strips(const MatrixTemplate &t);

struct RigPreset {
    std::string name;
    std::string description; // One line for the list: the shape, its size and its groups.
    std::vector<StripSpec> strips;
};

std::vector<RigPreset> builtin_rig_presets();

} // namespace psyight
