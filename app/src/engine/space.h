// Spatial mapping and time bases for the pattern effects (docs/PLAN_SECTIONS_EFFECTS.md part 2,
// after lighting consoles' phasers): every pixel gets a coordinate u from 0 to 1 by a mapping,
// optionally folded into mirrored wings, and an effect's motion runs on a clock, free or locked
// to the music. Pure functions of the rig, the pixel, the inputs and the seed.
#pragma once

#include "engine/inputs.h"
#include "engine/rig.h"

#include <cstdint>
#include <vector>

namespace psyight {

enum class Mapping {
    along_strip,
    across_x,
    across_y,
    radial,
    by_strip,
    random,
    hex_rings,
    around,
    along_length
};
inline constexpr int kMappingCount = 9;
// Option names for the effects' settings (EffectParamInfo::options), in enum order.
inline constexpr const char *kMappingOptions =
    "Along strip|Across x|Across y|Radial|By strip|Random|Hex rings|Around|Along, by length";

// The rig seen as a hexagon (the hexagon effects and mappings): its centre (the bounding box's
// middle in x and y), the direction of a corner, and the hex distance of its outermost pixel.
// Corners point up and down (the hexagon mandala) when the rig is taller than wide, else
// sideways; or as set: 1 up and down, 2 sideways.
struct HexFrame {
    double cx = 0.0;
    double cy = 0.0;
    double corner_deg = 90.0;
    double reach_m = 1.0; // The outermost pixel's distance from the centre to its flat side.
};
inline constexpr const char *kHexCornerOptions = "Auto|Up and down|Sideways";
HexFrame hex_frame(const Rig &rig, int corners = 0);
// Hex distance from the centre, 1 at the outermost pixel: the same all round each hexagon about
// the centre (a hexagon ring is one value, where a circle would reach its corners last).
double hex_radius(const HexFrame &frame, double x, double y);
// The angle round the centre in turns, 0 at the top and growing clockwise.
double hex_turn(const HexFrame &frame, double x, double y);
// The turn of the hexagon's k-th corner (k = 0..5, clockwise from the corner at or after the top).
double corner_turn(const HexFrame &frame, int k);

// The rig seen as nested hexagon rings and spokes (the hexagon-visuals effects): a strip is a
// ring when its polyline is closed (first and last points within 1 mm) and its pixels' hex
// radius spreads by under 0.1; every other strip is a spoke. Group names are not used, so any
// hexagonal rig works; a rig with no rings has an empty `rings`. O(pixels).
struct HexTopology {
    HexFrame frame;
    double u_min = 0.0;             // Smallest hex radius of any pixel.
    std::vector<int> ring_of_strip; // Per strip: 0 innermost ring, 1, ...; -1 for a spoke.
    std::vector<int> rings;         // Strip indices of the rings, innermost first.
    std::vector<int> spokes;        // Strip indices of the other strips.
    std::vector<double> radius;     // Per pixel: hex_radius.
    std::vector<double> turn;       // Per pixel: hex_turn, 0 at the top, clockwise.
    std::vector<int> first_pixel;   // Per strip: index of its first pixel in rig.pixels (-1: none).
};
HexTopology hex_topology(const Rig &rig, int corners = 0);

enum class Clock { free, sixteenth, beat, bar, phrase, bass_note, kick };
inline constexpr int kClockCount = 7;
inline constexpr const char *kClockOptions = "Free|16th|Beat|Bar|Phrase|Bass note|Kick";

// Deterministic hash to 0 to 1 (SplitMix64 finaliser).
double hash_unit(std::uint64_t a, std::uint64_t b, std::uint64_t c);

// u for every pixel of the rig, 0 to 1: along its strip (from the first pixel), across the rig's
// bounding box in x or y, radial from its centre (0 at the centre, 1 at the farthest corner), by
// strip (each strip one value, in strip order), a seeded random value per pixel, the hex distance
// (hex rings: each hexagon about the centre one value), the turn round the centre (around: 0
// at the top, clockwise), or along its strip in units of the rig's longest strip (along, by
// length: 0 to 1 only on the longest; shorter strips end early, so a pattern runs at one
// physical speed and size on every strip).
std::vector<double> map_pixels(const Rig &rig, Mapping mapping, std::uint64_t seed);

// u folded into `wings` mirrored copies: 1 leaves it, 2 runs 0 to 1 and back, and so on.
double fold_wings(double u, int wings);

// Smooth value noise, 0 to 1: a seeded lattice with smoothstep interpolation in three dimensions
// (two spatial, one time), so a field drifts without jumps.
double value_noise(double x, double y, double z, std::uint64_t seed);

// The clock's position in cycles: rate per second when free, else rate per 16th, beat, bar,
// phrase, bass note or kick. Notes and kicks step, gliding over 40 ms so a step is no jump.
double clock_cycles(const EngineInputs &in, Clock clock, double rate);

} // namespace psyight
