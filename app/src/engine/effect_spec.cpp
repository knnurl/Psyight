#include "engine/effect_spec.h"

#include "engine/space.h"

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
        {{"speed_hz", "Drift", "Hz", 0.0, 2.0, false, "Drift speed of the field"}, &P::speed_hz},
        {{"scale_m", "Scale", "m", 0.1, 10.0, false, "Spatial period"}, &P::scale_m},
        {{"groove_level", "Groove", "", 0.0, 1.0, false, "Level in grooves and drops"},
         &P::groove_level},
        {{"ambient_level", "Ambient", "", 0.0, 1.0, false,
          "Level in intros, breakdowns, builds and outros"},
         &P::ambient_level},
    };
    return f;
}

template <> const std::vector<Field<ShockwaveParams>> &fields<ShockwaveParams>() {
    using P = ShockwaveParams;
    static const std::vector<Field<P>> f = {
        {{"speed_m_per_s", "Speed", "m/s", 0.5, 30.0, false, "Ring speed"}, &P::speed_m_per_s},
        {{"width_m", "Width", "m", 0.02, 2.0, false, "Ring width"}, &P::width_m},
        {{"decay_s", "Decay", "s", 0.02, 2.0, false, "Brightness time constant"}, &P::decay_s},
    };
    return f;
}

template <> const std::vector<Field<ConveyorParams>> &fields<ConveyorParams>() {
    using P = ConveyorParams;
    static const std::vector<Field<P>> f = {
        {{"segment_pixels", "Segment", "px", 1.0, 100.0, true, "Segment length: one step per note"},
         nullptr,
         &P::segment_pixels},
        {{"period_segments", "Spacing", "", 2.0, 20.0, true, "Segments per lit one"},
         nullptr,
         &P::period_segments},
        {{"hue_per_semitone_deg", "Hue/semi", "deg", 0.0, 90.0, false, "Hue per semitone"},
         &P::hue_per_semitone_deg},
        {{"hue_offset_deg", "Hue base", "deg", 0.0, 360.0, false, "Hue of MIDI 0 (C-1)"},
         &P::hue_offset_deg},
        {{"release_s", "Release", "s", 0.0, 1.0, false, "Fade after the note ends"}, &P::release_s},
    };
    return f;
}

template <> const std::vector<Field<SparkleParams>> &fields<SparkleParams>() {
    using P = SparkleParams;
    static const std::vector<Field<P>> f = {
        {{"density", "Density", "", 0.0, 0.2, false, "Share of pixels lit per hat"}, &P::density},
        {{"decay_s", "Decay", "s", 0.01, 1.0, false, "Sparkle time constant"}, &P::decay_s},
    };
    return f;
}

template <> const std::vector<Field<ShimmerParams>> &fields<ShimmerParams>() {
    using P = ShimmerParams;
    static const std::vector<Field<P>> f = {
        {{"level", "Level", "", 0.0, 1.0, false, "Shimmer level"}, &P::level},
        {{"rate_hz", "Rate", "Hz", 0.0, 20.0, false, "Twinkle rate of each pixel"}, &P::rate_hz},
        {{"density", "Density", "", 0.0, 0.2, false, "Share of pixels that shimmer"}, &P::density},
        {{"fade_s", "Fade in", "s", 0.0, 0.5, false, "Fade-in time constant after a note ends"},
         &P::fade_s},
    };
    return f;
}

template <> const std::vector<Field<BuildParams>> &fields<BuildParams>() {
    using P = BuildParams;
    static const std::vector<Field<P>> f = {
        {{"wavelength_m", "Spacing", "m", 0.05, 5.0, false, "Ring spacing"}, &P::wavelength_m},
        {{"start_speed_hz", "Speed", "Hz", 0.0, 5.0, false, "Rings per second at the build start"},
         &P::start_speed_hz},
        {{"speed_ramp", "Ramp", "x", 0.0, 10.0, false, "Extra speed at the end, as a multiple"},
         &P::speed_ramp},
        {{"start_level", "Start", "", 0.0, 1.0, false, "Level at the build start"},
         &P::start_level},
        {{"end_level", "End", "", 0.0, 1.0, false, "Level at the build end"}, &P::end_level},
    };
    return f;
}

template <> const std::vector<Field<DropBurstParams>> &fields<DropBurstParams>() {
    using P = DropBurstParams;
    static const std::vector<Field<P>> f = {
        {{"speed_m_per_s", "Speed", "m/s", 0.5, 30.0, false, "Burst speed"}, &P::speed_m_per_s},
        {{"decay_s", "Decay", "s", 0.05, 3.0, false, "Burst time constant"}, &P::decay_s},
        {{"duration_s", "Duration", "s", 0.1, 5.0, false, "Burst duration after the drop starts"},
         &P::duration_s},
    };
    return f;
}

// The mapping, wings and clock settings shared by the pattern effects.
template <typename P> Field<P> mapping_field() {
    return {{"mapping", "Mapping", "", 0.0, kMappingCount - 1.0, true,
             "How pixels map to the pattern: along each strip, across the rig, from its centre, "
             "by strip or at random",
             kMappingOptions},
            nullptr,
            &P::mapping};
}

template <typename P> Field<P> wings_field() {
    return {{"wings", "Wings", "", 1.0, 4.0, true, "Mirrored copies of the pattern"},
            nullptr,
            &P::wings};
}

template <typename P> Field<P> clock_field() {
    return {{"clock", "Clock", "", 0.0, kClockCount - 1.0, true,
             "What drives the motion: free (rate in Hz), or locked to the 16th, beat, bar, "
             "phrase, bass note or kick",
             kClockOptions},
            nullptr,
            &P::clock};
}

template <> const std::vector<Field<ChaseParams>> &fields<ChaseParams>() {
    using P = ChaseParams;
    static const std::vector<Field<P>> f = {
        mapping_field<P>(),
        wings_field<P>(),
        clock_field<P>(),
        {{"rate", "Rate", "x", 0.0, 8.0, false, "Passes per clock division (Hz when free)"},
         &P::rate},
        {{"count", "Comets", "", 1.0, 16.0, true, "Comets, evenly spaced"}, nullptr, &P::count},
        {{"tail", "Tail", "", 0.01, 1.0, false, "Tail length, as a share of the mapping"},
         &P::tail},
        {{"bounce", "Motion", "", 0.0, 1.0, true, "Loop round, or bounce there and back",
          "Loop|Bounce"},
         nullptr,
         &P::bounce},
        {{"level", "Level", "", 0.0, 1.0, false, "Comet brightness"}, &P::level},
    };
    return f;
}

template <> const std::vector<Field<WaveParams>> &fields<WaveParams>() {
    using P = WaveParams;
    static const std::vector<Field<P>> f = {
        mapping_field<P>(),
        wings_field<P>(),
        clock_field<P>(),
        {{"rate", "Rate", "x", 0.0, 8.0, false, "Cycles per clock division (Hz when free)"},
         &P::rate},
        {{"shape", "Shape", "", 0.0, 4.0, true, "Waveform", "Sine|Triangle|Ramp|Square|Bump"},
         nullptr,
         &P::shape},
        {{"spread", "Spread", "", 0.0, 16.0, false, "Cycles across the mapping"}, &P::spread},
        {{"width", "Width", "", 0.01, 1.0, false, "Square duty, or bump width"}, &P::width},
        {{"level", "Level", "", 0.0, 1.0, false, "Wave brightness"}, &P::level},
        {{"colour_spread", "Colours", "", 0.0, 2.0, false, "Palette span across the mapping"},
         &P::colour_spread},
    };
    return f;
}

} // namespace

std::string effect_option_name(const EffectParamInfo &info, int value) {
    if (info.options == nullptr || value < 0) {
        return {};
    }
    const std::string all = info.options;
    size_t from = 0;
    for (int k = 0; k < value; ++k) {
        from = all.find('|', from);
        if (from == std::string::npos) {
            return {};
        }
        ++from;
    }
    const size_t to = all.find('|', from);
    return all.substr(from, to == std::string::npos ? std::string::npos : to - from);
}

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
    case EffectType::chase:
        return {ChaseParams{}};
    case EffectType::wave:
        return {WaveParams{}};
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
            } else if constexpr (std::is_same_v<P, DropBurstParams>) {
                return make_drop_burst(p);
            } else if constexpr (std::is_same_v<P, ChaseParams>) {
                return make_chase(p);
            } else {
                return make_wave(p);
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
    case EffectType::chase:
        return "chase";
    case EffectType::wave:
        return "wave";
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
