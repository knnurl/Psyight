// Golden-metric regression test (CLAUDE.md "Testing"): the full file-mode analysis
// (analysis/track_analysis.h) on every synthetic case, scored against the generator's exact
// labels and compared with tests/golden/synthetic_metrics.json. A metric that gets worse fails;
// one that improves passes with a message. After an intended change, rewrite the file with
//   set PSYIGHT_UPDATE_GOLDEN=1 && psyight_tests -tc="golden*"
// and commit it with the change, so the diff shows what moved.

#include "analysis/scoring.h"
#include "analysis/track_analysis.h"
#include "synthetic_set.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;
constexpr double kSlack = 1e-9; // Runs are bit-identical; this only absorbs JSON round trips.

struct Metric {
    const char *name;
    bool higher_is_better;
};

constexpr Metric kMetrics[] = {
    {"note_onset_f", true},
    {"note_pitch_accuracy", true},
    {"note_octave_errors", false},
    {"note_gate_ratio_mae", false},
    {"note_offset_abs_median_ms", false},
    {"note_offset_hit_rate", true},
    {"kick_f", true},
    {"tempo_error_bpm", false},
    {"beat_phase_error_ms", false},
};

std::filesystem::path golden_path() {
    return std::filesystem::path(PSYIGHT_TESTS_DIR) / "golden" / "synthetic_metrics.json";
}

std::vector<BassNoteLabel> as_notes(const std::vector<double> &times_s) {
    std::vector<BassNoteLabel> notes;
    for (const double t : times_s) {
        notes.push_back({t, t + 0.01, 0, 0.0, false, false, 1.0});
    }
    return notes;
}

nlohmann::json measure(const synth::SyntheticCase &c) {
    const synth::SynthOutput audio = synth::render(c.params, c.events);
    const Labels &truth = audio.labels;
    TrackAnalysisOptions options;
    options.grid =
        Grid{truth.grid.bpm, seconds_to_samples(truth.grid.first_downbeat_s, kSampleRateHz),
             kSampleRateHz};
    const Result<TrackAnalysis> analysed = analyse_track(audio.samples, kSampleRateHz, {}, options);
    REQUIRE(analysed.ok());
    const TrackAnalysis &a = analysed.value();
    const double slot_s = 60.0 / truth.grid.bpm / 4.0;
    const NoteScore notes = score_notes(truth.bass_notes, to_labels(a.bass.notes), slot_s, {});
    std::vector<double> truth_kicks_s;
    for (const LabelEvent &k : truth.kicks) {
        truth_kicks_s.push_back(k.t_s);
    }
    const NoteScore kicks =
        score_notes(as_notes(truth_kicks_s), as_notes(a.bass.kick_times_s), slot_s, {});

    // The beat tracker's own grid (the bass used the labelled one): tempo, and beat phase wrapped
    // to the nearest beat. Not the downbeat: these patterns repeat every beat or bar with no
    // bar-level cue, so the bar phase is undecidable (beat_tracker_test.cpp covers it with a bar
    // progression and kick breaks).
    const double beat_s = 60.0 / truth.grid.bpm;
    const double detected_downbeat_s =
        samples_to_seconds(a.beats.grid.first_downbeat, kSampleRateHz);
    const double phase_s =
        std::remainder(detected_downbeat_s - truth.grid.first_downbeat_s, beat_s);
    return {
        {"note_onset_f", notes.onset_f},
        {"note_pitch_accuracy", notes.pitch_accuracy},
        {"note_octave_errors", notes.octave_error_count},
        {"note_gate_ratio_mae", notes.gate_ratio_mae},
        {"note_offset_abs_median_ms", 1000.0 * notes.offset_error_abs_median_s},
        {"note_offset_hit_rate", notes.offset_hit_rate},
        {"kick_f", kicks.onset_f},
        {"tempo_error_bpm", std::abs(a.beats.bpm - truth.grid.bpm)},
        {"beat_phase_error_ms", 1000.0 * std::abs(phase_s)},
    };
}

bool update_requested() {
#ifdef _MSC_VER
    char *value = nullptr;
    size_t length = 0;
    const bool set = _dupenv_s(&value, &length, "PSYIGHT_UPDATE_GOLDEN") == 0 && value != nullptr &&
                     std::string(value) == "1";
    std::free(value);
    return set;
#else
    const char *value = std::getenv("PSYIGHT_UPDATE_GOLDEN");
    return value != nullptr && std::string(value) == "1";
#endif
}

// NaN is stored as null (JSON has no NaN).
double value_of(const nlohmann::json &j) {
    return j.is_number() ? j.get<double>() : std::numeric_limits<double>::quiet_NaN();
}

} // namespace

TEST_CASE("golden metrics: the full analysis on the synthetic set is no worse than stored") {
    nlohmann::json current;
    for (const synth::SyntheticCase &c : synth::synthetic_set()) {
        current[c.name] = measure(c);
    }
    if (update_requested()) {
        std::filesystem::create_directories(golden_path().parent_path());
        std::ofstream(golden_path()) << current.dump(2) << "\n";
        MESSAGE("wrote " << golden_path().string());
        return;
    }
    std::ifstream file(golden_path());
    REQUIRE_MESSAGE(file.good(), "missing " << golden_path().string()
                                            << "; set PSYIGHT_UPDATE_GOLDEN=1 to create it");
    const nlohmann::json golden = nlohmann::json::parse(file);
    CHECK(golden.size() == current.size());
    for (const auto &[name, metrics] : current.items()) {
        CAPTURE(name);
        REQUIRE_MESSAGE(golden.contains(name), "case not in the golden file");
        for (const Metric &m : kMetrics) {
            CAPTURE(m.name);
            const double now = value_of(metrics.at(m.name));
            const double stored = value_of(golden.at(name).at(m.name));
            if (std::isnan(stored)) {
                continue; // Nothing measured before: anything is no worse.
            }
            REQUIRE_FALSE(std::isnan(now));
            const bool worse = m.higher_is_better ? now < stored - kSlack : now > stored + kSlack;
            CHECK_MESSAGE(!worse, "got worse: " << stored << " -> " << now);
            if (!worse && std::abs(now - stored) > kSlack) {
                MESSAGE("improved: " << stored << " -> " << now
                                     << " (update the golden file to keep it)");
            }
        }
    }
}
