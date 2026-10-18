// Modulation matrix (SPEC section 7.4): a route takes a source, shapes it (curve, invert),
// optionally holds it for a grid division (quantise: sample-and-hold at each 16th, beat or bar)
// and smooths it (attack while rising, release while falling), and adds amount times it to a
// target. Sources are normalised to 0 to 1 and are pure functions of the engine inputs and the
// seed; the macros are the operator's knobs (8, from the Show tab or MIDI). Holding and smoothing
// need memory across frames (ModRouteState): with it, a route is deterministic for the same
// sequence of render times (as the limiters are); without it, a route is its instantaneous shaped
// value. Routes to one target are summed, then clamped to its range.
// Targets: a layer's opacity (base plus routes, 0 to 1), a layer's hue shift (degrees, rotated
// about the grey axis, which keeps luminance roughly), the scene's brightness (1 plus amount times
// (source minus 1), so amount 1 lets the source set it), and a layer's effect setting (`param`, an
// id from engine/effect_spec.h; amount is a fraction of the setting's range, so 1 sweeps all of
// it; the sum is clamped to the range and the effect is remade for the frame).
#pragma once

#include "engine/colour.h"
#include "engine/inputs.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

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
    macro_1,    // The operator's macro knobs (EngineInputs::macros).
    macro_2,
    macro_3,
    macro_4,
    macro_5,
    macro_6,
    macro_7,
    macro_8,
};

enum class ModCurve { linear, exponential, logarithmic, s_curve };
enum class ModTarget { opacity, hue_shift, brightness, setting };
enum class ModQuantise { off, sixteenth, beat, bar }; // Sample-and-hold per grid division.

inline constexpr std::array<ModSource, 24> kModSources = {
    ModSource::constant,     ModSource::beat_phase,       ModSource::bar_phase,
    ModSource::phrase_phase, ModSource::section_progress, ModSource::kick,
    ModSource::bass_active,  ModSource::bass_velocity,    ModSource::bass_gate,
    ModSource::bass_pitch,   ModSource::residual_level,   ModSource::residual_brightness,
    ModSource::lfo_beat,     ModSource::lfo_bar,          ModSource::lfo_phrase,
    ModSource::random_bar,   ModSource::macro_1,          ModSource::macro_2,
    ModSource::macro_3,      ModSource::macro_4,          ModSource::macro_5,
    ModSource::macro_6,      ModSource::macro_7,          ModSource::macro_8};
inline constexpr std::array<ModQuantise, 4> kModQuantises = {
    ModQuantise::off, ModQuantise::sixteenth, ModQuantise::beat, ModQuantise::bar};
inline constexpr double kMaxSmoothingS = 4.0; // Attack and release, at most.
inline constexpr std::array<ModCurve, 4> kModCurves = {ModCurve::linear, ModCurve::exponential,
                                                       ModCurve::logarithmic, ModCurve::s_curve};
inline constexpr std::array<ModTarget, 4> kModTargets = {ModTarget::opacity, ModTarget::hue_shift,
                                                         ModTarget::brightness, ModTarget::setting};

struct ModRoute {
    ModSource source = ModSource::lfo_bar;
    ModCurve curve = ModCurve::linear;
    bool invert = false;
    ModTarget target = ModTarget::opacity;
    int layer = 0; // For opacity, hue shift and settings.
    // Opacity and brightness: a fraction; hue shift: degrees; a setting: a fraction of its range.
    double amount = 0.5;
    std::string param; // The setting's id, for ModTarget::setting.
    ModQuantise quantise = ModQuantise::off;
    double attack_s = 0.0;  // Time constant while the value rises; 0: at once.
    double release_s = 0.0; // Time constant while it falls.

    bool operator==(const ModRoute &) const = default;
};

// A route's memory across frames (holding and smoothing).
struct ModRouteState {
    bool started = false;
    double t_s = 0.0;       // Of the last step.
    long long division = 0; // The grid division the held value belongs to.
    double held = 0.0;      // The shaped value at the division's start.
    double value = 0.0;     // The output: held (or shaped), then smoothed.
};

// A render time that moves back, or forward by more than this, restarts a route's memory at its
// current value (a seek, or a scene not rendered for a while).
inline constexpr double kModRestartS = 0.5;

double source_value(ModSource source, const EngineInputs &in, std::uint64_t seed);
double shaped(double value, ModCurve curve, bool invert);
// The instantaneous value: the shaped source, without holding or smoothing.
double route_value(const ModRoute &route, const EngineInputs &in, std::uint64_t seed);
// One frame with memory: the shaped source, held per division, then smoothed. Calling it twice
// for the same render time returns the same value.
double step_route(const ModRoute &route, ModRouteState &state, const EngineInputs &in,
                  std::uint64_t seed);
// The division index a quantised route holds for (0 when off).
long long mod_division(ModQuantise quantise, const EngineInputs &in);

// Rotates the hue about the grey axis, keeping luminance roughly; clamped at 0.
Rgb rotate_hue(const Rgb &c, double degrees);

const char *mod_source_name(ModSource source);
const char *mod_curve_name(ModCurve curve);
const char *mod_target_name(ModTarget target);
const char *mod_quantise_name(ModQuantise quantise);

// Stable ids for files ("lfo_phrase", "s_curve", "hue_shift" and so on).
const char *mod_source_id(ModSource source);
const char *mod_curve_id(ModCurve curve);
const char *mod_target_id(ModTarget target);
const char *mod_quantise_id(ModQuantise quantise);
std::optional<ModSource> mod_source_from_id(const std::string &id);
std::optional<ModCurve> mod_curve_from_id(const std::string &id);
std::optional<ModTarget> mod_target_from_id(const std::string &id);
std::optional<ModQuantise> mod_quantise_from_id(const std::string &id);

} // namespace psyight
