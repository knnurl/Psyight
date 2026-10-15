#include "synth_generator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace psyight;
using namespace psyight::synth;

namespace {

constexpr int kSampleRateHz = 48000;
constexpr std::int64_t kSlotSamples160 = 4500; // One 16th at 160 BPM and 48 kHz.

std::int64_t to_samples(double t_s) { return std::llround(t_s * kSampleRateHz); }

double rms(const std::vector<float> &x, std::int64_t begin, std::int64_t end) {
    double sum = 0.0;
    for (std::int64_t i = begin; i < end; ++i) {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(sum / static_cast<double>(end - begin));
}

double max_abs(const std::vector<float> &x, std::int64_t begin, std::int64_t end) {
    double peak = 0.0;
    for (std::int64_t i = begin; i < end; ++i) {
        peak = std::max(peak, static_cast<double>(std::abs(x[i])));
    }
    return peak;
}

// Fundamental by normalised autocorrelation over lags for 30 to 400 Hz, with parabolic refinement.
double autocorrelation_pitch_hz(const std::vector<float> &x, std::int64_t begin, std::int64_t end) {
    const int min_lag = kSampleRateHz / 400;
    const int max_lag = kSampleRateHz / 30;
    const std::int64_t length = end - begin - max_lag;
    std::vector<double> correlation(max_lag + 2, 0.0);
    for (int lag = min_lag - 1; lag <= max_lag + 1; ++lag) {
        double sum = 0.0;
        for (std::int64_t i = 0; i < length; ++i) {
            sum += static_cast<double>(x[begin + i]) * x[begin + i + lag];
        }
        correlation[lag] = sum;
    }
    int best = min_lag;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        if (correlation[lag] > correlation[best]) {
            best = lag;
        }
    }
    const double a = correlation[best - 1];
    const double b = correlation[best];
    const double c = correlation[best + 1];
    const double refined_lag = best + 0.5 * (a - c) / (a - 2.0 * b + c);
    return kSampleRateHz / refined_lag;
}

// Mean frequency from the spacing of upward zero crossings in [begin, end).
double zero_crossing_hz(const std::vector<float> &x, std::int64_t begin, std::int64_t end) {
    std::int64_t first = -1;
    std::int64_t last = -1;
    int crossings = 0;
    for (std::int64_t i = begin + 1; i < end; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0) {
                first = i;
            }
            last = i;
            ++crossings;
        }
    }
    return crossings < 2 ? 0.0
                         : (crossings - 1) * static_cast<double>(kSampleRateHz) / (last - first);
}

double midi_hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

SynthParams bass_only() {
    SynthParams params;
    params.kick = false;
    params.kick_click = false;
    return params;
}

} // namespace

TEST_CASE("synth: rolling pattern at 160 BPM has exact sample-aligned ground truth") {
    SynthParams params; // 160 BPM, 4 bars, 0.5 s lead-in.
    const SynthOutput out = render(params, make_pattern(Pattern::rolling, 4, 29, 0.6));
    const Labels &labels = out.labels;
    const std::int64_t lead_in = 24000;

    CHECK(labels.sample_rate == kSampleRateHz);
    CHECK(labels.grid.bpm == 160.0);
    CHECK(labels.grid.first_downbeat_s == 0.5);
    REQUIRE(labels.kicks.size() == 16);
    REQUIRE(labels.beats.size() == 16);
    REQUIRE(labels.downbeats.size() == 4);
    CHECK(labels.downbeats[3].t_s ==
          static_cast<double>(lead_in + 3 * 16 * kSlotSamples160) / kSampleRateHz);
    REQUIRE(labels.bass_notes.size() == 48);
    for (size_t i = 0; i < labels.bass_notes.size(); ++i) {
        CAPTURE(i);
        const std::int64_t slot = static_cast<std::int64_t>(i / 3 * 4 + i % 3 + 1);
        const std::int64_t onset = lead_in + slot * kSlotSamples160;
        const BassNoteLabel &note = labels.bass_notes[i];
        CHECK(note.onset_s == static_cast<double>(onset) / kSampleRateHz);
        CHECK(note.offset_s == static_cast<double>(onset + 2700) / kSampleRateHz); // 0.6 slot
        CHECK(note.midi == 29);
        CHECK_FALSE(note.slide_in);
    }
    REQUIRE(labels.regions.size() == 1);
    CHECK(labels.regions[0].complete);
    CHECK(labels.regions[0].end_s == static_cast<double>(out.samples.size()) / kSampleRateHz);
}

TEST_CASE("synth: the six patterns place notes on the documented slots") {
    const auto starts = [](Pattern pattern) {
        std::vector<double> slots;
        for (const BassEvent &e : make_pattern(pattern, 1, 29, 0.5)) {
            slots.push_back(e.start_slots);
        }
        return std::vector<double>(slots.begin(),
                                   slots.begin() + std::min<size_t>(3, slots.size()));
    };
    CHECK(make_pattern(Pattern::rolling, 1, 29, 0.5).size() == 12);
    CHECK(starts(Pattern::rolling) == std::vector<double>{1, 2, 3});
    CHECK(make_pattern(Pattern::offbeat_double, 1, 29, 0.5).size() == 8);
    CHECK(starts(Pattern::offbeat_double) == std::vector<double>{2, 3, 6});
    CHECK(make_pattern(Pattern::offbeat, 1, 29, 0.5).size() == 4);
    CHECK(starts(Pattern::offbeat) == std::vector<double>{2, 6, 10});

    const std::vector<BassEvent> gallop = make_pattern(Pattern::triplet_gallop, 1, 29, 0.5);
    REQUIRE(gallop.size() == 8);
    CHECK(gallop[0].start_slots == doctest::Approx(4.0 / 3.0));
    CHECK(gallop[1].start_slots == doctest::Approx(8.0 / 3.0));
    CHECK(gallop[0].length_slots == doctest::Approx(0.5 * 4.0 / 3.0));

    const std::vector<BassEvent> ghost = make_pattern(Pattern::rolling_ghost, 1, 29, 0.5);
    CHECK(ghost[0].velocity == 0.5);
    CHECK(ghost[1].velocity == 1.0);
    const std::vector<BassEvent> jump = make_pattern(Pattern::rolling_octave_jump, 1, 29, 0.5);
    CHECK(jump[0].midi == 29);
    CHECK(jump[2].midi == 41);
}

TEST_CASE("synth: event times do not drift at a BPM with a fractional slot length") {
    SynthParams params = bass_only();
    params.bpm = 145.0; // 4965.517... samples per slot.
    params.bars = 16;
    const SynthOutput out = render(params, make_pattern(Pattern::offbeat, 16, 29, 0.5));
    const double slot_samples = kSampleRateHz * 60.0 / 145.0 / 4.0;
    const BassNoteLabel &last = out.labels.bass_notes.back();
    const double ideal_onset_s = (24000.0 + (15 * 16 + 14) * slot_samples) / kSampleRateHz;
    CHECK(std::abs(last.onset_s - ideal_onset_s) <= 0.5 / kSampleRateHz);
}

TEST_CASE("synth: bass sounds inside every gate and is exactly silent in the rests") {
    SynthParams params = bass_only();
    const SynthOutput out = render(params, make_pattern(Pattern::offbeat, 4, 29, 0.5));
    const std::vector<BassNoteLabel> &notes = out.labels.bass_notes;
    const std::int64_t release = to_samples(params.bass_release_ms * 1e-3);
    for (size_t i = 0; i < notes.size(); ++i) {
        CAPTURE(i);
        const std::int64_t onset = to_samples(notes[i].onset_s);
        const std::int64_t offset = to_samples(notes[i].offset_s);
        CHECK(rms(out.samples, onset + 96, offset) > 0.02);
        const std::int64_t rest_end = i + 1 < notes.size()
                                          ? to_samples(notes[i + 1].onset_s)
                                          : static_cast<std::int64_t>(out.samples.size());
        CHECK(max_abs(out.samples, offset + release, rest_end) == 0.0);
    }
    CHECK(max_abs(out.samples, 0, to_samples(notes[0].onset_s)) == 0.0);
}

TEST_CASE("synth: a held note sounds at the labelled pitch") {
    for (const int midi : {29, 33, 41}) {
        CAPTURE(midi);
        SynthParams params = bass_only();
        params.bass_decay_tau_ms = 5000.0;
        const SynthOutput out = render(params, {{0.0, 12.0, midi, 1.0, false}});
        const std::int64_t onset = to_samples(out.labels.bass_notes[0].onset_s);
        const double measured_hz =
            autocorrelation_pitch_hz(out.samples, onset + 12000, onset + 36000);
        CHECK(measured_hz == doctest::Approx(midi_hz(midi)).epsilon(0.005));
    }
}

TEST_CASE("synth: a long note sounds through every slot it spans, then stops") {
    SynthParams params = bass_only();
    params.bass_decay_tau_ms = 5000.0;
    const SynthOutput out = render(params, {{1.0, 3.0, 29, 1.0, false}});
    const BassNoteLabel &note = out.labels.bass_notes.at(0);
    const std::int64_t onset = to_samples(note.onset_s);
    const std::int64_t offset = to_samples(note.offset_s);
    CHECK(offset - onset == 3 * kSlotSamples160);
    for (int slot = 0; slot < 3; ++slot) {
        CAPTURE(slot);
        const std::int64_t start = onset + slot * kSlotSamples160;
        CHECK(rms(out.samples, start + 100, start + kSlotSamples160) > 0.02);
    }
    const std::int64_t release = to_samples(params.bass_release_ms * 1e-3);
    CHECK(max_abs(out.samples, offset + release, static_cast<std::int64_t>(out.samples.size())) ==
          0.0);
}

TEST_CASE("synth: a slide glides to the new pitch without re-attacking") {
    SynthParams params = bass_only();
    params.bass_decay_tau_ms = 5000.0;
    const SynthOutput out = render(params, {{0.0, 4.0, 29, 1.0, false}, {4.0, 8.0, 36, 1.0, true}});
    REQUIRE(out.labels.bass_notes.size() == 2);
    const BassNoteLabel &first = out.labels.bass_notes[0];
    const BassNoteLabel &second = out.labels.bass_notes[1];
    CHECK(first.offset_s == second.onset_s);
    CHECK(first.slide_out);
    CHECK(second.slide_in);

    const std::int64_t boundary = to_samples(second.onset_s);
    const double settled_hz =
        autocorrelation_pitch_hz(out.samples, boundary + 9600, boundary + 33600);
    CHECK(settled_hz == doctest::Approx(midi_hz(36)).epsilon(0.005));

    // A re-attack restarts the filter envelope at 2 kHz, so the 50 ms after the boundary would be
    // far brighter. The slide keeps the filter where the first note left it.
    const SynthOutput reattacked =
        render(params, {{0.0, 4.0, 29, 1.0, false}, {4.0, 8.0, 36, 1.0, false}});
    const auto brightness = [&](const std::vector<float> &x) {
        double sum = 0.0; // Energy of the first difference: weights high frequencies.
        for (std::int64_t i = boundary + 1; i < boundary + 2400; ++i) {
            const double d = static_cast<double>(x[i]) - x[i - 1];
            sum += d * d;
        }
        return sum;
    };
    CHECK(brightness(out.samples) < 0.5 * brightness(reattacked.samples));
}

TEST_CASE("synth: overlapping notes are monophonic, so the offset clips to the next onset") {
    const SynthOutput out =
        render(bass_only(), {{0.0, 2.5, 29, 1.0, false}, {1.0, 0.5, 31, 1.0, false}});
    REQUIRE(out.labels.bass_notes.size() == 2);
    CHECK(out.labels.bass_notes[0].offset_s == out.labels.bass_notes[1].onset_s);
    CHECK_FALSE(out.labels.bass_notes[0].slide_out);
}

TEST_CASE("synth: the kick sweeps from 170 Hz to 45 Hz, with an optional high click") {
    SynthParams params;
    params.kick_click = false;
    params.bass_level = 0.0;
    const SynthOutput clean = render(params, {});
    const std::int64_t kick = to_samples(clean.labels.kicks.at(0).t_s);
    // First 20 ms: the sweep falls from 170 Hz to about 133 Hz. From 150 ms on it holds 45 Hz.
    const double early_hz = zero_crossing_hz(clean.samples, kick, kick + 960);
    CHECK(early_hz > 133.0);
    CHECK(early_hz < 170.0);
    CHECK(zero_crossing_hz(clean.samples, kick + 7200, kick + 14400) ==
          doctest::Approx(45.0).epsilon(0.03));

    params.kick_click = true;
    const SynthOutput clicked = render(params, {});
    const auto high_band_energy = [&](const std::vector<float> &x) {
        double sum = 0.0; // First difference: a crude high-pass that favours the click band.
        for (std::int64_t i = kick + 1; i < kick + 480; ++i) {
            const double d = static_cast<double>(x[i]) - x[i - 1];
            sum += d * d;
        }
        return sum;
    };
    CHECK(high_band_energy(clicked.samples) > 10.0 * high_band_energy(clean.samples));
}

TEST_CASE("synth: sidechain ducking lowers the bass after each kick without ending notes") {
    SynthParams params = bass_only();
    params.bass_decay_tau_ms = 5000.0;
    const std::vector<BassEvent> held{{0.0, 16.0, 29, 1.0, false}};
    const SynthOutput dry = render(params, held);
    params.sidechain_depth = 0.8;
    const SynthOutput ducked = render(params, held);
    CHECK(ducked.labels.bass_notes == dry.labels.bass_notes);
    // Beat 2 starts 4 slots after the downbeat; compare 3 ms just after it.
    const std::int64_t beat = to_samples(dry.labels.beats.at(1).t_s);
    const double ratio = rms(ducked.samples, beat, beat + 144) / rms(dry.samples, beat, beat + 144);
    CHECK(ratio == doctest::Approx(1.0 - 0.8 * std::exp(-1.5 / 60.0)).epsilon(0.05));
    // 290 to 350 ms after the kick the 60 ms release has recovered to above 99%.
    const double recovered = rms(ducked.samples, beat + 14000, beat + 17000) /
                             rms(dry.samples, beat + 14000, beat + 17000);
    CHECK(recovered > 0.98);
}

TEST_CASE("synth: rendering is bit-identical for the same parameters and seed") {
    SynthParams params;
    params.hats = true;
    params.noise_level = 0.01;
    params.sidechain_depth = 0.5;
    const std::vector<BassEvent> events = make_pattern(Pattern::rolling_octave_jump, 4, 29, 0.7);
    const SynthOutput a = render(params, events);
    const SynthOutput b = render(params, events);
    REQUIRE(a.samples.size() == b.samples.size());
    CHECK(std::memcmp(a.samples.data(), b.samples.data(), a.samples.size() * sizeof(float)) == 0);
    CHECK(a.labels == b.labels);

    params.seed = 2;
    const SynthOutput c = render(params, events);
    CHECK(std::memcmp(a.samples.data(), c.samples.data(), a.samples.size() * sizeof(float)) != 0);
    CHECK(c.labels == a.labels);
}

TEST_CASE("synth: the full mix stays below full scale and its labels survive JSON") {
    SynthParams params;
    params.hats = true;
    params.noise_level = 0.01;
    const SynthOutput out = render(params, make_pattern(Pattern::rolling, 4, 29, 0.6));
    CHECK(max_abs(out.samples, 0, static_cast<std::int64_t>(out.samples.size())) < 1.0);
    const Result<std::string> json = to_labels_json(out.labels);
    REQUIRE(json.ok());
    const Result<Labels> parsed = parse_labels_json(json.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == out.labels);
}
