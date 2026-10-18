#include "engine/modulation.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kKickDecayS = 0.15;

double lfo(double phase) { return 0.5 - 0.5 * std::cos(2.0 * kPi * phase); }

// SplitMix64 finaliser to 0 to 1, as the effects use.
double hash01(std::uint64_t a, std::uint64_t b) {
    std::uint64_t z =
        a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) * 0x1.0p-53;
}

} // namespace

double source_value(ModSource source, const EngineInputs &in, std::uint64_t seed) {
    double v = 0.0;
    switch (source) {
    case ModSource::constant:
        v = 1.0;
        break;
    case ModSource::beat_phase:
        v = in.beat_phase;
        break;
    case ModSource::bar_phase:
        v = in.bar_phase;
        break;
    case ModSource::phrase_phase:
        v = in.phrase_phase;
        break;
    case ModSource::section_progress:
        v = in.section_progress;
        break;
    case ModSource::kick:
        v = std::isfinite(in.since_kick_s) ? std::exp(-in.since_kick_s / kKickDecayS) : 0.0;
        break;
    case ModSource::bass_active:
        v = in.bass_active ? 1.0 : 0.0;
        break;
    case ModSource::bass_velocity:
        v = in.bass_velocity;
        break;
    case ModSource::bass_gate:
        v = in.bass_gate_ratio;
        break;
    case ModSource::bass_pitch:
        v = (in.bass_midi + in.bass_cents / 100.0 - 24.0) / 36.0;
        break;
    case ModSource::residual_level:
        v = in.residual_level;
        break;
    case ModSource::residual_brightness:
        v = in.residual_centroid_hz / 6000.0;
        break;
    case ModSource::lfo_beat:
        v = lfo(in.beat_phase);
        break;
    case ModSource::lfo_bar:
        v = lfo(in.bar_phase);
        break;
    case ModSource::lfo_phrase:
        v = lfo(in.phrase_phase);
        break;
    case ModSource::random_bar:
        v = hash01(seed, static_cast<std::uint64_t>(in.bar_index));
        break;
    case ModSource::macro_1:
    case ModSource::macro_2:
    case ModSource::macro_3:
    case ModSource::macro_4:
    case ModSource::macro_5:
    case ModSource::macro_6:
    case ModSource::macro_7:
    case ModSource::macro_8:
        v = in.macros[static_cast<size_t>(static_cast<int>(source) -
                                          static_cast<int>(ModSource::macro_1))];
        break;
    }
    return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
}

double shaped(double value, ModCurve curve, bool invert) {
    double v = std::clamp(value, 0.0, 1.0);
    switch (curve) {
    case ModCurve::linear:
        break;
    case ModCurve::exponential:
        v = v * v;
        break;
    case ModCurve::logarithmic:
        v = std::sqrt(v);
        break;
    case ModCurve::s_curve:
        v = v * v * (3.0 - 2.0 * v);
        break;
    }
    return invert ? 1.0 - v : v;
}

double route_value(const ModRoute &route, const EngineInputs &in, std::uint64_t seed) {
    return shaped(source_value(route.source, in, seed), route.curve, route.invert);
}

long long mod_division(ModQuantise quantise, const EngineInputs &in) {
    switch (quantise) {
    case ModQuantise::off:
        return 0;
    case ModQuantise::sixteenth:
        return 4 * in.beat_index +
               std::clamp(static_cast<long long>(std::floor(4.0 * in.beat_phase)), 0LL, 3LL);
    case ModQuantise::beat:
        return in.beat_index;
    case ModQuantise::bar:
        return in.bar_index;
    }
    return 0;
}

double step_route(const ModRoute &route, ModRouteState &state, const EngineInputs &in,
                  std::uint64_t seed) {
    const double now = route_value(route, in, seed);
    const long long division = mod_division(route.quantise, in);
    const double dt = in.t_s - state.t_s;
    if (!state.started || dt < 0.0 || dt > kModRestartS) {
        state = {true, in.t_s, division, now, now};
        return state.value;
    }
    if (route.quantise == ModQuantise::off) {
        state.held = now;
    } else if (division != state.division) {
        state.held = now; // Sample at the division's first frame, hold until the next.
        state.division = division;
    }
    const double tau = state.held > state.value ? route.attack_s : route.release_s;
    if (tau <= 0.0) {
        state.value = state.held;
    } else if (dt > 0.0) {
        state.value += (1.0 - std::exp(-dt / tau)) * (state.held - state.value);
    }
    state.t_s = in.t_s;
    return state.value;
}

Rgb rotate_hue(const Rgb &c, double degrees) {
    // The usual hue-rotation matrix about the grey axis (luminance weights 0.299, 0.587, 0.114).
    const double a = degrees * kPi / 180.0;
    const double cs = std::cos(a);
    const double sn = std::sin(a);
    const double r = c.r;
    const double g = c.g;
    const double b = c.b;
    const double nr = (0.299 + 0.701 * cs + 0.168 * sn) * r +
                      (0.587 - 0.587 * cs + 0.330 * sn) * g + (0.114 - 0.114 * cs - 0.497 * sn) * b;
    const double ng = (0.299 - 0.299 * cs - 0.328 * sn) * r +
                      (0.587 + 0.413 * cs + 0.035 * sn) * g + (0.114 - 0.114 * cs + 0.292 * sn) * b;
    const double nb = (0.299 - 0.300 * cs + 1.250 * sn) * r +
                      (0.587 - 0.588 * cs - 1.050 * sn) * g + (0.114 + 0.886 * cs - 0.203 * sn) * b;
    return {static_cast<float>(std::max(0.0, nr)), static_cast<float>(std::max(0.0, ng)),
            static_cast<float>(std::max(0.0, nb))};
}

const char *mod_source_name(ModSource source) {
    switch (source) {
    case ModSource::constant:
        return "Constant";
    case ModSource::beat_phase:
        return "Beat phase";
    case ModSource::bar_phase:
        return "Bar phase";
    case ModSource::phrase_phase:
        return "Phrase phase";
    case ModSource::section_progress:
        return "Section progress";
    case ModSource::kick:
        return "Kick";
    case ModSource::bass_active:
        return "Bass note on";
    case ModSource::bass_velocity:
        return "Bass velocity";
    case ModSource::bass_gate:
        return "Bass gate ratio";
    case ModSource::bass_pitch:
        return "Bass pitch";
    case ModSource::residual_level:
        return "Residual level";
    case ModSource::residual_brightness:
        return "Residual brightness";
    case ModSource::lfo_beat:
        return "LFO, beat";
    case ModSource::lfo_bar:
        return "LFO, bar";
    case ModSource::lfo_phrase:
        return "LFO, phrase";
    case ModSource::random_bar:
        return "Random per bar";
    case ModSource::macro_1:
        return "Macro 1";
    case ModSource::macro_2:
        return "Macro 2";
    case ModSource::macro_3:
        return "Macro 3";
    case ModSource::macro_4:
        return "Macro 4";
    case ModSource::macro_5:
        return "Macro 5";
    case ModSource::macro_6:
        return "Macro 6";
    case ModSource::macro_7:
        return "Macro 7";
    case ModSource::macro_8:
        return "Macro 8";
    }
    return "?";
}

const char *mod_curve_name(ModCurve curve) {
    switch (curve) {
    case ModCurve::linear:
        return "Linear";
    case ModCurve::exponential:
        return "Exponential";
    case ModCurve::logarithmic:
        return "Logarithmic";
    case ModCurve::s_curve:
        return "S-curve";
    }
    return "?";
}

const char *mod_target_name(ModTarget target) {
    switch (target) {
    case ModTarget::opacity:
        return "Opacity";
    case ModTarget::hue_shift:
        return "Hue shift";
    case ModTarget::brightness:
        return "Brightness";
    }
    return "?";
}

const char *mod_quantise_name(ModQuantise quantise) {
    switch (quantise) {
    case ModQuantise::off:
        return "Free";
    case ModQuantise::sixteenth:
        return "Hold 1/16";
    case ModQuantise::beat:
        return "Hold beat";
    case ModQuantise::bar:
        return "Hold bar";
    }
    return "?";
}

const char *mod_quantise_id(ModQuantise quantise) {
    switch (quantise) {
    case ModQuantise::off:
        return "off";
    case ModQuantise::sixteenth:
        return "sixteenth";
    case ModQuantise::beat:
        return "beat";
    case ModQuantise::bar:
        return "bar";
    }
    return "off";
}

const char *mod_source_id(ModSource source) {
    switch (source) {
    case ModSource::constant:
        return "constant";
    case ModSource::beat_phase:
        return "beat_phase";
    case ModSource::bar_phase:
        return "bar_phase";
    case ModSource::phrase_phase:
        return "phrase_phase";
    case ModSource::section_progress:
        return "section_progress";
    case ModSource::kick:
        return "kick";
    case ModSource::bass_active:
        return "bass_active";
    case ModSource::bass_velocity:
        return "bass_velocity";
    case ModSource::bass_gate:
        return "bass_gate";
    case ModSource::bass_pitch:
        return "bass_pitch";
    case ModSource::residual_level:
        return "residual_level";
    case ModSource::residual_brightness:
        return "residual_brightness";
    case ModSource::lfo_beat:
        return "lfo_beat";
    case ModSource::lfo_bar:
        return "lfo_bar";
    case ModSource::lfo_phrase:
        return "lfo_phrase";
    case ModSource::random_bar:
        return "random_bar";
    case ModSource::macro_1:
        return "macro_1";
    case ModSource::macro_2:
        return "macro_2";
    case ModSource::macro_3:
        return "macro_3";
    case ModSource::macro_4:
        return "macro_4";
    case ModSource::macro_5:
        return "macro_5";
    case ModSource::macro_6:
        return "macro_6";
    case ModSource::macro_7:
        return "macro_7";
    case ModSource::macro_8:
        return "macro_8";
    }
    return "constant";
}

const char *mod_curve_id(ModCurve curve) {
    switch (curve) {
    case ModCurve::linear:
        return "linear";
    case ModCurve::exponential:
        return "exponential";
    case ModCurve::logarithmic:
        return "logarithmic";
    case ModCurve::s_curve:
        return "s_curve";
    }
    return "linear";
}

const char *mod_target_id(ModTarget target) {
    switch (target) {
    case ModTarget::opacity:
        return "opacity";
    case ModTarget::hue_shift:
        return "hue_shift";
    case ModTarget::brightness:
        return "brightness";
    }
    return "opacity";
}

namespace {

template <typename T, size_t N>
std::optional<T> from_id(const std::string &id, const std::array<T, N> &all,
                         const char *(*name)(T)) {
    for (const T v : all) {
        if (id == name(v)) {
            return v;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<ModSource> mod_source_from_id(const std::string &id) {
    return from_id(id, kModSources, mod_source_id);
}

std::optional<ModCurve> mod_curve_from_id(const std::string &id) {
    return from_id(id, kModCurves, mod_curve_id);
}

std::optional<ModTarget> mod_target_from_id(const std::string &id) {
    return from_id(id, kModTargets, mod_target_id);
}

std::optional<ModQuantise> mod_quantise_from_id(const std::string &id) {
    return from_id(id, kModQuantises, mod_quantise_id);
}

} // namespace psyight
