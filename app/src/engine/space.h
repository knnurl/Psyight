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

enum class Mapping { along_strip, across_x, across_y, radial, by_strip, random };
inline constexpr int kMappingCount = 6;
// Option names for the effects' settings (EffectParamInfo::options), in enum order.
inline constexpr const char *kMappingOptions =
    "Along strip|Across x|Across y|Radial|By strip|Random";

enum class Clock { free, sixteenth, beat, bar, phrase, bass_note, kick };
inline constexpr int kClockCount = 7;
inline constexpr const char *kClockOptions = "Free|16th|Beat|Bar|Phrase|Bass note|Kick";

// Deterministic hash to 0 to 1 (SplitMix64 finaliser).
double hash_unit(std::uint64_t a, std::uint64_t b, std::uint64_t c);

// u for every pixel of the rig, 0 to 1: along its strip (from the first pixel), across the rig's
// bounding box in x or y, radial from its centre (0 at the centre, 1 at the farthest corner), by
// strip (each strip one value, in strip order), or a seeded random value per pixel.
std::vector<double> map_pixels(const Rig &rig, Mapping mapping, std::uint64_t seed);

// u folded into `wings` mirrored copies: 1 leaves it, 2 runs 0 to 1 and back, and so on.
double fold_wings(double u, int wings);

// The clock's position in cycles: rate per second when free, else rate per 16th, beat, bar,
// phrase, bass note or kick. Notes and kicks step, gliding over 40 ms so a step is no jump.
double clock_cycles(const EngineInputs &in, Clock clock, double rate);

} // namespace psyight
