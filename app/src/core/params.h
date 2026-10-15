// Parameter registry (SPEC section 10). Every tunable value is declared once with its metadata;
// the GUI builds controls from the specs and presets serialise values by id.
//
// Threading: a ParamRegistry is owned and edited by one thread (the GUI). Other threads receive
// copies of values(), so analysis runs on a fixed, reproducible parameter set.
#pragma once

#include "core/result.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

enum class ParamTier { operate, tune, engineer };

struct ParamSpec {
    std::string id; // Stable, for example "bass.offset_threshold".
    std::string label;
    std::string unit; // For display, for example "ms" or "dB"; empty for unitless.
    double min = 0.0;
    double max = 1.0;
    double default_value = 0.0;
    double step = 0.0; // 0 means continuous.
    ParamTier tier = ParamTier::tune;
    bool modulatable = false;
    bool midi_learnable = false;
    std::string description;
    std::string group;
};

using ParamValues = std::map<std::string, double, std::less<>>;

struct PresetLoadReport {
    std::vector<std::string> unknown_ids; // Ignored, so presets survive version changes.
    std::vector<std::string> clamped_ids; // Values outside the range were clamped.
};

class ParamRegistry {
  public:
    // Fails on an empty or duplicate id, min > max, or a default outside [min, max].
    Result<void> declare(const ParamSpec &spec);

    const std::vector<ParamSpec> &specs() const { return specs_; }
    const ParamSpec *find(std::string_view id) const;

    // Precondition: the id is declared.
    double get(std::string_view id) const;
    // Clamps to the declared range. Fails for an undeclared id.
    Result<void> set(std::string_view id, double value);
    void reset_to_defaults();

    const ParamValues &values() const { return values_; }

    // Preset JSON: {"schema": 1, "values": {"id": value, ...}}.
    std::string to_preset_json() const;
    // Applies the values it knows; ids not declared are reported, not errors.
    Result<PresetLoadReport> apply_preset_json(std::string_view json);

  private:
    std::vector<ParamSpec> specs_;
    ParamValues values_;
};

} // namespace psyight
