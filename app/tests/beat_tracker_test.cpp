// Beat tracker against the synthetic set's exact kicks and downbeats.

#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "synthetic_set.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;

struct BeatRun {
    synth::SynthOutput audio;
    BeatTrackResult beats;
};

BeatRun run_beats(const synth::SynthParams &params, const std::vector<synth::BassEvent> &events,
                  const BeatTrackerParams &beat_params = {}) {
    BeatRun run;
    run.audio = synth::render(params, events);
    const Result<FrontEndResult> features = analyse_front_end(run.audio.samples, kSampleRateHz, {});
    REQUIRE(features.ok());
    Result<BeatTrackResult> beats = track_beats(features.value(), beat_params);
    REQUIRE(beats.ok());
    run.beats = std::move(beats.value());
    return run;
}

// Distance from t_s to the nearest beat of the grid.
double grid_distance_s(const Grid &grid, double t_s) {
    const double beats =
        grid.slot_position(seconds_to_samples(t_s, grid.sample_rate_hz)) / kSlotsPerBeat;
    const double beat_s = kSlotsPerBeat * grid.slot_length_samples() / grid.sample_rate_hz;
    return std::abs(beats - std::round(beats)) * beat_s;
}

// One bar per root: the prototype's progression 0, 0, +3, -2 semitones from F1.
std::vector<synth::BassEvent> bar_progression(int bars) {
    constexpr int kRoots[] = {29, 29, 32, 27};
    std::vector<synth::BassEvent> events;
    for (int bar = 0; bar < bars; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            for (int slot = 1; slot < 4; ++slot) {
                events.push_back(
                    {bar * 16.0 + beat * 4.0 + slot, 0.7, kRoots[bar % 4], 1.0, false});
            }
        }
    }
    return events;
}

} // namespace

TEST_CASE("beat tracker: tempo and beats on every synthetic case") {
    for (const synth::SyntheticCase &c : synth::synthetic_set()) {
        CAPTURE(c.name);
        const BeatRun run = run_beats(c.params, c.events);
        CHECK(run.beats.bpm == doctest::Approx(c.params.bpm).epsilon(0.0003)); // 0.05 BPM.
        for (const LabelEvent &kick : run.audio.labels.kicks) {
            CHECK(grid_distance_s(run.beats.grid, kick.t_s) <= 0.005);
        }
        CHECK(run.beats.tempo_confidence > 0.3);
    }
}

TEST_CASE("beat tracker: the half and double tempos stay outside the default range") {
    for (const double bpm : {125.0, 145.0, 175.0}) {
        CAPTURE(bpm);
        synth::SynthParams params;
        params.bpm = bpm;
        params.bars = 8;
        const BeatRun run =
            run_beats(params, synth::make_pattern(synth::Pattern::offbeat, 8, 29, 0.5));
        CHECK(run.beats.bpm == doctest::Approx(bpm).epsilon(0.0003));
    }
}

TEST_CASE("beat tracker: a bar-level bass progression gives the downbeat") {
    for (const double bpm : {150.0, 160.0}) {
        CAPTURE(bpm);
        synth::SynthParams params;
        params.bpm = bpm;
        params.bars = 16;
        const BeatRun run = run_beats(params, bar_progression(16));
        const double bar_s = 4.0 * 60.0 / bpm;
        const double bars = (samples_to_seconds(run.beats.grid.first_downbeat, kSampleRateHz) -
                             run.audio.labels.downbeats.front().t_s) /
                            bar_s;
        CHECK(std::abs(bars - std::round(bars)) * bar_s <= 0.005);
        CHECK(run.beats.downbeat_confidence > 0.1); // At most 0.25: the next phase shares 3 beats.
    }
}

TEST_CASE("beat tracker: identical input gives bit-identical beats") {
    const synth::SyntheticCase c = synth::synthetic_set()[2];
    const BeatRun a = run_beats(c.params, c.events);
    const BeatRun b = run_beats(c.params, c.events);
    CHECK(std::memcmp(&a.beats.bpm, &b.beats.bpm, sizeof(double)) == 0);
    CHECK(a.beats.beat_times_s == b.beats.beat_times_s);
    CHECK(a.beats.grid.first_downbeat == b.beats.grid.first_downbeat);
}

TEST_CASE("beat tracker: silence and short input give no beats, not an error") {
    const std::vector<float> silence(10 * kSampleRateHz, 0.0f);
    const Result<FrontEndResult> features = analyse_front_end(silence, kSampleRateHz, {});
    const Result<BeatTrackResult> beats = track_beats(features.value(), {});
    REQUIRE(beats.ok());
    CHECK(beats.value().beat_times_s.empty());
    CHECK(beats.value().bpm == 0.0);
}

TEST_CASE("beat tracker: parameters round trip through the registry") {
    ParamRegistry registry;
    REQUIRE(declare_beat_tracker_params(registry).ok());
    const BeatTrackerParams defaults;
    const BeatTrackerParams from_registry = beat_tracker_params_from(registry.values());
    CHECK(std::memcmp(&defaults, &from_registry, sizeof(BeatTrackerParams)) == 0);
    REQUIRE(registry.set("beat.bars_per_phrase", 16.0).ok());
    CHECK(beat_tracker_params_from(registry.values()).bars_per_phrase == 16.0);
}
