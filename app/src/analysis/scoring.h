// Bass note scoring against ground-truth labels (SPEC section 6.10).
//
// Conventions follow mir_eval.transcription (checked against its source, 2026-10-15) so that the
// Python cross-check can reproduce the numbers:
// - Notes match one-to-one on onset only: |est - ref| <= onset tolerance, distances rounded to
//   0.1 ms, inclusive. The matching has maximum cardinality.
// - Offset hit: |est offset - ref offset| <= max(offset_ratio * ref duration, min tolerance).
// - P, R and F are 0 when either list is empty.
// Pitch, offset and gate-ratio statistics are computed over matched pairs. Statistics with no
// matched pairs are NaN.
#pragma once

#include "io/labels.h"

#include <vector>

namespace psyight {

struct ScoringParams {
    double onset_tolerance_s = 0.020; // mir_eval default is 0.050, too loose for 94 ms slots.
    double offset_ratio = 0.2;
    double offset_min_tolerance_s = 0.050;
    double pitch_tolerance_cents = 50.0;
};

struct NoteScore {
    int reference_count = 0;
    int estimate_count = 0;
    int matched_count = 0;
    double onset_precision = 0.0;
    double onset_recall = 0.0;
    double onset_f = 0.0;

    std::vector<double> offset_errors_s; // est - ref for each matched pair, in reference order.
    double offset_error_median_s = 0.0;  // Signed: positive means estimates end late.
    double offset_error_abs_median_s = 0.0;
    double offset_error_abs_p90_s = 0.0;
    double offset_hit_rate = 0.0;

    int pitch_correct_count = 0;
    int octave_error_count = 0; // Off by a whole number of octaves, within the pitch tolerance.
    double pitch_accuracy = 0.0;
    double octave_error_rate = 0.0;

    double gate_ratio_mae = 0.0;
};

struct RegionScore {
    LabelRegion region;
    NoteScore score;
};

struct TrackScore {
    std::vector<RegionScore> regions; // Complete regions only, in label order.
    NoteScore total;                  // All notes with onsets inside any complete region.
};

// Index pairs (reference, estimate) of the onset matching.
struct NoteMatch {
    int reference_index = 0;
    int estimate_index = 0;
};

std::vector<NoteMatch> match_note_onsets(const std::vector<BassNoteLabel> &references,
                                         const std::vector<BassNoteLabel> &estimates,
                                         double onset_tolerance_s);

// Scores estimates against references. slot_len_s is one 16th note, used for gate ratios.
NoteScore score_notes(const std::vector<BassNoteLabel> &references,
                      const std::vector<BassNoteLabel> &estimates, double slot_len_s,
                      const ScoringParams &params);

// Scores each complete region of the reference labels, and all complete regions together. A note
// belongs to a region when its onset lies in [start_s, end_s). Incomplete regions are skipped
// because unlabelled notes there would count as false positives.
TrackScore score_track(const Labels &reference, const std::vector<BassNoteLabel> &estimates,
                       const ScoringParams &params);

// Linear-interpolation percentile (numpy's default), q in [0, 1]. NaN for an empty input.
double percentile(std::vector<double> values, double q);

} // namespace psyight
