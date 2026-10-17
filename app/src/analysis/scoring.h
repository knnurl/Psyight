// Bass note scoring against ground-truth labels (SPEC section 6.10).
//
// Matching and counts follow mir_eval.transcription (checked against its source, and against
// mir_eval 0.8.2 itself on 3576 cases with identical matched counts and P/R/F, 2026-10-15):
// - Notes match one-to-one on onset only: |est - ref| <= onset tolerance, with distances rounded
//   half-to-even to 0.1 ms (numpy's np.around), inclusive. The matching has maximum cardinality.
// - Offset hit: |est offset - ref offset| <= max(offset_ratio * ref duration, min tolerance),
//   rounded the same way.
// - P, R and F are 0 when either list is empty.
// Where mir_eval stops:
// - Among the maximum matchings, mir_eval's pairing is arbitrary. Here the pairing is the one with
//   the least total onset distance, so an exact estimate wins over an early double trigger.
//   Pitch, offset and gate-ratio statistics use these pairs; a cross-check must use the same rule.
// - Pitch is compared in exact cents (midi * 100 + cents), inclusive at the tolerance; mir_eval
//   converts to Hz and back, which puts an exact 50-cent error just outside.
// Statistics with no matched pairs are NaN.
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

// Scores each complete region of the reference labels, and all complete regions together.
// A note belongs to a region when its onset lies in [start_s, end_s). Matching also sees notes up
// to one onset tolerance outside the region, so a detection at an edge pairs with the right note:
// an estimate just outside matching a reference inside is a hit, and an estimate inside matching a
// reference just outside is ignored (it belongs to the neighbouring note). Unmatched estimates are
// false positives only inside the region. Incomplete regions are skipped because unlabelled
// notes there would count as false positives.
TrackScore score_track(const Labels &reference, const std::vector<BassNoteLabel> &estimates,
                       const ScoringParams &params);

// One scoring error, for finding and listening to what went wrong.
enum class NoteErrorKind { missed, false_positive, octave, pitch, offset };

struct NoteError {
    NoteErrorKind kind = NoteErrorKind::missed;
    double time_s = 0.0;     // The reference's onset; the estimate's for a false positive.
    BassNoteLabel reference; // Default for a false positive.
    BassNoteLabel estimate;  // Default for a missed note.
    double error = 0.0;      // Pitch kinds: est - ref in cents. Offset: est - ref in s. Else 0.
};

// Every error in the complete regions, with score_track's matching: missed notes and false
// positives (by time), then octave and other pitch errors, then offsets outside the offset
// tolerance (each largest first). A matched note can appear twice: once for pitch, once for its
// offset.
std::vector<NoteError> list_note_errors(const Labels &reference,
                                        const std::vector<BassNoteLabel> &estimates,
                                        const ScoringParams &params);

// Linear-interpolation percentile (numpy's default), q in [0, 1]. NaN for an empty input.
double percentile(std::vector<double> values, double q);

} // namespace psyight
