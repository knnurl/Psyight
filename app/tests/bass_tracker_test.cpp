// Bass tracker v0 against the synthetic set's exact ground truth. The cases tagged skip are the
// tuning tools: run them with --no-skip, for example
//   psyight_tests -tc="bass tracker: table" --no-skip

#include "analysis/bass_tracker.h"
#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "analysis/scoring.h"
#include "synthetic_set.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;
constexpr double kOnsetToleranceS = 0.020;

struct CaseRun {
    std::string name;
    synth::SynthOutput audio;
    FrontEndResult features;
    Grid grid;
    BassTrackResult tracked;
    std::vector<BassNoteLabel> estimates;
    std::vector<NoteMatch> matches;
    NoteScore score;
};

Grid grid_of(const Labels &labels) {
    return {labels.grid.bpm, seconds_to_samples(labels.grid.first_downbeat_s, kSampleRateHz),
            kSampleRateHz};
}

void track(CaseRun &run, const BassTrackerParams &params) {
    Result<BassTrackResult> tracked = track_bass(run.audio.samples, run.features, run.grid, params);
    REQUIRE(tracked.ok());
    run.tracked = std::move(tracked.value());
    run.estimates = to_labels(run.tracked.notes);
    run.matches = match_note_onsets(run.audio.labels.bass_notes, run.estimates, kOnsetToleranceS);
    run.score = score_notes(run.audio.labels.bass_notes, run.estimates,
                            60.0 / run.audio.labels.grid.bpm / 4.0, {});
}

CaseRun run_case(const synth::SyntheticCase &c, const BassTrackerParams &params = {}) {
    CaseRun run;
    run.name = c.name;
    run.audio = synth::render(c.params, c.events);
    Result<FrontEndResult> features = analyse_front_end(run.audio.samples, kSampleRateHz, {});
    REQUIRE(features.ok());
    run.features = std::move(features.value());
    run.grid = grid_of(run.audio.labels);
    track(run, params);
    return run;
}

// The whole set, analysed once and shared by the tests below.
const std::vector<CaseRun> &synthetic_runs() {
    static const std::vector<CaseRun> runs = [] {
        std::vector<CaseRun> result;
        for (const synth::SyntheticCase &c : synth::synthetic_set()) {
            result.push_back(run_case(c));
        }
        return result;
    }();
    return runs;
}

const CaseRun &find_run(const std::string &name) {
    for (const CaseRun &run : synthetic_runs()) {
        if (run.name == name) {
            return run;
        }
    }
    FAIL("no synthetic case named " << name);
    return synthetic_runs().front();
}

double slot_s_of(const CaseRun &run) { return 60.0 / run.audio.labels.grid.bpm / 4.0; }

} // namespace

TEST_CASE("bass tracker: every synthetic kick is found once, within 5 ms") {
    for (const CaseRun &run : synthetic_runs()) {
        CAPTURE(run.name);
        const std::vector<LabelEvent> &kicks = run.audio.labels.kicks;
        REQUIRE(run.tracked.kick_times_s.size() == kicks.size());
        for (size_t i = 0; i < kicks.size(); ++i) {
            CHECK(std::abs(run.tracked.kick_times_s[i] - kicks[i].t_s) <= 0.005);
        }
    }
}

TEST_CASE("bass tracker: onsets and offsets on the synthetic set") {
    int references = 0;
    int estimates = 0;
    int matched = 0;
    double gate_mae_sum = 0.0;
    for (const CaseRun &run : synthetic_runs()) {
        CAPTURE(run.name);
        const NoteScore &s = run.score;
        CHECK(s.onset_f >= 0.99);
        CHECK(s.offset_error_abs_median_s <= 0.020);
        CHECK(s.gate_ratio_mae <= 0.30);
        references += s.reference_count;
        estimates += s.estimate_count;
        matched += s.matched_count;
        gate_mae_sum += s.gate_ratio_mae;
        std::vector<double> errors_s;
        for (const NoteMatch &m : run.matches) {
            errors_s.push_back(std::abs(run.estimates[m.estimate_index].onset_s -
                                        run.audio.labels.bass_notes[m.reference_index].onset_s));
        }
        // Slide onsets come from the salience crossover, a coarser clock than the onset function.
        CHECK(percentile(errors_s, 0.95) <= (run.name == "slides and long notes" ? 0.020 : 0.015));
    }
    const double precision = static_cast<double>(matched) / estimates;
    const double recall = static_cast<double>(matched) / references;
    CHECK(2.0 * precision * recall / (precision + recall) >= 0.99);
    // SPEC 15 placeholder target is 0.10; v0 sits on it (0.100 when written). Gaps shorter than
    // the envelope window run to the next onset, and long notes decay below k x peak before
    // their gate closes. This bound guards against regression.
    CHECK(gate_mae_sum / static_cast<double>(synthetic_runs().size()) <= 0.105);
}

TEST_CASE("bass tracker: gate ratio of a clean gated line is within 0.1 slot") {
    for (const char *name : {"rolling, gate 0.6", "rolling, gate 1.0", "octave jump, gate 0.6",
                             "offbeat double, gate 0.5", "triplet gallop, gate 0.7"}) {
        CAPTURE(name);
        CHECK(find_run(name).score.gate_ratio_mae <= 0.10);
    }
}

TEST_CASE("bass tracker: pitch is exact on notes of two periods or more") {
    for (const CaseRun &run : synthetic_runs()) {
        if (run.name == "offbeat, gate 0.3") {
            continue; // 28 ms notes: about one period of F1, below the two the SPEC needs.
        }
        CAPTURE(run.name);
        CHECK(run.score.pitch_accuracy >= 0.90);
    }
    CHECK(find_run("melodic, gate 0.7").score.pitch_accuracy >= 0.95);
    CHECK(find_run("octave jump, gate 0.6").score.octave_error_count == 0);
}

TEST_CASE("bass tracker: wrong pitches carry low confidence") {
    double right_sum = 0.0;
    double wrong_sum = 0.0;
    int right = 0;
    int wrong = 0;
    for (const CaseRun &run : synthetic_runs()) {
        for (const NoteMatch &m : run.matches) {
            const bool correct = run.tracked.notes[m.estimate_index].pitch_midi ==
                                 run.audio.labels.bass_notes[m.reference_index].midi;
            (correct ? right_sum : wrong_sum) += run.tracked.notes[m.estimate_index].confidence;
            ++(correct ? right : wrong);
        }
    }
    REQUIRE(right > 0);
    REQUIRE(wrong > 0);
    CHECK(wrong_sum / wrong + 0.25 <= right_sum / right);
}

TEST_CASE("bass tracker: slides are split and flagged; plain lines have none") {
    const CaseRun &slides = find_run("slides and long notes");
    int found = 0;
    int labelled = 0;
    for (const NoteMatch &m : slides.matches) {
        found += slides.audio.labels.bass_notes[m.reference_index].slide_in &&
                 slides.estimates[m.estimate_index].slide_in;
    }
    for (const BassNoteLabel &n : slides.audio.labels.bass_notes) {
        labelled += n.slide_in;
    }
    CHECK(labelled == 16);
    CHECK(found >= 14);
    for (const CaseRun &run : synthetic_runs()) {
        if (&run == &slides) {
            continue;
        }
        CAPTURE(run.name);
        for (const BassNote &note : run.tracked.notes) {
            CHECK_FALSE(note.slide_in);
        }
    }
}

TEST_CASE("bass tracker: long notes, slot index and velocity") {
    const CaseRun &slides = find_run("slides and long notes");
    int long_labelled = 0;
    int long_found = 0;
    for (const NoteMatch &m : slides.matches) {
        const BassNoteLabel &r = slides.audio.labels.bass_notes[m.reference_index];
        if (!r.slide_in && !r.slide_out && r.offset_s - r.onset_s > 1.5 * slot_s_of(slides)) {
            ++long_labelled;
            long_found += slides.tracked.notes[m.estimate_index].is_long;
        }
    }
    CHECK(long_labelled == 8); // The 2.7-slot note of every bar.
    CHECK(long_found == long_labelled);
    for (const BassNote &note : find_run("rolling, gate 0.6").tracked.notes) {
        CHECK_FALSE(note.is_long);
    }

    // K B B B: bass on slots 1, 2 and 3 of every beat.
    for (const BassNote &note : find_run("rolling, gate 0.6").tracked.notes) {
        CHECK(note.slot_index % 4 != 0);
        CHECK(note.slot_index >= 0);
        CHECK(note.slot_index < 16);
    }

    // Ghost notes are rendered at 0.4 of the others' amplitude.
    const CaseRun &ghost = find_run("ghost note, gate 0.8");
    for (const NoteMatch &m : ghost.matches) {
        const double expected = ghost.audio.labels.bass_notes[m.reference_index].velocity;
        CHECK(ghost.tracked.notes[m.estimate_index].velocity ==
              doctest::Approx(expected).epsilon(0.25));
    }
}

TEST_CASE("bass tracker: decay time constant follows the synth's decay") {
    // A long note's amplitude decays as exp(-t / tau); the filter closing adds to the measured
    // decay of the bass band, so the fit reads shorter than tau.
    for (const double tau_ms : {150.0, 400.0}) {
        CAPTURE(tau_ms);
        synth::SynthParams params;
        params.kick = false;
        params.bass_decay_tau_ms = tau_ms;
        params.filter_tau_ms = 5.0; // Filter settled before the fit's range.
        std::vector<synth::BassEvent> events;
        for (int note = 0; note < 8; ++note) {
            events.push_back({note * 8.0, 6.0, 29, 1.0, false});
        }
        const CaseRun run = run_case({"decay", params, events});
        CHECK(run.tracked.kick_times_s.empty()); // Bass attacks on the beat are not kicks.
        REQUIRE(run.tracked.notes.size() == 8);
        CHECK(run.score.onset_f == 1.0);
        for (const BassNote &note : run.tracked.notes) {
            CHECK(note.decay_tau_ms == doctest::Approx(tau_ms).epsilon(0.3));
        }
    }
}

TEST_CASE("bass tracker: identical input and parameters give bit-identical notes") {
    const synth::SyntheticCase c = synth::synthetic_set()[0];
    const CaseRun a = run_case(c);
    const CaseRun b = run_case(c);
    REQUIRE(a.tracked.notes.size() == b.tracked.notes.size());
    for (size_t i = 0; i < a.tracked.notes.size(); ++i) {
        const BassNote &x = a.tracked.notes[i];
        const BassNote &y = b.tracked.notes[i];
        CHECK(std::memcmp(&x.onset_s, &y.onset_s, sizeof(double)) == 0);
        CHECK(std::memcmp(&x.offset_s, &y.offset_s, sizeof(double)) == 0);
        CHECK(std::memcmp(&x.cents, &y.cents, sizeof(double)) == 0);
        CHECK(std::memcmp(&x.confidence, &y.confidence, sizeof(double)) == 0);
        CHECK(x.pitch_midi == y.pitch_midi);
    }
    CHECK(a.tracked.onset_function == b.tracked.onset_function);
    CHECK(a.tracked.kick_times_s == b.tracked.kick_times_s);
}

TEST_CASE("bass tracker: parameters round trip through the registry") {
    ParamRegistry registry;
    REQUIRE(declare_bass_tracker_params(registry).ok());
    CHECK(registry.specs().size() >= 20);
    for (const ParamSpec &spec : registry.specs()) {
        CHECK(spec.id.rfind("bass.", 0) == 0);
    }
    const BassTrackerParams defaults;
    const BassTrackerParams from_registry = bass_tracker_params_from(registry.values());
    CHECK(std::memcmp(&defaults, &from_registry, sizeof(BassTrackerParams)) == 0);
    REQUIRE(registry.set("bass.offset_ratio", 0.4).ok());
    CHECK(bass_tracker_params_from(registry.values()).offset_ratio == 0.4);
    CHECK_FALSE(declare_bass_tracker_params(registry).ok()); // Ids are declared once.
}

TEST_CASE("bass tracker: missing front-end bands are an error, not a crash") {
    const std::vector<float> silence(48000, 0.0f);
    FrontEndParams params;
    params.bands = {{"sub", 30.0, 80.0}};
    const Result<FrontEndResult> features = analyse_front_end(silence, kSampleRateHz, params);
    REQUIRE(features.ok());
    CHECK_FALSE(track_bass(silence, features.value(), Grid{}, {}).ok());
    // Silence with the default bands: no kicks, no notes.
    const Result<FrontEndResult> quiet = analyse_front_end(silence, kSampleRateHz, {});
    const Result<BassTrackResult> none = track_bass(silence, quiet.value(), Grid{}, {});
    REQUIRE(none.ok());
    CHECK(none.value().notes.empty());
    CHECK(none.value().kick_times_s.empty());
}

TEST_CASE("bass tracker: click ratio" * doctest::skip()) {
    std::vector<double> kick_ratio, other_ratio;
    std::vector<CaseRun> runs = synthetic_runs();
    {
        synth::SynthParams params;
        params.kick = false;
        params.filter_tau_ms = 5.0;
        std::vector<synth::BassEvent> events;
        for (int note = 0; note < 8; ++note) {
            events.push_back({note * 8.0, 6.0, 29, 1.0, false});
        }
        runs.push_back(run_case({"no kick, long notes on beats", params, events}));
    }
    for (const CaseRun &run : runs) {
        const std::vector<float> &click = run.features.bands[3].flux;
        const std::vector<float> &lowmid = run.features.bands[2].flux;
        for (int k = 2; k + 2 < run.features.frames; ++k) {
            if (click[k] < 0.01f || click[k] < click[k - 1] || click[k] < click[k - 2] ||
                click[k] <= click[k + 1] || click[k] <= click[k + 2]) {
                continue;
            }
            const double t = k * 256.0 / kSampleRateHz;
            bool is_kick = false;
            for (const LabelEvent &e : run.audio.labels.kicks) {
                is_kick |= std::abs(e.t_s - t) < 0.010;
            }
            (is_kick ? kick_ratio : other_ratio).push_back(click[k] / (lowmid[k] + 1e-6));
        }
    }
    for (auto *v : {&kick_ratio, &other_ratio}) {
        std::sort(v->begin(), v->end());
        std::printf("n %zu min %.2f p1 %.2f p5 %.2f med %.2f p95 %.2f p99 %.2f max %.2f\n",
                    v->size(), v->front(), (*v)[v->size() / 100], (*v)[v->size() / 20],
                    (*v)[v->size() / 2], (*v)[v->size() * 19 / 20], (*v)[v->size() * 99 / 100],
                    v->back());
    }
}

TEST_CASE("bass tracker: table" * doctest::skip()) {
    std::printf("%-46s %4s %4s %6s %7s %7s %6s %6s\n", "case", "ref", "est", "F", "off med",
                "off p90", "pitch", "gate");
    for (const CaseRun &run : synthetic_runs()) {
        const NoteScore &s = run.score;
        std::printf("%-46s %4d %4d %6.3f %6.1fms %6.1fms %6.3f %6.3f\n", run.name.c_str(),
                    s.reference_count, s.estimate_count, s.onset_f, 1e3 * s.offset_error_median_s,
                    1e3 * s.offset_error_abs_p90_s, s.pitch_accuracy, s.gate_ratio_mae);
    }
}

TEST_CASE("bass tracker: errors" * doctest::skip()) {
    for (const CaseRun &run : synthetic_runs()) {
        const Labels &l = run.audio.labels;
        const double slot_s = slot_s_of(run);
        const auto slot_of = [&](double t_s) { return (t_s - l.grid.first_downbeat_s) / slot_s; };
        std::vector<bool> est_hit(run.estimates.size());
        std::vector<bool> ref_hit(l.bass_notes.size());
        for (const NoteMatch &m : run.matches) {
            est_hit[m.estimate_index] = true;
            ref_hit[m.reference_index] = true;
            const BassNoteLabel &r = l.bass_notes[m.reference_index];
            const BassNote &e = run.tracked.notes[m.estimate_index];
            if (e.pitch_midi != r.midi) {
                std::printf("%s: pitch at slot %.2f, ref %d est %d, conf %.2f\n", run.name.c_str(),
                            slot_of(r.onset_s), r.midi, e.pitch_midi, e.confidence);
            }
            if (std::abs(e.offset_s - r.offset_s) > 0.015) {
                std::printf("%s: offset at slot %.2f, error %+.1f ms\n", run.name.c_str(),
                            slot_of(r.onset_s), 1e3 * (e.offset_s - r.offset_s));
            }
        }
        for (size_t i = 0; i < run.estimates.size(); ++i) {
            if (!est_hit[i]) {
                std::printf("%s: false onset at slot %.2f, conf %.2f\n", run.name.c_str(),
                            slot_of(run.estimates[i].onset_s), run.tracked.notes[i].confidence);
            }
        }
        for (size_t i = 0; i < l.bass_notes.size(); ++i) {
            if (!ref_hit[i]) {
                std::printf("%s: missed onset at slot %.2f\n", run.name.c_str(),
                            slot_of(l.bass_notes[i].onset_s));
            }
        }
    }
}

TEST_CASE("bass tracker: sweep" * doctest::skip()) {
    std::vector<CaseRun> runs = synthetic_runs();
    const auto evaluate = [&](const BassTrackerParams &params) {
        int ref = 0;
        int est = 0;
        int matched = 0;
        double gate = 0.0;
        double worst_f = 1.0;
        for (CaseRun &run : runs) {
            track(run, params);
            ref += run.score.reference_count;
            est += run.score.estimate_count;
            matched += run.score.matched_count;
            gate += run.score.gate_ratio_mae / static_cast<double>(runs.size());
            worst_f = std::min(worst_f, run.score.onset_f);
        }
        const double precision = static_cast<double>(matched) / est;
        const double recall = static_cast<double>(matched) / ref;
        std::printf("P %.3f R %.3f F %.3f worst F %.3f gate MAE %.3f\n", precision, recall,
                    2.0 * precision * recall / (precision + recall), worst_f, gate);
    };
    struct Sweep {
        const char *name;
        double BassTrackerParams::*member;
        std::vector<double> values;
    };
    const Sweep sweeps[] = {
        {"onset_delta", &BassTrackerParams::onset_delta, {0.02, 0.04, 0.06, 0.08, 0.12}},
        {"envelope_window_ms", &BassTrackerParams::envelope_window_ms, {20, 25, 30}},
        {"offset_ratio", &BassTrackerParams::offset_ratio, {0.15, 0.25, 0.35}},
        {"slide_bias_ms", &BassTrackerParams::slide_bias_ms, {10, 20, 30}},
    };
    for (const Sweep &sweep : sweeps) {
        for (const double value : sweep.values) {
            BassTrackerParams params;
            params.*sweep.member = value;
            std::printf("%-20s %7.3f  ", sweep.name, value);
            evaluate(params);
        }
    }
}

namespace {

// Batch H: a kick under a rolling bass that fills the low band (a note on every 16th, the gate
// fully open, loud, no sidechain), hats on the offbeats. The kick's low-band rise is buried, its
// click is not. Exact ground truth from the generator.
synth::SynthOutput buried_kicks() {
    synth::SynthParams p;
    p.bpm = 150.0;
    p.bars = 16;
    p.kick_level = 0.25;
    p.bass_level = 0.9;
    p.bass_decay_tau_ms = 2000.0;
    p.filter_end_hz = 600.0;
    p.hats = true;
    p.hat_level = 0.15;
    // Bars 0 to 3 leave the beat to the kick (K B B B), as a track's clearer passages do; from
    // bar 4 the bass plays every 16th and fills the low band under the kick.
    std::vector<synth::BassEvent> events;
    for (int slot = 0; slot < 16 * p.bars; ++slot) {
        if (slot < 64 && slot % 4 == 0) {
            continue;
        }
        events.push_back({static_cast<double>(slot), 1.0, slot % 8 == 7 ? 41 : 29, 1.0, false});
    }
    return synth::render(p, events);
}

// Recall over the buried bars (4 on) only.
struct KickRecall {
    double bass_tracker = 0.0; // Labelled kicks the bass tracker finds within 20 ms.
    double beat_tracker = 0.0; // Labelled kick beats the beat tracker marks.
    int offbeat_kicks = 0;     // Kicks found off the beat (on the hats), anywhere.
};

KickRecall kick_recall(const synth::SynthOutput &audio, const BassTrackerParams &bass_params,
                       const BeatTrackerParams &beat_params) {
    KickRecall r;
    const Result<FrontEndResult> features = analyse_front_end(audio.samples, kSampleRateHz, {});
    REQUIRE(features.ok());
    const Grid grid = grid_of(audio.labels);
    const Result<BassTrackResult> tracked =
        track_bass(audio.samples, features.value(), grid, bass_params);
    REQUIRE(tracked.ok());
    const double bar_s = 4.0 * 60.0 / audio.labels.grid.bpm;
    std::vector<LabelEvent> kicks;
    for (const LabelEvent &k : audio.labels.kicks) {
        if (k.t_s >= audio.labels.grid.first_downbeat_s + 4.0 * bar_s - 0.01) {
            kicks.push_back(k);
        }
    }
    int found = 0;
    for (const LabelEvent &k : kicks) {
        found +=
            std::any_of(tracked.value().kick_times_s.begin(), tracked.value().kick_times_s.end(),
                        [&](double t) { return std::abs(t - k.t_s) <= 0.020; });
    }
    const double beat_s = 60.0 / audio.labels.grid.bpm;
    for (const double t : tracked.value().kick_times_s) {
        const double beats = (t - audio.labels.grid.first_downbeat_s) / beat_s;
        r.offbeat_kicks += std::abs(beats - std::round(beats)) > 0.25;
    }
    r.bass_tracker = kicks.empty() ? 0.0 : static_cast<double>(found) / kicks.size();
    const Result<BeatTrackResult> beats = track_beats(features.value(), beat_params);
    REQUIRE(beats.ok());
    int marked = 0;
    for (const LabelEvent &k : kicks) {
        const double index = (k.t_s - beats.value().first_kick_beat_s) / (60.0 / beats.value().bpm);
        const auto i = static_cast<long long>(std::llround(index));
        marked += i >= 0 && i < static_cast<long long>(beats.value().beat_has_kick.size()) &&
                  beats.value().beat_has_kick[static_cast<size_t>(i)];
    }
    r.beat_tracker = kicks.empty() ? 0.0 : static_cast<double>(marked) / kicks.size();
    return r;
}

} // namespace

TEST_CASE("bass tracker: kicks buried under a rolling bass are missed today (Batch H1)") {
    const synth::SynthOutput audio = buried_kicks();
    REQUIRE(audio.labels.kicks.size() == 64);
    const KickRecall today = kick_recall(audio, {}, {});
    MESSAGE("buried bars: bass tracker recall " << today.bass_tracker << ", beat tracker "
                                                << today.beat_tracker);
    // The miss this batch is about: under the filled low band most kicks go unheard.
    CHECK(today.bass_tracker < 0.6);
    CHECK(today.beat_tracker < 0.8);
    CHECK(today.offbeat_kicks == 0);
}

TEST_CASE("bass tracker: the kick click contrast adds no kick off the beat or without a kick, "
          "and is today's detector when off (Batch H2)") {
    // The buried case's misses are kicks whose click is masked by the bass attacks (H1, PROGRESS):
    // the contrast rule is for a clear click over a buried low rise (as on Alien Army), so here it
    // must only do no harm.
    const synth::SynthOutput audio = buried_kicks();
    BassTrackerParams bass;
    bass.kick_click_contrast = 2.0;
    BeatTrackerParams beat;
    beat.kick_click_contrast = 2.0;
    const KickRecall today = kick_recall(audio, {}, {});
    const KickRecall contrast = kick_recall(audio, bass, beat);
    MESSAGE("buried bars, contrast 2: bass tracker recall "
            << contrast.bass_tracker << ", beat tracker " << contrast.beat_tracker);
    CHECK(contrast.bass_tracker >= today.bass_tracker);
    CHECK(contrast.beat_tracker >= today.beat_tracker);
    CHECK(contrast.offbeat_kicks == 0); // The offbeat hats stay hats.

    // Bars with the bass and hats but no kick: the contrast rule adds no kick there.
    synth::SynthParams p;
    p.bpm = 150.0;
    p.bars = 16;
    p.kick = false;
    p.bass_level = 0.9;
    p.bass_decay_tau_ms = 2000.0;
    p.filter_end_hz = 600.0;
    p.hats = true;
    p.hat_level = 0.15;
    std::vector<synth::BassEvent> events;
    for (int slot = 0; slot < 16 * p.bars; ++slot) {
        events.push_back({static_cast<double>(slot), 1.0, slot % 8 == 7 ? 41 : 29, 1.0, false});
    }
    synth::SynthOutput mixed = synth::render(p, events);
    synth::SynthParams k = p;
    k.kick = true;
    k.kick_level = 0.25;
    k.bass_level = 0.0;
    k.hats = false;
    const synth::SynthOutput kicks_only = synth::render(k, {});
    const double bar_s = 4.0 * 60.0 / p.bpm;
    const double first_s = mixed.labels.grid.first_downbeat_s;
    for (size_t i = 0; i < mixed.samples.size() && i < kicks_only.samples.size(); ++i) {
        if (static_cast<double>(i) / kSampleRateHz < first_s + 8.0 * bar_s) {
            mixed.samples[i] += kicks_only.samples[i]; // The kick in the first 8 bars only.
        }
    }
    const Result<FrontEndResult> features = analyse_front_end(mixed.samples, kSampleRateHz, {});
    REQUIRE(features.ok());
    const auto late = [&](const BassTrackerParams &params) {
        const Result<BassTrackResult> tracked =
            track_bass(mixed.samples, features.value(), grid_of(mixed.labels), params);
        REQUIRE(tracked.ok());
        int n = 0;
        for (const double t : tracked.value().kick_times_s) {
            n += t > first_s + 8.0 * bar_s + 0.05;
        }
        return n;
    };
    CHECK(late(bass) == late({}));

    // Off (0, the default) is today's detector exactly.
    BassTrackerParams off;
    off.kick_click_contrast = 0.0;
    const Result<FrontEndResult> f2 = analyse_front_end(audio.samples, kSampleRateHz, {});
    REQUIRE(f2.ok());
    const Result<BassTrackResult> a =
        track_bass(audio.samples, f2.value(), grid_of(audio.labels), {});
    const Result<BassTrackResult> b =
        track_bass(audio.samples, f2.value(), grid_of(audio.labels), off);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().kick_times_s == b.value().kick_times_s);
    BeatTrackerParams beat_off;
    beat_off.kick_click_contrast = 0.0;
    const Result<BeatTrackResult> c = track_beats(f2.value(), {});
    const Result<BeatTrackResult> d = track_beats(f2.value(), beat_off);
    REQUIRE(c.ok());
    REQUIRE(d.ok());
    CHECK(c.value().beat_has_kick == d.value().beat_has_kick);
}
