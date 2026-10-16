// psyight_analyse: the file-mode analysis of a track without labels, as a report.
//
//   psyight_analyse <audio> [more audio...]
//
// Runs the front end, the beat tracker and bass tracker v0 on the automatic grid, and prints
// tempo, grid, downbeat and phrase (with confidences), kick coverage of the beats, and bass note
// statistics: rate, position in the bar, pitch and confidence. When "<audio>.labels.json" exists,
// it also compares the automatic grid with the labelled one. For sanity checks on unlabelled
// tracks; accuracy needs labels (psyight_eval).

#include "analysis/bass_tracker.h"
#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "audio/audio_file.h"
#include "core/paths.h"
#include "core/pitch.h"
#include "io/labels.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <vector>

using namespace psyight;

namespace {

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double beat_s_of(const Grid &grid) {
    return kSlotsPerBeat * grid.slot_length_samples() / grid.sample_rate_hz;
}

// Signed distance from t_s to the nearest beat of the grid.
double beat_offset_s(const Grid &grid, double t_s) {
    const double beats =
        grid.slot_position(seconds_to_samples(t_s, grid.sample_rate_hz)) / kSlotsPerBeat;
    return (beats - std::round(beats)) * beat_s_of(grid);
}

void compare_with_labels(const Labels &labels, const Grid &automatic, double duration_s) {
    const LabelGrid &l = labels.grid;
    const Grid labelled{l.bpm, seconds_to_samples(l.first_downbeat_s, automatic.sample_rate_hz),
                        automatic.sample_rate_hz};
    std::vector<double> offsets_s;
    const double beat_s = 60.0 / l.bpm;
    for (double t = l.first_downbeat_s; t < duration_s; t += beat_s) {
        if (t >= 0.0) {
            offsets_s.push_back(beat_offset_s(automatic, t));
        }
    }
    const double bars = (samples_to_seconds(automatic.first_downbeat, automatic.sample_rate_hz) -
                         l.first_downbeat_s) /
                        (4.0 * beat_s);
    const double beats_apart = (bars - std::floor(bars)) * 4.0;
    std::printf("  labelled grid: %.3f BPM, first downbeat %.3f s\n", l.bpm, l.first_downbeat_s);
    std::printf("  automatic minus labelled: %+.3f BPM; labelled beats sit %+.1f ms (median) from "
                "automatic beats; downbeats %.2f beats apart (0 or 4: same bar phase)\n",
                automatic.bpm - labelled.bpm, 1e3 * median(offsets_s), beats_apart);
}

int analyse(const std::filesystem::path &audio_path) {
    const Result<AudioBuffer> audio = decode_audio_file(audio_path);
    if (!audio) {
        std::fprintf(stderr, "%s\n", audio.error().message.c_str());
        return 1;
    }
    const std::vector<float> mono = mono_mix(audio.value());
    const int sample_rate_hz = audio.value().sample_rate_hz;
    const double duration_s = static_cast<double>(mono.size()) / sample_rate_hz;
    const auto start = std::chrono::steady_clock::now();
    const Result<FrontEndResult> features = analyse_front_end(mono, sample_rate_hz, {});
    if (!features) {
        std::fprintf(stderr, "%s\n", features.error().message.c_str());
        return 1;
    }
    const Result<BeatTrackResult> beats = track_beats(features.value(), {});
    if (!beats) {
        std::fprintf(stderr, "%s\n", beats.error().message.c_str());
        return 1;
    }
    const BeatTrackResult &b = beats.value();
    std::printf("%s: %.1f s\n", path_to_utf8(audio_path.filename()).c_str(), duration_s);
    if (b.bpm <= 0.0) {
        std::printf("  no tempo found\n");
        return 0;
    }
    const Result<BassTrackResult> bass = track_bass(mono, features.value(), b.grid, {});
    if (!bass) {
        std::fprintf(stderr, "%s\n", bass.error().message.c_str());
        return 1;
    }
    const double analysis_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const Grid &grid = b.grid;
    const double beat_s = beat_s_of(grid);
    const double first_downbeat_s = samples_to_seconds(grid.first_downbeat, sample_rate_hz);
    std::printf("  analysed in %.1f s\n", analysis_s);
    std::printf("  tempo %.3f BPM (confidence %.2f); first downbeat %.3f s (confidence %.2f); "
                "phrases of 8 bars start %d bars later (confidence %.2f)\n",
                b.bpm, b.tempo_confidence, first_downbeat_s, b.downbeat_confidence,
                b.phrase_bar_offset, b.phrase_confidence);

    // Kicks against the grid: coverage of beats, and timing.
    const std::vector<double> &kicks = bass.value().kick_times_s;
    std::vector<double> kick_offsets_s;
    for (const double t : kicks) {
        kick_offsets_s.push_back(beat_offset_s(grid, t));
    }
    const double grid_beats = std::floor((duration_s - first_downbeat_s) / beat_s);
    std::printf("  kicks: %zu on %.0f grid beats (%.0f%%); kick minus beat median %+.1f ms\n",
                kicks.size(), grid_beats, 100.0 * kicks.size() / std::max(1.0, grid_beats),
                1e3 * median(kick_offsets_s));

    // Bass notes.
    const std::vector<BassNote> &notes = bass.value().notes;
    std::array<int, kSlotsPerBar> slots{};
    std::map<int, int> pitches;
    std::vector<double> confidences;
    std::vector<double> gates;
    int low_confidence = 0;
    int slides = 0;
    int long_notes = 0;
    for (const BassNote &n : notes) {
        ++slots[n.slot_index];
        ++pitches[n.pitch_midi];
        confidences.push_back(n.confidence);
        gates.push_back(n.gate_ratio);
        low_confidence += n.confidence < 0.3;
        slides += n.slide_in;
        long_notes += n.is_long;
    }
    std::printf("  bass notes: %zu (%.1f per bar), confidence median %.2f, below 0.3: %.0f%%; "
                "gate ratio median %.2f; slides %d, long notes %d\n",
                notes.size(), notes.size() / std::max(1.0, grid_beats / 4.0), median(confidences),
                100.0 * low_confidence / std::max<size_t>(1, notes.size()), median(gates), slides,
                long_notes);
    std::printf("  notes per slot in bar:");
    for (const int count : slots) {
        std::printf(" %d", count);
    }
    std::printf("\n  pitches:");
    std::vector<std::pair<int, int>> ranked(pitches.begin(), pitches.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const auto &x, const auto &y) { return x.second > y.second; });
    for (size_t i = 0; i < std::min<size_t>(6, ranked.size()); ++i) {
        std::printf(" %s %.0f%%", midi_to_note_name(ranked[i].first).c_str(),
                    100.0 * ranked[i].second / notes.size());
    }
    std::printf("\n");

    const std::filesystem::path labels_path = labels_path_for(audio_path);
    if (std::filesystem::exists(labels_path)) {
        if (const Result<Labels> labels = load_labels(labels_path); labels) {
            compare_with_labels(labels.value(), grid, duration_s);
        }
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: psyight_analyse <audio> [more audio...]\n");
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        status = std::max(status, analyse(argv[i]));
    }
    return status;
}
