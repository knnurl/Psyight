#include "analysis/scoring.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace psyight {

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kCentsPerSemitone = 100.0;
constexpr double kCentsPerOctave = 1200.0;

// mir_eval rounds time distances to 4 decimals (0.1 ms) before comparing, so that a distance of
// exactly the tolerance still matches despite floating-point error.
double rounded_distance_s(double a_s, double b_s) {
    return std::round(std::abs(a_s - b_s) * 1e4) / 1e4;
}

std::vector<int> indices_sorted_by_onset(const std::vector<BassNoteLabel> &notes) {
    std::vector<int> order(notes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&notes](int a, int b) { return notes[a].onset_s < notes[b].onset_s; });
    return order;
}

double pitch_cents(const BassNoteLabel &note) { return note.midi * kCentsPerSemitone + note.cents; }

double f_measure(double precision, double recall) {
    return precision + recall == 0.0 ? 0.0 : 2.0 * precision * recall / (precision + recall);
}

std::vector<BassNoteLabel> notes_in(const std::vector<BassNoteLabel> &notes,
                                    const std::vector<LabelRegion> &regions) {
    std::vector<BassNoteLabel> selected;
    for (const BassNoteLabel &note : notes) {
        const bool inside = std::any_of(regions.begin(), regions.end(), [&](const LabelRegion &r) {
            return note.onset_s >= r.start_s && note.onset_s < r.end_s;
        });
        if (inside) {
            selected.push_back(note);
        }
    }
    return selected;
}

} // namespace

std::vector<NoteMatch> match_note_onsets(const std::vector<BassNoteLabel> &references,
                                         const std::vector<BassNoteLabel> &estimates,
                                         double onset_tolerance_s) {
    // Each note's match window is an interval of equal width on the time line. For such interval
    // graphs, matching every reference (in time order) to the earliest still-unmatched estimate
    // inside its window gives a maximum-cardinality matching, the same count as mir_eval's
    // Hopcroft-Karp matching.
    const std::vector<int> refs = indices_sorted_by_onset(references);
    const std::vector<int> ests = indices_sorted_by_onset(estimates);
    std::vector<NoteMatch> matches;
    size_t next_est = 0;
    for (const int ref : refs) {
        const double ref_onset_s = references[ref].onset_s;
        // Estimates too early for this reference are too early for every later one as well.
        while (next_est < ests.size() && estimates[ests[next_est]].onset_s < ref_onset_s &&
               rounded_distance_s(estimates[ests[next_est]].onset_s, ref_onset_s) >
                   onset_tolerance_s) {
            ++next_est;
        }
        if (next_est < ests.size() && rounded_distance_s(estimates[ests[next_est]].onset_s,
                                                         ref_onset_s) <= onset_tolerance_s) {
            matches.push_back({ref, ests[next_est]});
            ++next_est;
        }
    }
    return matches;
}

NoteScore score_notes(const std::vector<BassNoteLabel> &references,
                      const std::vector<BassNoteLabel> &estimates, double slot_len_s,
                      const ScoringParams &params) {
    NoteScore score;
    score.reference_count = static_cast<int>(references.size());
    score.estimate_count = static_cast<int>(estimates.size());
    const std::vector<NoteMatch> matches =
        match_note_onsets(references, estimates, params.onset_tolerance_s);
    score.matched_count = static_cast<int>(matches.size());
    if (!references.empty() && !estimates.empty()) {
        score.onset_precision = static_cast<double>(score.matched_count) / score.estimate_count;
        score.onset_recall = static_cast<double>(score.matched_count) / score.reference_count;
        score.onset_f = f_measure(score.onset_precision, score.onset_recall);
    }
    if (matches.empty()) {
        score.offset_error_median_s = kNan;
        score.offset_error_abs_median_s = kNan;
        score.offset_error_abs_p90_s = kNan;
        score.offset_hit_rate = kNan;
        score.pitch_accuracy = kNan;
        score.octave_error_rate = kNan;
        score.gate_ratio_mae = kNan;
        return score;
    }

    int offset_hits = 0;
    double gate_ratio_error_sum = 0.0;
    std::vector<double> abs_offset_errors_s;
    for (const NoteMatch &match : matches) {
        const BassNoteLabel &ref = references[match.reference_index];
        const BassNoteLabel &est = estimates[match.estimate_index];

        const double offset_error_s = est.offset_s - ref.offset_s;
        score.offset_errors_s.push_back(offset_error_s);
        abs_offset_errors_s.push_back(std::abs(offset_error_s));
        const double ref_duration_s = ref.offset_s - ref.onset_s;
        const double offset_tolerance_s =
            std::max(params.offset_ratio * ref_duration_s, params.offset_min_tolerance_s);
        if (rounded_distance_s(est.offset_s, ref.offset_s) <= offset_tolerance_s) {
            ++offset_hits;
        }

        const double pitch_error_cents = pitch_cents(est) - pitch_cents(ref);
        const double octaves = std::round(pitch_error_cents / kCentsPerOctave);
        if (std::abs(pitch_error_cents) <= params.pitch_tolerance_cents) {
            ++score.pitch_correct_count;
        } else if (octaves != 0.0 && std::abs(pitch_error_cents - octaves * kCentsPerOctave) <=
                                         params.pitch_tolerance_cents) {
            ++score.octave_error_count;
        }

        const double ref_gate_ratio = ref_duration_s / slot_len_s;
        const double est_gate_ratio = (est.offset_s - est.onset_s) / slot_len_s;
        gate_ratio_error_sum += std::abs(est_gate_ratio - ref_gate_ratio);
    }

    const double matched = static_cast<double>(score.matched_count);
    score.offset_error_median_s = percentile(score.offset_errors_s, 0.5);
    score.offset_error_abs_median_s = percentile(abs_offset_errors_s, 0.5);
    score.offset_error_abs_p90_s = percentile(abs_offset_errors_s, 0.9);
    score.offset_hit_rate = offset_hits / matched;
    score.pitch_accuracy = score.pitch_correct_count / matched;
    score.octave_error_rate = score.octave_error_count / matched;
    score.gate_ratio_mae = gate_ratio_error_sum / matched;
    return score;
}

TrackScore score_track(const Labels &reference, const std::vector<BassNoteLabel> &estimates,
                       const ScoringParams &params) {
    const double slot_len_s = 60.0 / reference.grid.bpm / 4.0;
    TrackScore track;
    std::vector<LabelRegion> complete_regions;
    for (const LabelRegion &region : reference.regions) {
        if (!region.complete) {
            continue;
        }
        complete_regions.push_back(region);
        const std::vector<LabelRegion> just_this{region};
        track.regions.push_back(
            {region, score_notes(notes_in(reference.bass_notes, just_this),
                                 notes_in(estimates, just_this), slot_len_s, params)});
    }
    track.total = score_notes(notes_in(reference.bass_notes, complete_regions),
                              notes_in(estimates, complete_regions), slot_len_s, params);
    return track;
}

double percentile(std::vector<double> values, double q) {
    if (values.empty()) {
        return kNan;
    }
    std::sort(values.begin(), values.end());
    const double position = q * static_cast<double>(values.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = std::min(lower + 1, values.size() - 1);
    const double fraction = position - static_cast<double>(lower);
    return values[lower] + fraction * (values[upper] - values[lower]);
}

} // namespace psyight
