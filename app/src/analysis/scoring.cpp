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

// mir_eval rounds time distances to 4 decimals (0.1 ms) with np.around, which rounds halves to
// even, before comparing. std::nearbyint in the default rounding mode does the same.
double rounded_distance_s(double a_s, double b_s) {
    return std::nearbyint(std::abs(a_s - b_s) * 1e4) / 1e4;
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

// Best matching of one cluster: refs and ests are sorted by onset, and only pairs within the
// tolerance may match. An optimal matching never crosses (swapping two crossing pairs keeps both
// feasible and does not increase the total distance), so a DP over prefixes finds the maximum
// count, then the least total distance.
void match_cluster(const std::vector<BassNoteLabel> &references,
                   const std::vector<BassNoteLabel> &estimates, const std::vector<int> &refs,
                   const std::vector<int> &ests, double tolerance_s,
                   std::vector<NoteMatch> &matches) {
    enum class Step : unsigned char { skip_ref, skip_est, pair };
    struct Cell {
        int count = 0;
        double cost = 0.0;
        Step step = Step::skip_ref;
        bool better_than(const Cell &other) const {
            return count != other.count ? count > other.count : cost < other.cost;
        }
    };
    const size_t n = refs.size();
    const size_t m = ests.size();
    std::vector<Cell> dp((n + 1) * (m + 1));
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    for (size_t j = 1; j <= m; ++j) {
        dp[at(0, j)].step = Step::skip_est;
    }
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            Cell best = dp[at(i - 1, j)];
            best.step = Step::skip_ref;
            if (dp[at(i, j - 1)].better_than(best)) {
                best = dp[at(i, j - 1)];
                best.step = Step::skip_est;
            }
            const double r = references[refs[i - 1]].onset_s;
            const double e = estimates[ests[j - 1]].onset_s;
            if (rounded_distance_s(r, e) <= tolerance_s) {
                const Cell paired{dp[at(i - 1, j - 1)].count + 1,
                                  dp[at(i - 1, j - 1)].cost + std::abs(r - e), Step::pair};
                if (paired.better_than(best)) {
                    best = paired;
                }
            }
            dp[at(i, j)] = best;
        }
    }
    std::vector<NoteMatch> cluster_matches;
    for (size_t i = n, j = m; i > 0 && j > 0;) {
        switch (dp[at(i, j)].step) {
        case Step::pair:
            cluster_matches.push_back({refs[i - 1], ests[j - 1]});
            --i;
            --j;
            break;
        case Step::skip_ref:
            --i;
            break;
        case Step::skip_est:
            --j;
            break;
        }
    }
    matches.insert(matches.end(), cluster_matches.rbegin(), cluster_matches.rend());
}

struct Selection {
    std::vector<BassNoteLabel> notes;
    std::vector<bool> inside; // Onset inside a region, not only within the margin.
};

Selection select_notes(const std::vector<BassNoteLabel> &notes,
                       const std::vector<LabelRegion> &regions, double margin_s) {
    Selection selection;
    for (const BassNoteLabel &note : notes) {
        bool in_margin = false;
        bool inside = false;
        for (const LabelRegion &r : regions) {
            inside = inside || (note.onset_s >= r.start_s && note.onset_s < r.end_s);
            in_margin = in_margin ||
                        (note.onset_s >= r.start_s - margin_s && note.onset_s < r.end_s + margin_s);
        }
        if (in_margin) {
            selection.notes.push_back(note);
            selection.inside.push_back(inside);
        }
    }
    return selection;
}

// Offset tolerance of a reference note, as mir_eval's.
double offset_tolerance_s(const BassNoteLabel &ref, const ScoringParams &params) {
    return std::max(params.offset_ratio * (ref.offset_s - ref.onset_s),
                    params.offset_min_tolerance_s);
}

// Fills every statistic from a matching. The counts are the recall and precision denominators.
NoteScore score_matches(const std::vector<BassNoteLabel> &references,
                        const std::vector<BassNoteLabel> &estimates,
                        const std::vector<NoteMatch> &matches, int reference_count,
                        int estimate_count, double slot_len_s, const ScoringParams &params) {
    NoteScore score;
    score.reference_count = reference_count;
    score.estimate_count = estimate_count;
    score.matched_count = static_cast<int>(matches.size());
    if (score.reference_count > 0 && score.estimate_count > 0) {
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
        if (rounded_distance_s(est.offset_s, ref.offset_s) <= offset_tolerance_s(ref, params)) {
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

// The matching inside the regions. It sees every reference and estimate within one tolerance of a
// region, so detections at an edge pair with the right note: an estimate matched to a reference
// outside the regions is neither a hit nor a false positive here. Indices are into refs and ests.
struct RegionMatching {
    Selection refs;
    Selection ests;
    std::vector<NoteMatch> matches;   // Pairs whose reference is inside.
    std::vector<int> missed;          // Inside references with no match.
    std::vector<int> false_positives; // Inside estimates with no match.
};

RegionMatching match_in_regions(const std::vector<BassNoteLabel> &reference_notes,
                                const std::vector<BassNoteLabel> &estimates,
                                const std::vector<LabelRegion> &regions,
                                const ScoringParams &params) {
    const double margin_s = params.onset_tolerance_s;
    RegionMatching m;
    m.refs = select_notes(reference_notes, regions, margin_s);
    m.ests = select_notes(estimates, regions, margin_s);
    std::vector<bool> ref_matched(m.refs.notes.size(), false);
    std::vector<bool> est_matched(m.ests.notes.size(), false);
    for (const NoteMatch &match :
         match_note_onsets(m.refs.notes, m.ests.notes, params.onset_tolerance_s)) {
        ref_matched[match.reference_index] = true;
        est_matched[match.estimate_index] = true;
        if (m.refs.inside[match.reference_index]) {
            m.matches.push_back(match);
        }
    }
    for (size_t i = 0; i < m.refs.notes.size(); ++i) {
        if (!ref_matched[i] && m.refs.inside[i]) {
            m.missed.push_back(static_cast<int>(i));
        }
    }
    for (size_t i = 0; i < m.ests.notes.size(); ++i) {
        if (!est_matched[i] && m.ests.inside[i]) {
            m.false_positives.push_back(static_cast<int>(i));
        }
    }
    return m;
}

NoteScore score_in_regions(const std::vector<BassNoteLabel> &reference_notes,
                           const std::vector<BassNoteLabel> &estimates,
                           const std::vector<LabelRegion> &regions, double slot_len_s,
                           const ScoringParams &params) {
    const RegionMatching m = match_in_regions(reference_notes, estimates, regions, params);
    const auto matched = static_cast<int>(m.matches.size());
    return score_matches(m.refs.notes, m.ests.notes, m.matches,
                         matched + static_cast<int>(m.missed.size()),
                         matched + static_cast<int>(m.false_positives.size()), slot_len_s, params);
}

} // namespace

std::vector<NoteMatch> match_note_onsets(const std::vector<BassNoteLabel> &references,
                                         const std::vector<BassNoteLabel> &estimates,
                                         double onset_tolerance_s) {
    // Group notes into clusters connected by feasible pairs (union-find), then match each cluster
    // optimally. Clusters are tiny on real data: a note's window rarely reaches past its
    // neighbours.
    const std::vector<int> refs = indices_sorted_by_onset(references);
    const std::vector<int> ests = indices_sorted_by_onset(estimates);
    const size_t ref_count = refs.size();
    std::vector<size_t> parent(ref_count + ests.size()); // Refs first, then ests.
    std::iota(parent.begin(), parent.end(), size_t{0});
    const auto find = [&parent](size_t x) {
        while (parent[x] != x) {
            x = parent[x] = parent[parent[x]];
        }
        return x;
    };
    size_t first_candidate = 0; // Earliest estimate that a later reference could still reach.
    for (size_t r = 0; r < ref_count; ++r) {
        const double ref_onset_s = references[refs[r]].onset_s;
        while (first_candidate < ests.size() &&
               estimates[ests[first_candidate]].onset_s < ref_onset_s &&
               rounded_distance_s(estimates[ests[first_candidate]].onset_s, ref_onset_s) >
                   onset_tolerance_s) {
            ++first_candidate;
        }
        for (size_t e = first_candidate; e < ests.size(); ++e) {
            if (rounded_distance_s(estimates[ests[e]].onset_s, ref_onset_s) > onset_tolerance_s) {
                if (estimates[ests[e]].onset_s > ref_onset_s) {
                    break;
                }
                continue;
            }
            parent[find(r)] = find(ref_count + e);
        }
    }
    std::vector<std::vector<int>> cluster_refs(parent.size());
    std::vector<std::vector<int>> cluster_ests(parent.size());
    for (size_t r = 0; r < ref_count; ++r) {
        cluster_refs[find(r)].push_back(refs[r]);
    }
    for (size_t e = 0; e < ests.size(); ++e) {
        cluster_ests[find(ref_count + e)].push_back(ests[e]);
    }
    std::vector<NoteMatch> matches;
    for (size_t root = 0; root < parent.size(); ++root) {
        if (!cluster_refs[root].empty() && !cluster_ests[root].empty()) {
            match_cluster(references, estimates, cluster_refs[root], cluster_ests[root],
                          onset_tolerance_s, matches);
        }
    }
    std::sort(matches.begin(), matches.end(), [&](const NoteMatch &a, const NoteMatch &b) {
        return references[a.reference_index].onset_s < references[b.reference_index].onset_s;
    });
    return matches;
}

NoteScore score_notes(const std::vector<BassNoteLabel> &references,
                      const std::vector<BassNoteLabel> &estimates, double slot_len_s,
                      const ScoringParams &params) {
    return score_matches(references, estimates,
                         match_note_onsets(references, estimates, params.onset_tolerance_s),
                         static_cast<int>(references.size()), static_cast<int>(estimates.size()),
                         slot_len_s, params);
}

TrackScore score_track(const Labels &reference, const std::vector<BassNoteLabel> &estimates,
                       const ScoringParams &params) {
    const double slot_len_s = 60.0 / reference.grid.bpm / 4.0;
    TrackScore track;
    std::vector<LabelRegion> complete_regions;
    for (const LabelRegion &region : reference.regions) {
        if (region.complete) {
            complete_regions.push_back(region);
            track.regions.push_back({region, score_in_regions(reference.bass_notes, estimates,
                                                              {region}, slot_len_s, params)});
        }
    }
    track.total =
        score_in_regions(reference.bass_notes, estimates, complete_regions, slot_len_s, params);
    return track;
}

std::vector<NoteError> list_note_errors(const Labels &reference,
                                        const std::vector<BassNoteLabel> &estimates,
                                        const ScoringParams &params) {
    std::vector<LabelRegion> complete_regions;
    for (const LabelRegion &region : reference.regions) {
        if (region.complete) {
            complete_regions.push_back(region);
        }
    }
    const RegionMatching m =
        match_in_regions(reference.bass_notes, estimates, complete_regions, params);
    std::vector<NoteError> errors;
    for (const int i : m.missed) {
        errors.push_back({NoteErrorKind::missed, m.refs.notes[i].onset_s, m.refs.notes[i], {}});
    }
    for (const int i : m.false_positives) {
        errors.push_back(
            {NoteErrorKind::false_positive, m.ests.notes[i].onset_s, {}, m.ests.notes[i]});
    }
    for (const NoteMatch &match : m.matches) {
        const BassNoteLabel &ref = m.refs.notes[match.reference_index];
        const BassNoteLabel &est = m.ests.notes[match.estimate_index];
        const double cents = pitch_cents(est) - pitch_cents(ref);
        if (std::abs(cents) > params.pitch_tolerance_cents) {
            const double octaves = std::round(cents / kCentsPerOctave);
            const bool octave = octaves != 0.0 && std::abs(cents - octaves * kCentsPerOctave) <=
                                                      params.pitch_tolerance_cents;
            errors.push_back({octave ? NoteErrorKind::octave : NoteErrorKind::pitch, ref.onset_s,
                              ref, est, cents});
        }
        if (rounded_distance_s(est.offset_s, ref.offset_s) > offset_tolerance_s(ref, params)) {
            errors.push_back(
                {NoteErrorKind::offset, ref.onset_s, ref, est, est.offset_s - ref.offset_s});
        }
    }
    std::stable_sort(errors.begin(), errors.end(), [](const NoteError &a, const NoteError &b) {
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        if (std::abs(a.error) != std::abs(b.error)) {
            return std::abs(a.error) > std::abs(b.error);
        }
        return a.time_s < b.time_s;
    });
    return errors;
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
