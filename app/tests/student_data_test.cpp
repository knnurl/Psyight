// M4 student data: causal features, teacher targets and the fingerprint.

#include "analysis/student_data.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace psyight;

namespace {

constexpr int kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> tone(double hz, double seconds, double amplitude = 0.5) {
    std::vector<float> s(static_cast<size_t>(seconds * kRate));
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] = static_cast<float>(amplitude *
                                  std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    }
    return s;
}

// Centre frequency of the loudest band in [first, first + count) at frame k.
int loudest_band(const StudentFeatures &f, int k, int first, int count) {
    const float *row = f.values.data() + static_cast<size_t>(k) * static_cast<size_t>(f.dims);
    return static_cast<int>(std::max_element(row + first, row + first + count) - (row + first));
}

double band_hz(int band, int count, double min_hz, double max_hz) {
    return min_hz * std::pow(max_hz / min_hz, static_cast<double>(band + 1) / (count + 1));
}

} // namespace

TEST_CASE("student features: causal, sized, and tones land in the right bands") {
    const StudentFeatureParams p;
    std::vector<float> audio = tone(110.0, 2.0);
    const std::vector<float> high = tone(5000.0, 2.0, 0.3);
    for (size_t i = 0; i < audio.size(); ++i) {
        audio[i] += high[i];
    }
    const StudentFeatures f = student_features(audio, kRate, p).value();
    CHECK(f.frames == static_cast<int>(audio.size()) / p.hop_samples);
    CHECK(f.dims == p.short_bands + p.long_bands);
    for (const float v : f.values) {
        CHECK(v >= -1.0f);
        CHECK(v <= 1.0f);
    }
    const int k = 200; // Well inside the tones.
    const double low = band_hz(loudest_band(f, k, p.short_bands, p.long_bands), p.long_bands,
                               p.long_min_hz, p.long_max_hz);
    CHECK(low == doctest::Approx(110.0).epsilon(0.05));
    // Among the short bands above 1 kHz, the 5 kHz tone's band is loudest.
    int first_high = 0;
    while (band_hz(first_high, p.short_bands, p.short_min_hz, p.short_max_hz) < 1000.0) {
        ++first_high;
    }
    const double top =
        band_hz(first_high + loudest_band(f, k, first_high, p.short_bands - first_high),
                p.short_bands, p.short_min_hz, p.short_max_hz);
    CHECK(top == doctest::Approx(5000.0).epsilon(0.05));

    // Changing the audio after frame k's sample changes no frame up to k.
    std::vector<float> changed = audio;
    for (size_t i = static_cast<size_t>(k) * static_cast<size_t>(p.hop_samples); i < changed.size();
         ++i) {
        changed[i] = 0.9f * std::sin(static_cast<float>(i) * 0.37f);
    }
    const StudentFeatures g = student_features(changed, kRate, p).value();
    const size_t up_to = static_cast<size_t>(k + 1) * static_cast<size_t>(f.dims);
    CHECK(std::equal(f.values.begin(), f.values.begin() + static_cast<std::ptrdiff_t>(up_to),
                     g.values.begin()));
    CHECK_FALSE(std::equal(f.values.begin(), f.values.end(), g.values.begin()));
}

TEST_CASE("teacher targets: onsets, gate, pitch and residual on the frame clock") {
    TrackAnalysis a;
    a.sample_rate_hz = kRate;
    a.bass.kick_times_s = {1.0};
    a.residual.hats = {{1.25, 1.0}};
    BassNote n;
    n.onset_s = 1.1;
    n.offset_s = 1.2;
    n.pitch_midi = 31;
    a.bass.notes = {n};
    a.mix_features.sample_rate_hz = kRate;
    a.mix_features.hop_samples = 256;
    a.residual.energy_db.assign(500, -90.0f);
    a.residual.energy_db[400] = 0.0f;
    const double frame_s = 256.0 / kRate;
    const TeacherTargets t = teacher_targets(a, 500, 256);
    const auto at = [frame_s](double s) { return static_cast<size_t>(std::lround(s / frame_s)); };
    CHECK(t.kick_onset[at(1.0)] == 1.0f);
    CHECK(t.kick_onset[at(1.0) + 1] == 0.5f);
    CHECK(t.kick_onset[at(1.0) + 2] == 0.0f);
    CHECK(t.hat_onset[at(1.25)] == 1.0f);
    CHECK(t.bass_onset[at(1.1)] == 1.0f);
    CHECK(t.bass_gate[at(1.15)] == 1.0f);
    CHECK(t.bass_pitch[at(1.15)] == 31);
    CHECK(t.bass_gate[at(1.3)] == 0.0f);
    CHECK(t.bass_pitch[at(1.3)] == -1);
    CHECK(t.residual_activity[400] == 1.0f);
    CHECK(t.residual_activity[10] == 0.0f);
}

TEST_CASE("fingerprint: 12 bands a second, equal for equal audio") {
    const std::vector<float> a = tone(220.0, 3.0);
    const std::vector<float> b = tone(880.0, 3.0);
    const StudentFeatures fa = student_features(a, kRate).value();
    const std::vector<float> pa = fingerprint(fa, kRate);
    CHECK(pa.size() == 3 * kFingerprintBands);
    CHECK(pa == fingerprint(student_features(a, kRate).value(), kRate));
    CHECK(pa != fingerprint(student_features(b, kRate).value(), kRate));
}
