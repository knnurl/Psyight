// Modulation matrix v0 (SPEC section 7.4): a route takes a source, shapes it (curve, invert) and
// adds amount times it to a target. Sources are normalised to 0 to 1 and are pure functions of
// the engine inputs and the seed, so rendering stays deterministic. Routes to one target are
// summed, then clamped to its range.
// Targets in v0: a layer's opacity (base plus routes, 0 to 1), a layer's hue shift (degrees,
// rotated about the grey axis, which keeps luminance roughly), and the scene's brightness (1 plus
// amount times (source minus 1), so amount 1 lets the source set it). Not yet: smoothing,
// sample-and-hold, quantise to grid, MIDI CC, registry parameters as targets, GUI rings.
#pragma once

#include "engine/colour.h"
#include "engine/inputs.h"

#include <array>
#include <cstdint>

namespace psyight {

enum class ModSource {
    constant,
    beat_phase,
    bar_phase,
    phrase_phase,
    section_progress,
    kick,        // exp(-time since kick / 0.15 s).
    bass_active, // 1 while a detected note sounds.
    bass_velocity,
    bass_gate,
    bass_pitch, // C1 (24) to C4 (60).
    residual_level,
    residual_brightness, // Spectral centroid, 0 to 6 kHz.
    lfo_beat,            // 0.5 - 0.5 cos(2 pi phase): 0 on the beat, 1 halfway.
    lfo_bar,
    lfo_phrase,
    random_bar, // Seeded, one value per bar.
};

enum class ModCurve { linear, exponential, logarithmic, s_curve };
enum class ModTarget { opacity, hue_shift, brightness };

inline constexpr std::array<ModSource, 16> kModSources = {
    ModSource::constant,     ModSource::beat_phase,       ModSource::bar_phase,
    ModSource::phrase_phase, ModSource::section_progress, ModSource::kick,
    ModSource::bass_active,  ModSource::bass_velocity,    ModSource::bass_gate,
    ModSource::bass_pitch,   ModSource::residual_level,   ModSource::residual_brightness,
    ModSource::lfo_beat,     ModSource::lfo_bar,          ModSource::lfo_phrase,
    ModSource::random_bar};
inline constexpr std::array<ModCurve, 4> kModCurves = {ModCurve::linear, ModCurve::exponential,
                                                       ModCurve::logarithmic, ModCurve::s_curve};
inline constexpr std::array<ModTarget, 3> kModTargets = {ModTarget::opacity, ModTarget::hue_shift,
                                                         ModTarget::brightness};

struct ModRoute {
    ModSource source = ModSource::lfo_bar;
    ModCurve curve = ModCurve::linear;
    bool invert = false;
    ModTarget target = ModTarget::opacity;
    int layer = 0;       // For opacity and hue shift.
    double amount = 0.5; // Opacity and brightness: a fraction; hue shift: degrees.
};

double source_value(ModSource source, const EngineInputs &in, std::uint64_t seed);
double shaped(double value, ModCurve curve, bool invert);
double route_value(const ModRoute &route, const EngineInputs &in, std::uint64_t seed);

// Rotates the hue about the grey axis, keeping luminance roughly; clamped at 0.
Rgb rotate_hue(const Rgb &c, double degrees);

const char *mod_source_name(ModSource source);
const char *mod_curve_name(ModCurve curve);
const char *mod_target_name(ModTarget target);

} // namespace psyight
