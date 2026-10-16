#include "analysis/residual.h"

#include "analysis/series.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr int kPeakHalfWidthFrames = 2;
constexpr double kPowerFloor = 1e-12;

int band_index(const FrontEndResult &features, const char *name) {
    for (size_t b = 0; b < features.bands.size(); ++b) {
        if (features.bands[b].band.name == name) {
            return static_cast<int>(b);
        }
    }
    return -1;
}

} // namespace

Result<ResidualFeatures> analyse_residual(const FrontEndResult &f, const ResidualParams &p) {
    const int hats = band_index(f, "hats");
    const int lowmid = band_index(f, "low mid");
    if (hats < 0 || lowmid < 0) {
        return Error{"residual analysis needs the front-end bands hats and low mid"};
    }
    ResidualFeatures result;
    const double frame_s = static_cast<double>(f.hop_samples) / f.sample_rate_hz;

    // Hats: adaptive-threshold peaks of the hats band flux.
    const std::vector<float> &flux = f.bands[hats].flux;
    const double norm = quantile(flux, 0.99);
    if (norm > 0.0) {
        const auto half = static_cast<int>(std::lround(p.hat_mean_window_ms * 1e-3 / frame_s / 2));
        const std::vector<float> mean = box_mean(flux, half);
        for (int i = 0; i < f.frames; ++i) {
            const double value = flux[i] / norm;
            if (value < p.hat_floor || value < mean[i] / norm + p.hat_delta) {
                continue;
            }
            bool peak = true;
            for (int k = 1; k <= kPeakHalfWidthFrames && peak; ++k) {
                peak = (i - k < 0 || flux[i - k] <= flux[i]) &&
                       (i + k >= f.frames || flux[i + k] < flux[i]);
            }
            if (!peak) {
                continue;
            }
            const double position = i > 0 && i + 1 < f.frames
                                        ? i + parabolic_offset(flux[i - 1], flux[i], flux[i + 1])
                                        : i;
            const HatOnset onset{position * frame_s, value};
            if (!result.hats.empty() &&
                onset.t_s - result.hats.back().t_s < p.hat_min_gap_ms * 1e-3) {
                if (onset.strength > result.hats.back().strength) {
                    result.hats.back() = onset; // The stronger of two close hits.
                }
                continue;
            }
            result.hats.push_back(onset);
        }
    }

    // Residual: the log spectrogram above min_hz.
    const LogSpectrogram &spec = f.spectrogram;
    int first_row = 0;
    while (first_row < spec.rows && spec.row_hz[first_row] < p.residual_min_hz) {
        ++first_row;
    }
    const int rows = spec.rows - first_row;
    result.energy_db.resize(f.frames);
    result.centroid_hz.resize(f.frames);
    result.flatness.resize(f.frames);
    result.flux = f.bands[lowmid].flux;
    for (int i = 0; i < f.frames && rows > 0; ++i) {
        double power_sum = 0.0;
        double weighted_hz = 0.0;
        double log_sum = 0.0;
        for (int r = first_row; r < spec.rows; ++r) {
            const double power =
                std::max(std::pow(10.0, spec.db[static_cast<size_t>(i) * spec.rows + r] / 10.0),
                         kPowerFloor);
            power_sum += power;
            weighted_hz += power * spec.row_hz[r];
            log_sum += std::log(power);
        }
        const double mean_power = power_sum / rows;
        result.energy_db[i] = static_cast<float>(10.0 * std::log10(mean_power));
        result.centroid_hz[i] =
            mean_power > 10.0 * kPowerFloor ? static_cast<float>(weighted_hz / power_sum) : 0.0f;
        result.flatness[i] = static_cast<float>(std::exp(log_sum / rows) / mean_power);
    }
    return result;
}

namespace {

struct ParamEntry {
    double ResidualParams::*member;
    const char *id;
    const char *label;
    const char *unit;
    double min;
    double max;
    ParamTier tier;
    const char *description;
};

const ResidualParams kDefaults;

// clang-format off
const ParamEntry kParamTable[] = {
    {&ResidualParams::hat_delta, "residual.hat_delta", "Hat sensitivity", "", 0.0, 1.0, ParamTier::tune, "Hats band flux peak above its local mean, as a fraction of the track's 99th percentile. Lower finds more hats."},
    {&ResidualParams::hat_floor, "residual.hat_floor", "Hat floor", "", 0.0, 1.0, ParamTier::engineer, "Minimum hats band flux peak, as a fraction of the track's 99th percentile."},
    {&ResidualParams::hat_mean_window_ms, "residual.hat_mean_window_ms", "Hat mean window", "ms", 10.0, 500.0, ParamTier::engineer, "Length of the centred local mean for the hat threshold."},
    {&ResidualParams::hat_min_gap_ms, "residual.hat_min_gap_ms", "Hat minimum gap", "ms", 5.0, 200.0, ParamTier::engineer, "Of two hats closer than this, the stronger is kept."},
    {&ResidualParams::residual_min_hz, "residual.min_hz", "Residual low edge", "Hz", 100.0, 4000.0, ParamTier::engineer, "The residual (non-bass content) starts at this frequency."},
};
// clang-format on

} // namespace

Result<void> declare_residual_params(ParamRegistry &registry) {
    for (const ParamEntry &entry : kParamTable) {
        ParamSpec spec;
        spec.id = entry.id;
        spec.label = entry.label;
        spec.unit = entry.unit;
        spec.min = entry.min;
        spec.max = entry.max;
        spec.default_value = kDefaults.*entry.member;
        spec.tier = entry.tier;
        spec.description = entry.description;
        spec.group = "Hats and residual";
        if (Result<void> declared = registry.declare(spec); !declared) {
            return declared;
        }
    }
    return {};
}

ResidualParams residual_params_from(const ParamValues &values) {
    ResidualParams params;
    for (const ParamEntry &entry : kParamTable) {
        if (const auto it = values.find(entry.id); it != values.end()) {
            params.*entry.member = it->second;
        }
    }
    return params;
}

} // namespace psyight
