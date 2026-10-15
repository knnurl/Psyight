#include "analysis/filterbank.h"
#include "analysis/front_end.h"
#include "analysis/stft.h"
#include "synth_generator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double frequency_hz, double amplitude, int samples) {
    std::vector<float> x(samples);
    for (int i = 0; i < samples; ++i) {
        x[i] =
            static_cast<float>(amplitude * std::sin(2.0 * kPi * frequency_hz * i / kSampleRateHz));
    }
    return x;
}

double rms(const std::vector<float> &x, size_t begin, size_t end) {
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i) {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(sum / static_cast<double>(end - begin));
}

int frame_of(double t_s, int hop) {
    return static_cast<int>(std::lround(t_s * kSampleRateHz / hop));
}

} // namespace

TEST_CASE("stft: a sine at a bin centre reads its amplitude; Hann neighbours read half") {
    Stft stft;
    REQUIRE(stft.prepare(kSampleRateHz, 4096).ok());
    const double bin_hz = stft.bin_hz(1);
    const std::vector<float> x = sine(85 * bin_hz, 0.5, 16384);
    std::vector<float> magnitudes(stft.bins());
    stft.process(x, 8192, magnitudes);
    CHECK(magnitudes[85] == doctest::Approx(0.5).epsilon(0.001));
    CHECK(magnitudes[84] == doctest::Approx(0.25).epsilon(0.01));
    CHECK(magnitudes[86] == doctest::Approx(0.25).epsilon(0.01));
    CHECK(magnitudes[95] < 1e-3);
    CHECK_FALSE(stft.prepare(kSampleRateHz, 1000).ok());
}

TEST_CASE("stft: frames are centred and zero-padded outside the signal") {
    Stft stft;
    REQUIRE(stft.prepare(kSampleRateHz, 1024).ok());
    std::vector<float> impulse(4000, 0.0f);
    impulse[1000] = 1.0f;
    std::vector<float> magnitudes(stft.bins());
    stft.process(impulse, 1000, magnitudes); // Impulse at the window peak: flat spectrum.
    const float flat = magnitudes[1];
    for (int bin = 1; bin < stft.bins() - 1; ++bin) {
        CHECK(magnitudes[bin] == doctest::Approx(flat).epsilon(1e-4));
    }
    stft.process(impulse, 1000 + 600, magnitudes); // Impulse outside this window.
    CHECK(*std::max_element(magnitudes.begin(), magnitudes.end()) == 0.0f);
    stft.process(impulse, 0, magnitudes); // Half the window lies before the signal start.
    CHECK(*std::max_element(magnitudes.begin(), magnitudes.end()) == 0.0f);
}

TEST_CASE("band filter: unity gain at the centre, 12 dB per octave cascade skirts") {
    // Click band 2.5 to 6 kHz: centre 3873 Hz, Q 1.107. At 1 kHz one stage gives 0.243, the
    // cascade 0.059 (-24.6 dB).
    BandFilter filter;
    filter.prepare(kSampleRateHz, 2500.0, 6000.0);
    const auto gain = [&](double frequency_hz) {
        filter.reset();
        const std::vector<float> in = sine(frequency_hz, 1.0, kSampleRateHz);
        std::vector<float> out(in.size());
        filter.process(in, out);
        return rms(out, 24000, 48000) / rms(in, 24000, 48000);
    };
    CHECK(gain(std::sqrt(2500.0 * 6000.0)) == doctest::Approx(1.0).epsilon(0.01));
    CHECK(20.0 * std::log10(gain(1000.0)) == doctest::Approx(-24.6).epsilon(0.03));
    CHECK(gain(12000.0) < gain(6000.0));
}

TEST_CASE("envelope follower: attack and release reach 1 - 1/e and 1/e in their times") {
    EnvelopeFollower follower;
    follower.prepare(kSampleRateHz, 2.0, 20.0);
    double level = 0.0;
    for (int i = 0; i < 96; ++i) { // 2 ms of a unit step.
        level = follower.process(1.0);
    }
    CHECK(level == doctest::Approx(1.0 - std::exp(-1.0)).epsilon(0.01));
    for (int i = 0; i < 4800; ++i) { // Settle.
        level = follower.process(1.0);
    }
    for (int i = 0; i < 960; ++i) { // 20 ms of silence.
        level = follower.process(0.0);
    }
    CHECK(level == doctest::Approx(std::exp(-1.0)).epsilon(0.01));
}

TEST_CASE("front end: the spectrogram peak below 100 Hz sits at a held note's fundamental") {
    synth::SynthParams params;
    params.kick = false;
    params.bass_decay_tau_ms = 5000.0;
    params.filter_end_hz = 120.0; // Dark, so the fundamental dominates.
    const synth::SynthOutput out = synth::render(params, {{0.0, 32.0, 33, 1.0, false}}); // 55 Hz
    const Result<FrontEndResult> result = analyse_front_end(out.samples, kSampleRateHz, {});
    REQUIRE(result.ok());
    const LogSpectrogram &spec = result.value().spectrogram;
    const int frame = frame_of(out.labels.bass_notes[0].onset_s + 1.0, 256);
    int best_row = 0;
    for (int r = 0; r < spec.rows && spec.row_hz[r] < 100.0; ++r) {
        if (spec.db[frame * spec.rows + r] > spec.db[frame * spec.rows + best_row]) {
            best_row = r;
        }
    }
    // Rows are 3% apart but a 4096-sample FFT has 11.7 Hz bins, so the displayed peak is only
    // accurate to half a bin (SPEC 6.1: the low end needs longer windows or CQT for display).
    CHECK(std::abs(spec.row_hz[best_row] - 55.0) <= 0.5 * kSampleRateHz / 4096.0);
}

TEST_CASE("front end: frame count, bass-band contrast and kick flux timing on synthetic audio") {
    synth::SynthParams bass_params;
    bass_params.kick = false;
    const synth::SynthOutput bass =
        synth::render(bass_params, synth::make_pattern(synth::Pattern::offbeat, 4, 29, 0.5));
    const FrontEndParams params;
    const Result<FrontEndResult> bass_result =
        analyse_front_end(bass.samples, kSampleRateHz, params);
    REQUIRE(bass_result.ok());
    const FrontEndResult &r = bass_result.value();
    CHECK(r.frames == static_cast<int>((bass.samples.size() + 255) / 256));
    REQUIRE(r.bands.size() == 5);
    CHECK(r.bands[1].band.name == "bass");
    // Mid-note against mid-rest (offbeat notes: gate 0.5 slot, then 3.5 slots of rest).
    const BassNoteLabel &note = bass.labels.bass_notes[4];
    const double slot_s = 0.09375;
    const int in_note = frame_of((note.onset_s + note.offset_s) / 2.0, 256);
    const int in_rest = frame_of(note.offset_s + 2.0 * slot_s, 256);
    CHECK(r.bands[1].energy_db[in_note] - r.bands[1].energy_db[in_rest] > 40.0);
    CHECK(r.bands[1].envelope[in_note] > 10.0 * r.bands[1].envelope[in_rest]);

    synth::SynthParams kick_params;
    kick_params.bass_level = 0.0;
    const synth::SynthOutput kicks = synth::render(kick_params, {});
    const Result<FrontEndResult> kick_result =
        analyse_front_end(kicks.samples, kSampleRateHz, params);
    REQUIRE(kick_result.ok());
    const std::vector<float> &flux = kick_result.value().flux_total;
    for (const LabelEvent &kick : kicks.labels.kicks) {
        CAPTURE(kick.t_s);
        const int expected = frame_of(kick.t_s, 256);
        const int begin = std::max(0, expected - 8); // +-43 ms search window.
        const int end = std::min(static_cast<int>(flux.size()), expected + 9);
        const int peak = static_cast<int>(
            std::max_element(flux.begin() + begin, flux.begin() + end) - flux.begin());
        CHECK(std::abs(peak - expected) <= 2); // Within 10.7 ms.
    }
}

TEST_CASE("front end: identical input and parameters give bit-identical output") {
    synth::SynthParams params;
    params.hats = true;
    params.noise_level = 0.01;
    const synth::SynthOutput out =
        synth::render(params, synth::make_pattern(synth::Pattern::rolling, 2, 29, 0.6));
    const Result<FrontEndResult> a = analyse_front_end(out.samples, kSampleRateHz, {});
    const Result<FrontEndResult> b = analyse_front_end(out.samples, kSampleRateHz, {});
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    const auto same = [](const std::vector<float> &x, const std::vector<float> &y) {
        return x.size() == y.size() && std::memcmp(x.data(), y.data(), x.size() * 4) == 0;
    };
    CHECK(same(a.value().spectrogram.db, b.value().spectrogram.db));
    CHECK(same(a.value().flux_total, b.value().flux_total));
    for (size_t i = 0; i < a.value().bands.size(); ++i) {
        CHECK(same(a.value().bands[i].energy_db, b.value().bands[i].energy_db));
        CHECK(same(a.value().bands[i].envelope, b.value().bands[i].envelope));
        CHECK(same(a.value().bands[i].flux, b.value().bands[i].flux));
    }
}

TEST_CASE("front end: invalid parameters are rejected") {
    const std::vector<float> x(4800, 0.0f);
    FrontEndParams bad_band;
    bad_band.bands = {{"bad", 500.0, 100.0}};
    CHECK_FALSE(analyse_front_end(x, kSampleRateHz, bad_band).ok());
    FrontEndParams bad_window;
    bad_window.flux_window_samples = 1000;
    CHECK_FALSE(analyse_front_end(x, kSampleRateHz, bad_window).ok());
    FrontEndParams bad_range;
    bad_range.spectrogram_max_hz = 30000.0;
    CHECK_FALSE(analyse_front_end(x, kSampleRateHz, bad_range).ok());
}
