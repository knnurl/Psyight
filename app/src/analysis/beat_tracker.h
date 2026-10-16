// Beat, downbeat and phrase tracker (SPEC section 6.5), file mode.
//
// Steps:
// 1. Beat onset function: the geometric mean of the low band's (sub plus bass) level rise and the
//    click band's flux, each normalised to the track. A kick has both; a bass onset has little
//    click and a hat has no low rise. The level is averaged over one bass period first (as in
//    the bass tracker) and the rise is a centred difference, so it peaks at the kick.
//    Onsets whose click is weak against their low-mid flux (bass attacks) are weighted down.
// 2. Tempo: autocorrelation of the onset function with a harmonic comb (1, 2 and 4 beats),
//    weighted by a log-Gaussian prior and limited to min_bpm..max_bpm, which excludes the half and
//    double tempos of psytrance.
// 3. Beats: dynamic programming (Ellis 2007): each beat maximises onset strength minus a cost on
//    log-period deviation, so beats continue through breakdowns.
// 4. Grid: the constant-tempo period and phase whose beats collect the most onset strength over
//    the whole track (grid search), refined by least squares on the strong beats near it. DJ
//    mixes with tempo changes need the per-beat times instead (M5).
// 5. Downbeat and phrase: breakdowns start and end on bar lines, so where the kick returns (and,
//    at half weight, stops) after a break of a bar or more votes for the downbeat, as does every
//    isolated kick (kick-on-one sections); breaks of two bars or more vote for the phrase
//    position. Without breaks, the beat (and bar) positions where
//    the music changes most between the preceding and following bars decide (log-spectrogram
//    band energies and bass pitch salience, each group scaled over the track). Phrases are
//    counted in groups of bars_per_phrase.
//
// Assumptions: 4/4, a roughly constant tempo, kicks with a click transient. Deterministic.
// Latency: non-causal (file mode); uses the whole track.
#pragma once

#include "analysis/front_end.h"
#include "core/params.h"
#include "core/result.h"
#include "core/timebase.h"

#include <vector>

namespace psyight {

struct BeatTrackerParams {
    double min_bpm = 120.0;
    double max_bpm = 190.0;
    double prior_bpm = 150.0;
    double prior_width_octaves = 0.5;   // Standard deviation of the log2 tempo prior.
    double tightness = 100.0;           // Cost weight on squared log-period deviation (Ellis 2007).
    double click_ratio_full = 0.2;      // Click / low-mid flux ratio counting fully as a kick.
    double envelope_window_ms = 25.0;   // Low-band level averaged over about one bass period.
    double fit_strength_quantile = 0.5; // Beats stronger than this quantile anchor the grid fit.
    double fit_outlier_ms = 30.0;       // Beats further than this from the grid are not fitted.
    double beat_bias_ms = 0.0;          // Subtracted from beat times.
    double kick_presence_fraction = 0.3; // Of the strong beats' onset strength (p90).
    double bars_per_phrase = 8.0;
};

struct BeatTrackResult {
    double bpm = 0.0;                 // Of the constant-tempo grid fit.
    double tempo_confidence = 0.0;    // 0 to 1: autocorrelation peak margin.
    std::vector<double> beat_times_s; // Dynamic-programming beats.
    Grid grid; // Constant tempo; first_downbeat is the first one at 0 or later.
    double downbeat_confidence = 0.0; // 0 to 1: margin of the best beat position over the next.
    int downbeat_cues = 0;            // Kick breaks and isolated kicks; 0: decided by novelty.
    int phrase_bar_offset = 0;        // Bars from grid.first_downbeat to the first phrase start.
    double phrase_confidence = 0.0;
    int phrase_cues = 0;
    // Kick presence on every grid beat from first_kick_beat_s on (the grid's first beat at 0 s or
    // later), one entry per beat; sections are derived from it.
    double first_kick_beat_s = 0.0;
    std::vector<bool> beat_has_kick;
    std::vector<float> onset_function; // Per front-end frame, normalised.
};

Result<BeatTrackResult> track_beats(const FrontEndResult &features,
                                    const BeatTrackerParams &params);

// Registry declarations ("beat.*") with the defaults above, and the reverse mapping.
Result<void> declare_beat_tracker_params(ParamRegistry &registry);
BeatTrackerParams beat_tracker_params_from(const ParamValues &values);

} // namespace psyight
