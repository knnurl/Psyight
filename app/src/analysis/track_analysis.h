// Whole-track file-mode analysis of one audio file: decode, front end, beat grid, kicks, bass
// notes and sections, from the mix or from offline Demucs stems (SPEC 6.8 file mode).
//
// Source mix: everything from the file itself.
// Source stems: beats, kicks and hats from the drums stem, bass notes from the bass stem (with
// the drums' kicks as its masks), the residual from the "other" stem (SPEC 6.4), and sections
// from the drums' kicks and the mix's high band. Stems
// come from tools/python/separate.py (io/stems.h) and are first aligned to the mix by
// cross-correlation (analysis/align.h): another decoder can shift them by tens of milliseconds.
// Deterministic for the same files and parameters. Not real-time: file mode only.
#pragma once

#include "analysis/bass_aggregates.h"
#include "analysis/bass_tracker.h"
#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "analysis/residual.h"
#include "analysis/sections.h"
#include "core/result.h"
#include "core/timebase.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace psyight {

enum class AnalysisSource { mix, stems };

struct TrackAnalysisOptions {
    AnalysisSource source = AnalysisSource::mix;
    std::optional<Grid> grid; // Set: the bass tracker uses it (a labelled grid), not the beats'.
    FrontEndParams front_end;
    BeatTrackerParams beats;
    BassTrackerParams bass;
    SectionParams sections;
    ResidualParams residual;
};

struct TrackAnalysis {
    AnalysisSource source = AnalysisSource::mix;
    int sample_rate_hz = 0;
    double duration_s = 0.0;
    FrontEndResult mix_features;
    BeatTrackResult beats;
    Grid grid; // The grid the bass tracker used.
    BassTrackResult bass;
    std::vector<DetectedSection> sections;
    std::vector<SectionBar> section_bars; // What the sections were found from (and edited ones'
                                          // energy).
    std::vector<float> riser;             // Per mix frame, 0 to 1 (riser_curve).
    std::vector<double> phrase_starts_s;  // Restarting at section changes (SPEC 6.5).
    ResidualFeatures residual;            // Hats and the non-bass features.
    BassAggregates aggregates;            // Per bar and phrase of `grid`.
    std::vector<float> intensity;         // Per section bar, 0 to 1 (analysis/intensity.h).
    double stems_lag_s = 0.0; // Stems against the mix before alignment; negative: ahead.
};

// Fails when the audio (or, for stems, the drums or bass stem) cannot be decoded, or when the
// beat tracker finds no tempo and no grid was given.
Result<TrackAnalysis> analyse_track(const std::filesystem::path &audio_path,
                                    const TrackAnalysisOptions &options);

// The same for a mix already decoded (mono, at sample_rate_hz); audio_path locates its stems.
Result<TrackAnalysis> analyse_track(std::span<const float> mix, int sample_rate_hz,
                                    const std::filesystem::path &audio_path,
                                    const TrackAnalysisOptions &options);

} // namespace psyight
