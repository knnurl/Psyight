// Engine inputs compatibility golden (docs/PLAN_HEX_VISUALS.md section 8): every EngineInputs
// field that existed before the hexagon-visuals additions, at 200 times over a synthetic track
// with hand-set sections (two builds back to back among them), equals what the unchanged code
// gave (tests/golden/inputs_baseline.json, written before the additions). Fields added since are
// not compared. Rewrite only for an intended change:
//   set PSYIGHT_UPDATE_GOLDEN=1 && psyight_tests -tc="golden*"

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
    CHECK(kModSources.back() == ModSource::build_progress);

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
