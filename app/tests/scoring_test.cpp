#include "analysis/scoring.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace psyight;

namespace {

constexpr double kSlotLenS = 0.09375; // One 16th at 160 BPM.

BassNoteLabel note(double onset_s, double offset_s, int midi = 29, double cents = 0.0) {
    return {onset_s, offset_s, midi, cents, false, false, 1.0};
}

std::vector<BassNoteLabel> rolling_reference() {
    std::vector<BassNoteLabel> notes;
    for (int i = 0; i < 12; ++i) {
        const double onset_s = 1.0 + (i + i / 3 + 1) * kSlotLenS; // K B B B
        notes.push_back(note(onset_s, onset_s + 0.6 * kSlotLenS));
    }
    return notes;
}

} // namespace

TEST_CASE("scoring: identical estimates score perfectly") {
    const std::vector<BassNoteLabel> reference = rolling_reference();
    const NoteScore score = score_notes(reference, reference, kSlotLenS, {});
    CHECK(score.matched_count == 12);
    CHECK(score.onset_precision == 1.0);
    CHECK(score.onset_recall == 1.0);
    CHECK(score.onset_f == 1.0);
    CHECK(score.offset_error_abs_p90_s == 0.0);
    CHECK(score.offset_hit_rate == 1.0);
    CHECK(score.pitch_accuracy == 1.0);
    CHECK(score.octave_error_rate == 0.0);
    CHECK(score.gate_ratio_mae == 0.0);
}

TEST_CASE("scoring: the onset tolerance is inclusive after rounding to 0.1 ms") {
    const std::vector<BassNoteLabel> reference{note(60.105, 60.171)};
    const ScoringParams params; // 20 ms.
    CHECK(score_notes(reference, {note(60.125, 60.171)}, kSlotLenS, params).matched_count == 1);
    CHECK(score_notes(reference, {note(60.085, 60.171)}, kSlotLenS, params).matched_count == 1);
    CHECK(score_notes(reference, {note(60.1251, 60.171)}, kSlotLenS, params).matched_count == 0);
    CHECK(score_notes(reference, {note(60.0849, 60.171)}, kSlotLenS, params).matched_count == 0);
}

TEST_CASE("scoring: matching has maximum cardinality where closest-pair-first does not") {
    // Closest pair first takes (ref 0.000, est 0.017) and strands both others. The maximum
    // matching pairs ref 0.000 with est -0.019 and ref 0.036 with est 0.017.
    const std::vector<BassNoteLabel> reference{note(10.000, 10.05), note(10.036, 10.08)};
    const std::vector<BassNoteLabel> estimates{note(10.017, 10.05), note(9.981, 10.05)};
    const std::vector<NoteMatch> matches = match_note_onsets(reference, estimates, 0.020);
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].reference_index == 0);
    CHECK(matches[0].estimate_index == 1);
    CHECK(matches[1].reference_index == 1);
    CHECK(matches[1].estimate_index == 0);
}

TEST_CASE("scoring: precision, recall and F count false positives and misses") {
    std::vector<BassNoteLabel> estimates = rolling_reference();
    estimates.erase(estimates.begin() + 3, estimates.begin() + 6); // 3 misses
    estimates.push_back(note(1.0, 1.05));                          // 1 false positive (kick)
    const NoteScore score = score_notes(rolling_reference(), estimates, kSlotLenS, {});
    CHECK(score.matched_count == 9);
    CHECK(score.onset_precision == doctest::Approx(9.0 / 10.0));
    CHECK(score.onset_recall == doctest::Approx(9.0 / 12.0));
    CHECK(score.onset_f == doctest::Approx(2.0 * 0.9 * 0.75 / (0.9 + 0.75)));
}

TEST_CASE("scoring: empty inputs follow mir_eval (zeros) and statistics become NaN") {
    const NoteScore no_estimates = score_notes(rolling_reference(), {}, kSlotLenS, {});
    CHECK(no_estimates.onset_precision == 0.0);
    CHECK(no_estimates.onset_recall == 0.0);
    CHECK(no_estimates.onset_f == 0.0);
    CHECK(std::isnan(no_estimates.offset_error_abs_median_s));
    CHECK(std::isnan(no_estimates.pitch_accuracy));
    CHECK(std::isnan(no_estimates.gate_ratio_mae));
    const NoteScore nothing = score_notes({}, {}, kSlotLenS, {});
    CHECK(nothing.reference_count == 0);
    CHECK(nothing.onset_f == 0.0);
}

TEST_CASE("scoring: offset errors, their distribution and the mir_eval offset hit rule") {
    // Durations: 1 s (tolerance 0.2 s) and 0.06 s (tolerance clamps to the 0.05 s minimum).
    const std::vector<BassNoteLabel> reference{note(1.0, 2.0), note(3.0, 3.06), note(4.0, 5.0),
                                               note(6.0, 6.06)};
    const std::vector<BassNoteLabel> estimates{note(1.0, 2.15), note(3.0, 3.11), note(4.0, 4.75),
                                               note(6.0, 6.0)};
    const NoteScore score = score_notes(reference, estimates, kSlotLenS, {});
    REQUIRE(score.offset_errors_s.size() == 4);
    CHECK(score.offset_errors_s[0] == doctest::Approx(0.15));
    CHECK(score.offset_errors_s[1] == doctest::Approx(0.05));
    CHECK(score.offset_errors_s[2] == doctest::Approx(-0.25));
    CHECK(score.offset_errors_s[3] == doctest::Approx(-0.06));
    // Hits: 0.15 <= 0.2 yes, 0.05 <= 0.05 yes (inclusive), 0.25 <= 0.2 no, 0.06 <= 0.05 no.
    CHECK(score.offset_hit_rate == doctest::Approx(0.5));
    // Signed sorted: -0.25 -0.06 0.05 0.15 -> median -0.005. Abs sorted: 0.05 0.06 0.15 0.25.
    CHECK(score.offset_error_median_s == doctest::Approx(-0.005));
    CHECK(score.offset_error_abs_median_s == doctest::Approx(0.105));
    CHECK(score.offset_error_abs_p90_s == doctest::Approx(0.22)); // 0.15 + 0.7 * 0.10
}

TEST_CASE("scoring: pitch within 50 cents is correct and whole-octave errors count separately") {
    std::vector<BassNoteLabel> reference;
    std::vector<BassNoteLabel> estimates;
    const struct {
        int midi;
        double cents;
    } kCases[] = {{29, 30.0}, {29, -50.0}, {30, -40.0},  {29, 60.0},
                  {41, 0.0},  {17, 10.0},  {29, 1190.0}, {36, 0.0}};
    // Against reference 29 + 0: correct (+30), correct (-50, inclusive), wrong (+60), wrong (+60),
    // octave (+1200), octave (-1190), octave (+1190), wrong (+700, a fifth).
    for (size_t i = 0; i < std::size(kCases); ++i) {
        const double onset_s = 1.0 + static_cast<double>(i);
        reference.push_back(note(onset_s, onset_s + 0.05, 29, 0.0));
        estimates.push_back(note(onset_s, onset_s + 0.05, kCases[i].midi, kCases[i].cents));
    }
    const NoteScore score = score_notes(reference, estimates, kSlotLenS, {});
    CHECK(score.pitch_correct_count == 2);
    CHECK(score.octave_error_count == 3);
    CHECK(score.pitch_accuracy == doctest::Approx(2.0 / 8.0));
    CHECK(score.octave_error_rate == doctest::Approx(3.0 / 8.0));
}

TEST_CASE("scoring: gate-ratio MAE is measured in 16th-note slots") {
    const std::vector<BassNoteLabel> reference{note(1.0, 1.0 + 0.5 * kSlotLenS),
                                               note(2.0, 2.0 + 1.0 * kSlotLenS)};
    const std::vector<BassNoteLabel> estimates{note(1.0, 1.0 + 0.6 * kSlotLenS),
                                               note(2.0, 2.0 + 0.7 * kSlotLenS)};
    const NoteScore score = score_notes(reference, estimates, kSlotLenS, {});
    CHECK(score.gate_ratio_mae == doctest::Approx((0.1 + 0.3) / 2.0));
}

TEST_CASE("scoring: tracks are scored per complete region and in total") {
    Labels reference;
    reference.grid.bpm = 160.0;
    reference.regions = {{0.0, 10.0, true}, {10.0, 20.0, false}, {20.0, 30.0, true}};
    reference.bass_notes = {note(1.0, 1.05), note(2.0, 2.05), note(11.0, 11.05), note(21.0, 21.05)};
    // Region 1: one hit, one miss. Region 2 (incomplete): a false positive that must not count.
    // Region 3: one hit and one false positive. Outside every region: ignored.
    const std::vector<BassNoteLabel> estimates{note(1.005, 1.05), note(15.0, 15.05),
                                               note(21.0, 21.05), note(25.0, 25.05),
                                               note(40.0, 40.05)};
    const TrackScore track = score_track(reference, estimates, {});
    REQUIRE(track.regions.size() == 2);
    CHECK(track.regions[0].region.start_s == 0.0);
    CHECK(track.regions[0].score.onset_recall == doctest::Approx(0.5));
    CHECK(track.regions[0].score.onset_precision == doctest::Approx(1.0));
    CHECK(track.regions[1].region.start_s == 20.0);
    CHECK(track.regions[1].score.onset_precision == doctest::Approx(0.5));
    CHECK(track.total.reference_count == 3);
    CHECK(track.total.estimate_count == 3);
    CHECK(track.total.matched_count == 2);
}

TEST_CASE("scoring: percentile matches numpy's linear interpolation") {
    CHECK(percentile({4.0, 1.0, 3.0, 2.0}, 0.5) == doctest::Approx(2.5));
    CHECK(percentile({4.0, 1.0, 3.0, 2.0}, 0.9) == doctest::Approx(3.7));
    CHECK(percentile({7.0}, 0.9) == 7.0);
    CHECK(std::isnan(percentile({}, 0.5)));
}
