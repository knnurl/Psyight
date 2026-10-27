// Effects as data (SPEC sections 7.3 and 10): an EffectSpec (engine/scene.h) is an effect type
// with its parameters, from which the effect is made, so scenes can be saved in the project file
// and every effect parameter edited from a generated control. Each type's parameters are
// described once here (id, label, unit, range), like the parameter registry; ids are stable
// strings used in the project file.
#pragma once

#include "engine/scene.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

inline constexpr std::array<EffectType, 20> kEffectTypes = {
    EffectType::plasma,    EffectType::shockwave, EffectType::conveyor,   EffectType::sparkle,
    EffectType::shimmer,   EffectType::build,     EffectType::drop_burst, EffectType::chase,
    EffectType::wave,      EffectType::strobe,    EffectType::stutter,    EffectType::noise,
    EffectType::fire,      EffectType::bursts,    EffectType::spiral,     EffectType::bass_bars,
    EffectType::hex_pulse, EffectType::cube,      EffectType::sectors,    EffectType::interference};

EffectSpec default_spec(EffectType type);
std::shared_ptr<const Effect> make_effect(const EffectSpec &spec);
Layer make_layer(const EffectSpec &spec, BlendMode blend, float opacity = 1.0f);

const char *effect_type_id(EffectType type); // "plasma", "drop_burst" and so on.
std::optional<EffectType> effect_type_from_id(const std::string &id);

struct EffectParamInfo {
    const char *id;
    const char *label;
    const char *unit; // "" when unitless.
    double min;
    double max;
    bool integer;
    const char *description = "";  // The long name, for tooltips (the label is short, for knobs).
    const char *options = nullptr; // An integer setting's value names, "|" separated; or none.
};

// The name of an integer setting's value (its options), or "" when it has none or is out of range.
std::string effect_option_name(const EffectParamInfo &info, int value);

std::vector<EffectParamInfo> effect_param_infos(EffectType type);
double effect_param(const EffectSpec &spec, size_t index);
void set_effect_param(EffectSpec &spec, size_t index, double value); // Clamped to the range.

// The blend a new layer of this type starts with: multiply for the stutter gate, else add.
BlendMode default_blend(EffectType type);

const char *blend_mode_id(BlendMode mode); // "add", "max", "alpha", "multiply", "screen".
std::optional<BlendMode> blend_mode_from_id(const std::string &id);
inline constexpr std::array<BlendMode, 5> kBlendModes = {
    BlendMode::add, BlendMode::max, BlendMode::alpha, BlendMode::multiply, BlendMode::screen};

} // namespace psyight
