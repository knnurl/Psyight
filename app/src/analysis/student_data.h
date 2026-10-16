// Student model data (SPEC section 6.9, M4): the inputs the causal student sees, the targets the
// file-mode teacher (the stems analysis) provides, and a coarse fingerprint to find duplicates.
//
// Inputs: a causal two-resolution log spectrogram on the 5.33 ms hop. Frame k uses only samples
// before k * hop (each window ends at the frame's sample), so live mode can compute it as audio
// arrives and training and inference share this code. A short window (1024, 21 ms) for timing
// over 30 Hz to 16 kHz, a long one (4096, 85 ms) for bass pitch over 25 Hz to 1 kHz; log-spaced
// bands on the power spectrum (narrow ones interpolated between bins, wide ones summed under a
// triangle), in dB, scaled as (dB + 60) / 60 and clamped to -1..1. The long window's bins are
// 11.7 Hz apart, about two semitones at 100 Hz, so the bass pitch comes mostly from harmonics.
// Latency: none beyond the hop (the student may add frames of lookahead).
//
// Targets per frame: kick, hat and bass onsets (1 at the event's frame, 0.5 either side), bass
// gate (1 while a detected note sounds), bass pitch (the sounding note's MIDI number, or -1) and
// residual activity (the residual level, 0 to 1).
#pragma once

#include "analysis/track_analysis.h"
#include "core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace psyight {

struct StudentFeatureParams {
    int hop_samples = 256;
    int short_window = 1024;
    int short_bands = 96;
    double short_min_hz = 30.0;
    double short_max_hz = 16000.0;
    int long_window = 4096;
    int long_bands = 96;
    double long_min_hz = 25.0;
    double long_max_hz = 1000.0;
};

struct StudentFeatures {
    int frames = 0;
    int dims = 0;              // short_bands + long_bands.
    std::vector<float> values; // frames * dims, frame-major.
};

Result<StudentFeatures> student_features(std::span<const float> mono, int sample_rate_hz,
                                         const StudentFeatureParams &params = {});

struct TeacherTargets {
    int frames = 0;
    std::vector<float> kick_onset;
    std::vector<float> hat_onset;
    std::vector<float> bass_onset;
    std::vector<float> bass_gate;
    std::vector<std::int16_t> bass_pitch; // MIDI, -1 when no note sounds.
    std::vector<float> residual_activity;
};

TeacherTargets teacher_targets(const TrackAnalysis &analysis, int frames, int hop_samples);

// Coarse fingerprint: 12 log-spaced band energies (dB) per second of the mix.
inline constexpr int kFingerprintBands = 12;
std::vector<float> fingerprint(const StudentFeatures &features, int sample_rate_hz,
                               const StudentFeatureParams &params = {});

} // namespace psyight
