#include "engine/effect_spec.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace psyight {

namespace {

// One parameter of a params struct: a double or an int member.
template <typename P> struct Field {
    EffectParamInfo info;
    double P::*real = nullptr;
    int P::*whole = nullptr;
};

template <typename P> const std::vector<Field<P>> &fields();

template <> const std::vector<Field<PlasmaParams>> &fields<PlasmaParams>() {
    using P = PlasmaParams;
    static const std::vector<Field<P>> f = {
        {{"speed_hz", "Drift speed", "Hz", 0.0, 2.0, false}, &P::speed_hz},
        {{"scale_m", "Spatial period", "m", 0.1, 10.0, false}, &P::scale_m},
        {{"groove_level", "Level in grooves", "", 0.0, 1.0, false}, &P::groove_level},
        {{"ambient_level", "Level elsewhere", "", 0.0, 1.0, false}, &P::ambient_level},
    };
    return f;
}

template <> const std::vector<Field<ShockwaveParams>> &fields<ShockwaveParams>() {
    using P = ShockwaveParams;
    static const std::vector<Field<P>> f = {
        {{"speed_m_per_s", "Ring speed", "m/s", 0.5, 30.0, false}, &P::speed_m_per_s},
        {{"width_m", "Ring width", "m", 0.02, 2.0, false}, &P::width_m},
        {{"decay_s", "Decay", "s", 0.02, 2.0, false}, &P::decay_s},
    };
    return f;
}

template <> const std::vector<Field<ConveyorParams>> &fields<ConveyorParams>() {
    using P = ConveyorParams;
    static const std::vector<Field<P>> f = {
        {{"segment_pixels", "Segment", "px", 1.0, 100.0, true}, nullptr, &P::segment_pixels},
        {{"period_segments", "Segments per lit one", "", 2.0, 20.0, true},
         nullptr,
         &P::period_segments},
        {{"hue_per_semitone_deg", "Hue per semitone", "deg", 0.0, 90.0, false},
         &P::hue_per_semitone_deg},
        {{"hue_offset_deg", "Hue of MIDI 0", "deg", 0.0, 360.0, false}, &P::hue_offset_deg},
        {{"release_s", "Release", "s", 0.0, 1.0, false}, &P::release_s},
    };
    return f;
}

template <> const std::vector<Field<SparkleParams>> &fields<SparkleParams>() {
    using P = SparkleParams;
    static const std::vector<Field<P>> f = {
        {{"density", "Share lit per hat", "", 0.0, 0.2, false}, &P::density},
        {{"decay_s", "Decay", "s", 0.01, 1.0, false}, &P::decay_s},
    };
    return f;
}

template <> const std::vector<Field<ShimmerParams>> &fields<ShimmerParams>() {
    using P = ShimmerParams;
    static const std::vector<Field<P>> f = {
        {{"level", "Level", "", 0.0, 1.0, false}, &P::level},
        {{"rate_hz", "Twinkle rate", "Hz", 0.0, 20.0, false}, &P::rate_hz},
        {{"density", "Share of pixels", "", 0.0, 0.2, false}, &P::density},
        {{"fade_s", "Fade in", "s", 0.0, 0.5, false}, &P::fade_s},
    };
    return f;
}

template <> const std::vector<Field<BuildParams>> &fields<BuildParams>() {
    using P = BuildParams;
    static const std::vector<Field<P>> f = {
        {{"wavelength_m", "Ring spacing", "m", 0.05, 5.0, false}, &P::wavelength_m},
        {{"start_speed_hz", "Start speed", "Hz", 0.0, 5.0, false}, &P::start_speed_hz},
        {{"speed_ramp", "Speed ramp", "x", 0.0, 10.0, false}, &P::speed_ramp},
        {{"start_level", "Start level", "", 0.0, 1.0, false}, &P::start_level},
        {{"end_level", "End level", "", 0.0, 1.0, false}, &P::end_level},
    };
    return f;
}

template <> const std::vector<Field<DropBurstParams>> &fields<DropBurstParams>() {
    using P = DropBurstParams;
    static const std::vector<Field<P>> f = {
        {{"speed_m_per_s", "Speed", "m/s", 0.5, 30.0, false}, &P::speed_m_per_s},
        {{"decay_s", "Decay", "s", 0.05, 3.0, false}, &P::decay_s},
        {{"duration_s", "Duration", "s", 0.1, 5.0, false}, &P::duration_s},
    };
    return f;
}

} // namespace

EffectSpec default_spec(EffectType type) {
    switch (type) {
    case EffectType::plasma:
        return {PlasmaParams{}};
    case EffectType::shockwave:
        return {ShockwaveParams{}};
    case EffectType::conveyor:
        return {ConveyorParams{}};
    case EffectType::sparkle:
        return {SparkleParams{}};
    case EffectType::shimmer:
        return {ShimmerParams{}};
    case EffectType::build:
        return {BuildParams{}};
    case EffectType::drop_burst:
        return {DropBurstParams{}};
    }
    return {PlasmaParams{}};
}

std::shared_ptr<const Effect> make_effect(const EffectSpec &spec) {
    return std::visit(
        [](const auto &p) -> std::shared_ptr<const Effect> {
            using P = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<P, PlasmaParams>) {
                return make_plasma(p);
            } else if constexpr (std::is_same_v<P, ShockwaveParams>) {
                return make_shockwave(p);
            } else if constexpr (std::is_same_v<P, ConveyorParams>) {
                return make_conveyor(p);
            } else if constexpr (std::is_same_v<P, SparkleParams>) {
                return make_sparkle(p);
            } else if constexpr (std::is_same_v<P, ShimmerParams>) {
                return make_shimmer(p);
            } else if constexpr (std::is_same_v<P, BuildParams>) {
                return make_build(p);
            } else {
                return make_drop_burst(p);
            }
        },
        spec.params);
}

Layer make_layer(const EffectSpec &spec, BlendMode blend, float opacity) {
    return {make_effect(spec), blend, opacity, true, spec};
}

const char *effect_type_id(EffectType type) {
    switch (type) {
    case EffectType::plasma:
        return "plasma";
    case EffectType::shockwave:
        return "shockwave";
    case EffectType::conveyor:
        return "conveyor";
    case EffectType::sparkle:
        return "sparkle";
    case EffectType::shimmer:
        return "shimmer";
    case EffectType::build:
        return "build";
    case EffectType::drop_burst:
        return "drop_burst";
    }
    return "plasma";
}

std::optional<EffectType> effect_type_from_id(const std::string &id) {
    for (const EffectType t : kEffectTypes) {
        if (id == effect_type_id(t)) {
            return t;
        }
    }
    return std::nullopt;
}

std::vector<EffectParamInfo> effect_param_infos(EffectType type) {
    return std::visit(
        [](const auto &p) {
            using P = std::decay_t<decltype(p)>;
            std::vector<EffectParamInfo> out;
            for (const Field<P> &f : fields<P>()) {
                out.push_back(f.info);
            }
            return out;
        },
        default_spec(type).params);
}

double effect_param(const EffectSpec &spec, size_t index) {
    return std::visit(
        [index](const auto &p) -> double {
            using P = std::decay_t<decltype(p)>;
            const std::vector<Field<P>> &f = fields<P>();
            if (index >= f.size()) {
                return 0.0;
            }
            return f[index].real != nullptr ? p.*(f[index].real)
                                            : static_cast<double>(p.*(f[index].whole));
        },
        spec.params);
}

void set_effect_param(EffectSpec &spec, size_t index, double value) {
    std::visit(
        [index, value](auto &p) {
            using P = std::decay_t<decltype(p)>;
            const std::vector<Field<P>> &f = fields<P>();
            if (index >= f.size() || !std::isfinite(value)) {
                return;
            }
            const double v = std::clamp(value, f[index].info.min, f[index].info.max);
            if (f[index].real != nullptr) {
                p.*(f[index].real) = v;
            } else {
                p.*(f[index].whole) = static_cast<int>(std::lround(v));
            }
        },
        spec.params);
}

const char *blend_mode_id(BlendMode mode) {
    switch (mode) {
    case BlendMode::add:
        return "add";
    case BlendMode::max:
        return "max";
    case BlendMode::alpha:
        return "alpha";
    case BlendMode::multiply:
        return "multiply";
    case BlendMode::screen:
        return "screen";
    }
    return "add";
}

std::optional<BlendMode> blend_mode_from_id(const std::string &id) {
    for (const BlendMode m : kBlendModes) {
        if (id == blend_mode_id(m)) {
            return m;
        }
    }
    return std::nullopt;
}

} // namespace psyight
