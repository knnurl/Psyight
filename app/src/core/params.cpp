#include "core/params.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr int kPresetSchemaVersion = 1;

double clamp_to_spec(const ParamSpec &spec, double value) {
    return std::clamp(value, spec.min, spec.max);
}

} // namespace

Result<void> ParamRegistry::declare(const ParamSpec &spec) {
    if (spec.id.empty()) {
        return Error{"parameter id is empty"};
    }
    if (find(spec.id) != nullptr) {
        return Error{"duplicate parameter id " + spec.id};
    }
    if (!(spec.min <= spec.max) || !(spec.default_value >= spec.min) ||
        !(spec.default_value <= spec.max)) {
        return Error{"parameter " + spec.id + " has an invalid range or default"};
    }
    specs_.push_back(spec);
    values_[spec.id] = spec.default_value;
    return {};
}

const ParamSpec *ParamRegistry::find(std::string_view id) const {
    const auto it = std::find_if(specs_.begin(), specs_.end(),
                                 [id](const ParamSpec &spec) { return spec.id == id; });
    return it == specs_.end() ? nullptr : &*it;
}

double ParamRegistry::get(std::string_view id) const { return values_.find(id)->second; }

Result<void> ParamRegistry::set(std::string_view id, double value) {
    const ParamSpec *spec = find(id);
    if (spec == nullptr) {
        return Error{"unknown parameter id " + std::string(id)};
    }
    if (std::isnan(value)) {
        return Error{"parameter " + spec->id + " cannot be NaN"};
    }
    values_.find(id)->second = clamp_to_spec(*spec, value);
    return {};
}

void ParamRegistry::reset_to_defaults() {
    for (const ParamSpec &spec : specs_) {
        values_[spec.id] = spec.default_value;
    }
}

std::string ParamRegistry::to_preset_json() const {
    nlohmann::json values = nlohmann::json::object();
    for (const auto &[id, value] : values_) {
        values[id] = value;
    }
    return nlohmann::json{{"schema", kPresetSchemaVersion}, {"values", values}}.dump(2) + "\n";
}

Result<PresetLoadReport> ParamRegistry::apply_preset_json(std::string_view json) {
    try {
        const nlohmann::json root = nlohmann::json::parse(json);
        if (root.at("schema").get<int>() != kPresetSchemaVersion) {
            return Error{"unsupported preset schema"};
        }
        // Validate everything before changing any value, so a bad preset changes nothing.
        std::vector<std::pair<const ParamSpec *, double>> updates;
        PresetLoadReport report;
        for (const auto &[id, value] : root.at("values").items()) {
            const ParamSpec *spec = find(id);
            if (spec == nullptr) {
                report.unknown_ids.push_back(id);
                continue;
            }
            const double number = value.get<double>(); // JSON has no NaN.
            if (clamp_to_spec(*spec, number) != number) {
                report.clamped_ids.push_back(id);
            }
            updates.emplace_back(spec, number);
        }
        for (const auto &[spec, number] : updates) {
            values_[spec->id] = clamp_to_spec(*spec, number);
        }
        return report;
    } catch (const nlohmann::json::exception &e) {
        return Error{std::string("invalid preset JSON: ") + e.what()};
    }
}

} // namespace psyight
