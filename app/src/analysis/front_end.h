// File-mode analysis front end (SPEC sections 6.1 and 6.2): log-frequency spectrogram for display,
// filterbank band energies and envelopes, and spectral flux, for a whole track.
//
// Frame k describes the audio around sample k * hop (centred windows; non-causal, file mode).
// Every output is a pure function of the input and parameters (deterministic).
#pragma once

#include "core/result.h"

#include <span>
#include <string>
#include <vector>

namespace psyight {

struct BandSpec {
    std::string name;
    double low_hz = 0.0;
    double high_hz = 0.0;
};

std::vector<BandSpec> default_bands(); // SPEC 6: sub, bass, low-mid, click, hats.

struct FrontEndParams {
    int hop_samples = 256;                 // 5.33 ms at 48 kHz.
    int flux_window_samples = 1024;        // Timing resolution.
    int spectrogram_window_samples = 4096; // Low-end resolution for display.
    // Pitch salience needs sharper harmonics: at 4096 an F1 read as A0 (whose 8th harmonic meets
    // F1's 5th inside the 23 Hz Hann main lobe); at 8192 (170 ms) all tested pitches are exact.
    int salience_window_samples = 8192;
    int spectrogram_rows = 192;
    double spectrogram_min_hz = 30.0;
    double spectrogram_max_hz = 16000.0;
    double envelope_attack_ms = 1.0;
    double envelope_release_ms = 20.0;
    std::vector<BandSpec> bands = default_bands();
};

struct LogSpectrogram {
    int rows = 0;
    int frames = 0;
    std::vector<double> row_hz; // Centre frequency of each row, log-spaced.
    std::vector<float> db;      // frames * rows, frame-major: db[frame * rows + row].
};

struct BandTrack {
    BandSpec band;
    std::vector<float> energy_db; // Mean square of the band signal over each hop, in dBFS.
    std::vector<float> envelope;  // Follower value at each frame's sample.
    std::vector<float> flux;      // Rectified log-magnitude increase over the band's bins.
};

// Bass pitch salience on semitone rows (SPEC 6.3 step 5, harmonic sum), from its own STFT
// (salience_window_samples) on the same frame clock. Row r is MIDI first_midi + r; its value is the
// geometric mean of the magnitudes at the note's first kSalienceHarmonics harmonics, in dB. The log
// domain makes a candidate an octave down (missing odd harmonics) or up (half the harmonics) score
// below the true pitch. Resolution: with the default 8192-sample window bins are 5.9 Hz,
// against 2.6 Hz per semitone at F1, so the pitch comes mostly from the upper harmonics (F1's 8th
// is 349 Hz, semitones 21 Hz apart).
inline constexpr int kSalienceFirstMidi = 21; // A0.
inline constexpr int kSalienceLastMidi = 64;  // E4.
inline constexpr int kSalienceHarmonics = 8;

struct FrontEndResult {
    int sample_rate_hz = 0;
    int hop_samples = 0;
    int frames = 0;
    LogSpectrogram spectrogram;
    LogSpectrogram pitch_salience; // Rows are semitones from kSalienceFirstMidi; values in dB.
    std::vector<BandTrack> bands;
    std::vector<float> flux_total; // Over all bins.
};

Result<FrontEndResult> analyse_front_end(std::span<const float> mono, int sample_rate_hz,
                                         const FrontEndParams &params);

} // namespace psyight
