// Hats, residual features and bass aggregates against synthetic ground truth.

#include "analysis/bass_aggregates.h"
#include "analysis/front_end.h"
#include "analysis/residual.h"
#include "synth_generator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;

FrontEndResult features_of(const std::vector<float> &audio) {
    Result<FrontEndResult> features = analyse_front_end(audio, kSampleRateHz, {});
    REQUIRE(features.ok());
    return std::move(features.value());
}

double median_of(std::vector<float> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// Bass notes as the tracker would report them, from synthetic labels.
std::vector<BassNote> notes_from(const Labels &labels) {
    const double slot_s = 60.0 / labels.grid.bpm / 4.0;
    std::vector<BassNote> notes;
    for (const BassNoteLabel &l : labels.bass_notes) {
        BassNote n;
        n.onset_s = l.onset_s;
        n.offset_s = l.offset_s;
        n.gate_ratio = (l.offset_s - l.onset_s) / slot_s;
        n.pitch_midi = l.midi;
        n.slide_in = l.slide_in;
        notes.push_back(n);
    }
    return notes;
}

Grid grid_of(const Labels &labels) {
    return {labels.grid.bpm, seconds_to_samples(labels.grid.first_downbeat_s, kSampleRateHz),
            kSampleRateHz};
}

} // namespace

TEST_CASE("hats: every offbeat hat is found within 5 ms; other hits are kick clicks") {
    synth::SynthParams params;
    params.bars = 8;
    params.hats = true;
    const synth::SynthOutput out =
        synth::render(params, synth::make_pattern(synth::Pattern::rolling, 8, 29, 0.6));
    const Result<ResidualFeatures> residual = analyse_residual(features_of(out.samples), {});
    REQUIRE(residual.ok());
    const std::vector<HatOnset> &hats = residual.value().hats;
    const double slot_s = 60.0 / params.bpm / 4.0;
    std::vector<double> expected;
    for (int beat = 0; beat < params.bars * 4; ++beat) {
        expected.push_back(params.lead_in_s + (beat * 4 + 2) * slot_s);
    }
    const auto near = [](const std::vector<double> &times, double t, double tolerance) {
        return std::any_of(times.begin(), times.end(),
                           [&](double x) { return std::abs(x - t) <= tolerance; });
    };
    std::vector<double> found;
    for (const HatOnset &hat : hats) {
        found.push_back(hat.t_s);
    }
    for (const double t : expected) {
        CAPTURE(t);
        CHECK(near(found, t, 0.005));
    }
    std::vector<double> kicks;
    for (const LabelEvent &kick : out.labels.kicks) {
        kicks.push_back(kick.t_s);
    }
    for (const double t : found) {
        CAPTURE(t);
        CHECK((near(expected, t, 0.005) || near(kicks, t, 0.010)));
    }
}

TEST_CASE("residual: noise is flat and bright, a held bass is tonal and dark") {
    synth::SynthParams noise_params;
    noise_params.kick = false;
    noise_params.bass_level = 0.0;
    noise_params.noise_level = 0.1;
    const Result<ResidualFeatures> noise =
        analyse_residual(features_of(synth::render(noise_params, {}).samples), {});
    REQUIRE(noise.ok());

    synth::SynthParams bass_params;
    bass_params.kick = false;
    bass_params.bass_decay_tau_ms = 5000.0;
    bass_params.filter_end_hz = 1500.0; // Harmonics above the residual's 400 Hz edge.
    const Result<ResidualFeatures> bass = analyse_residual(
        features_of(synth::render(bass_params, {{0.0, 32.0, 41, 1.0, false}}).samples), {});
    REQUIRE(bass.ok());

    // Middles of the two signals, away from the edges.
    const auto middle = [](const std::vector<float> &x) {
        return std::vector<float>(x.begin() + static_cast<std::ptrdiff_t>(x.size() / 3),
                                  x.begin() + static_cast<std::ptrdiff_t>(2 * x.size() / 3));
    };
    CHECK(median_of(middle(noise.value().flatness)) > 0.5);
    CHECK(median_of(middle(bass.value().flatness)) < 0.2);
    CHECK(median_of(middle(noise.value().centroid_hz)) > 3000.0);
    CHECK(median_of(middle(bass.value().centroid_hz)) < 1500.0);
    CHECK(median_of(middle(noise.value().energy_db)) > -80.0);
}

TEST_CASE("bass aggregates: per bar counts, gates, rests and pitch range") {
    synth::SynthParams params;
    params.bars = 8;
    const synth::SynthOutput rolling =
        synth::render(params, synth::make_pattern(synth::Pattern::rolling_octave_jump, 8, 29, 0.6));
    const double duration_s = static_cast<double>(rolling.samples.size()) / kSampleRateHz;
    const BassAggregates a =
        aggregate_bass(notes_from(rolling.labels), grid_of(rolling.labels), duration_s, 4, 1);
    REQUIRE(a.bars.size() == 8);
    for (const BassAggregate &bar : a.bars) {
        CHECK(bar.notes == 12);
        CHECK(bar.gate_mean == doctest::Approx(0.6).epsilon(0.001));
        CHECK(bar.gate_variance == doctest::Approx(0.0).epsilon(1e-6));
        CHECK(bar.rests == 4); // The kick slot of each beat.
        CHECK(bar.slides == 0);
        CHECK(bar.pitch_low == 29);
        CHECK(bar.pitch_high == 41); // The octave jump.
    }
    // Phrases of 4 bars starting at bar 1: bar 0 alone, then 1 to 4, then 5 to 7.
    REQUIRE(a.phrases.size() == 3);
    CHECK(a.phrases[0].notes == 12);
    CHECK(a.phrases[1].notes == 48);
    CHECK(a.phrases[2].notes == 36);
    CHECK(a.phrases[1].start_s == doctest::Approx(a.bars[1].start_s));
    CHECK(a.phrases[1].end_s == doctest::Approx(a.bars[4].end_s));

    // Offbeat K-B-: one note per beat, three rest slots per beat. (Gate 0.5 would end exactly at
    // the slot's middle, the rest rule's boundary.)
    const synth::SynthOutput offbeat =
        synth::render(params, synth::make_pattern(synth::Pattern::offbeat, 8, 29, 0.6));
    const BassAggregates b =
        aggregate_bass(notes_from(offbeat.labels), grid_of(offbeat.labels), duration_s, 8, 0);
    REQUIRE(!b.bars.empty());
    CHECK(b.bars[3].notes == 4);
    CHECK(b.bars[3].rests == 12);
    REQUIRE(b.phrases.size() == 1);
    CHECK(b.phrases[0].notes == 32);
}

TEST_CASE("bass aggregates: no notes gives empty aggregates and every slot a rest") {
    const Grid grid{150.0, 0, kSampleRateHz};
    const BassAggregates a = aggregate_bass({}, grid, 3.2, 8, 0); // Two bars at 150 BPM.
    REQUIRE(a.bars.size() == 2);
    CHECK(a.bars[0].notes == 0);
    CHECK(a.bars[0].rests == 16);
    CHECK(a.bars[0].pitch_low == 0);
}

TEST_CASE("residual: parameters round trip through the registry") {
    ParamRegistry registry;
    REQUIRE(declare_residual_params(registry).ok());
    REQUIRE(registry.set("residual.hat_min_gap_ms", 60.0).ok());
    CHECK(residual_params_from(registry.values()).hat_min_gap_ms == 60.0);
}
