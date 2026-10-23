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
// Rig presets: named stage shapes, adjustable through their settings (counts, sizes, switches),
// each within the 1,500-pixel budget (SPEC 2.1) at its defaults. Shapes
// with mirror symmetry run each half from the centre outwards, so mirrored and radial mappings
// line up. Every strip is tagged "left" and/or "right" by the centre of its bounding box (both
// when centred), plus its shape's own groups, listed in each preset's description.
#pragma once

#include "engine/rig.h"

#include <functional>
#include <string>
#include <string_view>
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

// One setting of a built-in preset: a count, a length, a multiplier or an on/off switch.
struct RigPresetParam {
    std::string id;
    std::string label;
    std::string unit;   // "m", "x", "deg", "px/m", or "" for counts and switches.
    double value = 0.0; // The default: the preset as its description says.
    double min = 0.0;
    double max = 1.0;
    bool integer = false;
    bool toggle = false; // A checkbox: 0 off, 1 on.
};

// The settings a builder reads, by id, each clamped to its range (missing values: defaults).
class PresetValues {
  public:
    PresetValues(const std::vector<RigPresetParam> &params, const std::vector<double> &values)
        : params_(params), values_(values) {}
    double operator[](std::string_view id) const;
    int count(std::string_view id) const; // Rounded.
    bool on(std::string_view id) const;

  private:
    const std::vector<RigPresetParam> &params_;
    const std::vector<double> &values_;
};

struct RigPreset {
    std::string name;
    std::string description;       // One line for the list: the shape, its size and its groups.
    std::vector<StripSpec> strips; // Built-in: the default build.
    std::string category; // The list's heading for built-in presets; empty for the user's own.
    // Built-in presets are generators: their settings and the builder (empty and null for the
    // user's own rigs, which are fixed strips). Every built-in preset has Scale and, unless it
    // sets pixels per metre itself, Density.
    std::vector<RigPresetParam> params;
    std::function<std::vector<StripSpec>(const PresetValues &)> build;
};

// The preset's strips for `values` (one per setting, in order; missing ones take the defaults):
// built, then scaled about the floor's centre and with Density applied to every strip. A fixed
// rig's own strips.
std::vector<StripSpec> build_rig_preset(const RigPreset &preset, const std::vector<double> &values);

// Grouped by category, each category's presets together: stage builds, Hive, sacred geometry,
// organic.
std::vector<RigPreset> builtin_rig_presets();

} // namespace psyight
