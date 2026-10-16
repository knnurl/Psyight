// Hats and residual features (SPEC sections 6.2 and 6.4), file mode.
//
// Hats: onsets of the 8 to 12 kHz band's spectral flux (front end), by adaptive-threshold peak
// picking normalised to the track, with a minimum gap. A kick's click reaches this band too, so
// hits coinciding with kicks are kept: a sparkle layer may want them, and a consumer can drop them
// against the kick times.
// Residual: what sounds besides the bass, per front-end frame, from the log spectrogram above
// min_hz (default 400 Hz, above the bass band): energy in dB, spectral centroid in Hz, spectral
// flatness (geometric over arithmetic mean of power, 0 tonal to 1 noise), and the low-mid flux.
// It drives the light during bass rests (SPEC 6.3).
// Assumptions: the front end's default bands (hats, low mid). Deterministic. Latency: non-causal
// peak picking (file mode).
#pragma once

#include "analysis/front_end.h"
#include "core/params.h"
#include "core/result.h"

#include <vector>

namespace psyight {

struct ResidualParams {
    double hat_delta = 0.1; // Above the local mean, as a fraction of the track's p99.
    double hat_floor = 0.1; // Minimum peak, as a fraction of the track's p99.
    double hat_mean_window_ms = 100.0;
    double hat_min_gap_ms = 40.0;
    double residual_min_hz = 400.0; // Lowest spectrogram row of the residual.
};

struct HatOnset {
    double t_s = 0.0;
    double strength = 0.0; // Peak flux over the track's p99.
};

struct ResidualFeatures {
    std::vector<HatOnset> hats;
    std::vector<float> energy_db;   // Per frame.
    std::vector<float> centroid_hz; // Per frame; 0 in silence.
    std::vector<float> flatness;    // Per frame, 0 to 1.
    std::vector<float> flux;        // Per frame: the front end's low-mid band flux.
};

Result<ResidualFeatures> analyse_residual(const FrontEndResult &features,
                                          const ResidualParams &params);

Result<void> declare_residual_params(ParamRegistry &registry);
ResidualParams residual_params_from(const ParamValues &values);

} // namespace psyight
