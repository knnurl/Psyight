// Engine inputs compatibility golden (docs/PLAN_HEX_VISUALS.md section 8): every EngineInputs
// field that existed before the hexagon-visuals additions, at 200 times over a synthetic track
// with hand-set sections (two builds back to back among them), equals what the unchanged code
// gave (tests/golden/inputs_baseline.json, written before the additions). Fields added since are
// not compared. Rewrite only for an intended change:
//   set PSYIGHT_UPDATE_GOLDEN=1 && psyight_tests -tc="golden*"

#include "analysis/live_analysis.h"
#include "analysis/track_analysis.h"
#include "engine/effect_spec.h"
#include "engine/inputs.h"
#include "engine/modulation.h"
#include "engine/rig.h"
#include "synth_generator.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace psyight;
using nlohmann::json;

namespace {

constexpr int kSampleRateHz = 48000;

std::filesystem::path golden_path() {
    return std::filesystem::path(PSYIGHT_TESTS_DIR) / "golden" / "inputs_baseline.json";
}

bool update_requested() {
    char *value = nullptr;
    size_t length = 0;
    const bool set = _dupenv_s(&value, &length, "PSYIGHT_UPDATE_GOLDEN") == 0 && value != nullptr &&
                     std::string(value) == "1";
    std::free(value);
    return set;
}

// Infinities (kNever) as text: JSON has none.
json number(double v) { return std::isfinite(v) ? json(v) : json(v > 0 ? "inf" : "-inf"); }

json ages(const EventAges &a) {
    json out = json::array();
    for (const double v : a) {
        out.push_back(number(v));
    }
    return out;
}

json fields_of(const EngineInputs &in) {
    return {{"t_s", number(in.t_s)},
            {"beat_phase", number(in.beat_phase)},
            {"beat_index", in.beat_index},
            {"beat_in_bar", in.beat_in_bar},
            {"bar_phase", number(in.bar_phase)},
            {"bar_index", in.bar_index},
            {"slot_phase", number(in.slot_phase)},
            {"beat_s", number(in.beat_s)},
            {"phrase_phase", number(in.phrase_phase)},
            {"phrase_index", in.phrase_index},
            {"since_kick_s", number(in.since_kick_s)},
            {"kicks_so_far", in.kicks_so_far},
            {"bass_active", in.bass_active},
            {"bass_midi", in.bass_midi},
            {"bass_cents", number(in.bass_cents)},
            {"bass_note_phase", number(in.bass_note_phase)},
            {"bass_gate_ratio", number(in.bass_gate_ratio)},
            {"bass_decay_tau_ms", number(in.bass_decay_tau_ms)},
            {"bass_velocity", number(in.bass_velocity)},
            {"bass_confidence", number(in.bass_confidence)},
            {"since_bass_onset_s", number(in.since_bass_onset_s)},
            {"since_bass_offset_s", number(in.since_bass_offset_s)},
            {"bass_notes_so_far", in.bass_notes_so_far},
            {"bass_slide", in.bass_slide},
            {"section", in.section},
            {"section_progress", number(in.section_progress)},
            {"section_index", in.section_index},
            {"since_section_s", number(in.since_section_s)},
            {"section_length_s", number(in.section_length_s)},
            {"section_end_known", in.section_end_known},
            {"section_energy", in.section_energy},
            {"riser", number(in.riser)},
            {"intensity", number(in.intensity)},
            {"residual_level", number(in.residual_level)},
            {"residual_centroid_hz", number(in.residual_centroid_hz)},
            {"residual_flatness", number(in.residual_flatness)},
            {"since_hat_s", number(in.since_hat_s)},
            {"hats_so_far", in.hats_so_far},
            {"hat_ages_s", ages(in.hat_ages_s)},
            {"bass_onset_ages_s", ages(in.bass_onset_ages_s)}};
}

} // namespace

TEST_CASE("golden inputs: the engine inputs' older fields are unchanged at 200 times") {
    synth::SynthParams p;
    p.bpm = 150.0;
    p.bars = 40;
    p.hats = true;
    p.sidechain_depth = 0.5;
    const synth::SynthOutput audio =
        synth::render(p, synth::make_pattern(synth::Pattern::rolling, p.bars, 29, 0.7));
    TrackAnalysisOptions options;
    options.grid =
        Grid{p.bpm, seconds_to_samples(audio.labels.grid.first_downbeat_s, kSampleRateHz),
             kSampleRateHz};
    const Result<TrackAnalysis> analysed = analyse_track(audio.samples, kSampleRateHz, {}, options);
    REQUIRE(analysed.ok());
    const TrackAnalysis &a = analysed.value();
    const double bar_s = 4.0 * 60.0 / p.bpm;
    const double first_s = audio.labels.grid.first_downbeat_s;
    const std::pair<int, const char *> starts[] = {{0, "intro"},  {4, "groove"}, {12, "build"},
                                                   {16, "build"}, {20, "drop"},  {28, "breakdown"},
                                                   {36, "outro"}};
    std::vector<DetectedSection> sections;
    for (size_t i = 0; i < std::size(starts); ++i) {
        DetectedSection s;
        s.start_s = i == 0 ? 0.0 : first_s + starts[i].first * bar_s;
        s.end_s = i + 1 < std::size(starts) ? first_s + starts[i + 1].first * bar_s : a.duration_s;
        s.label = starts[i].second;
        sections.push_back(s);
    }
    const SectionTimeline timeline = section_timeline(a, a.grid, sections);
    json now = json::array();
    for (int k = 0; k < 200; ++k) {
        const double t_s = a.duration_s * (k + 0.5) / 200.0;
        now.push_back({{"analysis", fields_of(inputs_at(a, a.grid, t_s))},
                       {"timeline", fields_of(inputs_at(a, a.grid, t_s, &timeline))}});
    }
    if (update_requested()) {
        std::filesystem::create_directories(golden_path().parent_path());
        std::ofstream(golden_path()) << now.dump(1) << "\n";
        MESSAGE("wrote " << golden_path().string());
        return;
    }
    std::ifstream file(golden_path());
    REQUIRE_MESSAGE(file.good(), "missing " << golden_path().string()
                                            << "; set PSYIGHT_UPDATE_GOLDEN=1 to create it");
    const json golden = json::parse(file);
    REQUIRE(golden.size() == now.size());
    for (size_t k = 0; k < golden.size(); ++k) {
        for (const char *source : {"analysis", "timeline"}) {
            for (const auto &[key, value] : golden[k][source].items()) {
                CAPTURE(k);
                CAPTURE(source);
                CAPTURE(key);
                CHECK(now[k][source][key] == value);
            }
        }
    }
}

TEST_CASE("inputs: consecutive builds are one run for build progress; the new sources") {
    TrackAnalysis a;
    a.sample_rate_hz = kSampleRateHz;
    a.duration_s = 60.0;
    a.grid = {150.0, 0, kSampleRateHz};
    a.sections = {
        {0.0, 12.8, "groove", 1.0}, {12.8, 19.2, "build", 1.0}, // Two 4-bar builds back to back.
        {19.2, 25.6, "build", 1.0}, {25.6, 38.4, "drop", 1.0},
        {38.4, 44.8, "build", 1.0}, // A build of its own.
        {44.8, 60.0, "drop", 1.0}};
    // In the first build of the run: progress through the whole 12.8 s.
    EngineInputs in = inputs_at(a, a.grid, 16.0);
    CHECK(in.section_progress == doctest::Approx(3.2 / 6.4));
    CHECK(in.build_progress == doctest::Approx(3.2 / 12.8));
    CHECK(in.since_build_s == doctest::Approx(3.2));
    CHECK(in.build_length_s == doctest::Approx(12.8));
    CHECK(in.build_end_known);
    // In the second: the section restarts, the run does not.
    in = inputs_at(a, a.grid, 22.4);
    CHECK(in.section_progress == doctest::Approx(0.5));
    CHECK(in.build_progress == doctest::Approx(9.6 / 12.8));
    CHECK(source_value(ModSource::build_progress, in, 1) == doctest::Approx(0.75));
    // A build alone: its own values.
    in = inputs_at(a, a.grid, 41.6);
    CHECK(in.build_progress == doctest::Approx(in.section_progress));
    CHECK(in.build_length_s == doctest::Approx(6.4));
    // Outside builds: 0.
    in = inputs_at(a, a.grid, 30.0);
    CHECK(in.build_progress == 0.0);
    CHECK_FALSE(in.build_end_known);
    in.bass_confidence = 0.37;
    CHECK(source_value(ModSource::bass_confidence, in, 1) == doctest::Approx(0.37));
    CHECK(mod_source_from_id("build_progress") == ModSource::build_progress);
    CHECK(mod_source_from_id("bass_confidence") == ModSource::bass_confidence);
    // Appended sources keep every older one in place (Tension came after Build progress).
    CHECK(kModSources[29] == ModSource::build_progress);

    // Stutter: the pre-drop gap of the joined builds darkens only the run's last beat, where
    // the plain pre-drop gap darkens each build's.
    StutterParams joined;
    joined.mode = 4;
    StutterParams plain;
    plain.mode = 3;
    const Rig rig = default_rig();
    const Palette palette{"p", {{1, 1, 1}}};
    const auto gate = [&](const StutterParams &p, double t_s) {
        Frame out(rig.pixels.size());
        make_effect({p})->render({rig, inputs_at(a, a.grid, t_s), palette, 1}, out);
        return out[0].r;
    };
    CHECK(gate(plain, 19.0) < 0.1f);  // The end of the first build section.
    CHECK(gate(joined, 19.0) > 0.9f); // Inside the run.
    CHECK(gate(joined, 25.5) < 0.1f); // The run's last beat.
    CHECK(gate(plain, 25.5) < 0.1f);
}

TEST_CASE("inputs: tension climbs through a build in both modes and is 0 outside") {
    // File mode: a build from 12.8 to 19.2 s whose riser rises to 0.9 by 15 s and holds.
    TrackAnalysis a;
    a.sample_rate_hz = kSampleRateHz;
    a.duration_s = 30.0;
    a.grid = {150.0, 0, kSampleRateHz};
    a.sections = {
        {0.0, 12.8, "groove", 1.0}, {12.8, 19.2, "build", 1.0}, {19.2, 30.0, "drop", 1.0}};
    a.mix_features.sample_rate_hz = kSampleRateHz;
    a.mix_features.hop_samples = 480; // Frames of 10 ms.
    const size_t frames = 3000;
    a.residual.energy_db.assign(frames, -30.0f);
    a.residual.centroid_hz.assign(frames, 2000.0f);
    a.residual.flatness.assign(frames, 0.1f);
    a.riser.assign(frames, 0.0f);
    for (size_t f = 1280; f < 1920; ++f) {
        a.riser[f] = static_cast<float>(std::min(0.9, 0.9 * (f - 1280) / 220.0));
    }
    CHECK(inputs_at(a, a.grid, 14.0).tension == doctest::Approx(0.9 * 120 / 220.0).epsilon(0.01));
    CHECK(inputs_at(a, a.grid, 19.0).tension == doctest::Approx(6.2 / 6.4)); // Progress leads.
    CHECK(inputs_at(a, a.grid, 10.0).tension == 0.0);
    CHECK(inputs_at(a, a.grid, 20.0).tension == 0.0);
    double last = 0.0;
    for (double t = 12.8; t < 19.2; t += 0.05) {
        const double v = inputs_at(a, a.grid, t).tension;
        CHECK(v >= last);
        last = v;
    }
    CHECK(source_value(ModSource::tension, inputs_at(a, a.grid, 19.0), 1) ==
          doctest::Approx(6.2 / 6.4));
    CHECK(mod_source_from_id("tension") == ModSource::tension);
    CHECK(kModSources.back() == ModSource::tension);

    // Live mode: a build from 10 s, phrases of 12.8 s; the riser jumps to 0.6 at 11 s and to 1
    // at 30 s.
    LiveState s;
    s.tempo_known = true;
    s.locked = true;
    s.period_s = 0.4;
    s.phrase_bars = 8;
    s.section = LiveSection::build;
    s.section_start_s = 10.0;
    s.section_count = 2;
    last = 0.0;
    for (double t = 10.0; t < 60.0; t += 0.1) {
        s.riser = t < 11.0 ? 0.0 : t < 30.0 ? 0.6 : 1.0;
        const double v = inputs_at(s, t).tension;
        CHECK(v >= last - 1e-12);
        last = v;
    }
    s.riser = 0.0;
    CHECK(inputs_at(s, 10.0 + 12.8).tension == doctest::Approx(0.5)); // One phrase: half way.
    CHECK(inputs_at(s, 10.0 + 25.6).tension == doctest::Approx(0.75));
    s.riser = 0.6; // max(ramp, 0.6 * (0.5 + 0.5 * 0.5)) one phrase in.
    CHECK(inputs_at(s, 10.0 + 12.8).tension == doctest::Approx(0.5));
    CHECK(inputs_at(s, 10.0 + 1.0).tension ==
          doctest::Approx(0.6 * (0.5 + 0.5 * (1.0 - std::exp2(-1.0 / 12.8)))));
    s.section = LiveSection::groove;
    CHECK(inputs_at(s, 20.0).tension == 0.0);
}

TEST_CASE("inputs: the events the engine reads come from the labels, or detected only when asked") {
    TrackAnalysis a;
    a.events = {{"fill", 30.0, 1.6}};
    CHECK(engine_events(a, nullptr, false).empty());
    CHECK(engine_events(a, nullptr, true) == a.events);
    const SectionTimeline t =
        section_timeline(a, {150.0, 0, kSampleRateHz}, {{0.0, 60.0, "groove", 1.0}},
                         {{"fill", 20.0, 1.6}, {"break", 10.0, 3.2}});
    REQUIRE(t.events.size() == 2);
    CHECK(t.events[0].kind == "break");            // In time order.
    CHECK(engine_events(a, &t, true) == t.events); // Labelled, never the detected ones.
}
