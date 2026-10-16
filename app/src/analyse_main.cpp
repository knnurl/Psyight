// psyight_analyse: the file-mode analysis of a track without labels, as a report.
//
//   psyight_analyse [--kicks] [--tempo] <audio> [more audio...]
//
// Runs the front end, the beat tracker and bass tracker v0 on the automatic grid, and prints
// tempo, grid, downbeat and phrase (with confidences), kick coverage of the beats, and bass note
// statistics: rate, position in the bar, pitch and confidence. When "<audio>.labels.json" exists,
// it also compares the automatic grid with the labelled one. For sanity checks on unlabelled
// tracks; accuracy needs labels (psyight_eval). --kicks prints the kick detector's features at
// the grid's beats against its offbeats (in psytrance kicks sit on beats, hats on offbeats), to
// check its thresholds without kick labels. --tempo prints the tempo of each minute alone, which
// shows tempo changes (DJ mixes) and unstable estimates.

#include "analysis/bass_tracker.h"
#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "analysis/series.h"
#include "audio/audio_file.h"
#include "core/paths.h"
#include "core/pitch.h"
#include "io/labels.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <map>
#include <string>
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

void print_quantiles(const char *name, std::vector<float> values) {
    std::printf("    %-22s p10 %7.2f  p25 %7.2f  median %7.2f  p75 %7.2f  p90 %7.2f\n", name,
                quantile(values, 0.10), quantile(values, 0.25), quantile(values, 0.5),
                quantile(values, 0.75), quantile(values, 0.90));
}

// The kick detector's features (bass_tracker.cpp) at the strongest click-flux peak within
// 20 ms of each grid beat, and of each offbeat.
void print_kick_features(const FrontEndResult &f, const Grid &grid) {
    const double frame_s = static_cast<double>(f.hop_samples) / f.sample_rate_hz;
    const std::vector<float> &click = f.bands[3].flux;
    const std::vector<float> &lowmid = f.bands[2].flux;
    const double click_p99 = quantile(click, 0.99);
    std::vector<float> low(f.frames);
    for (int i = 0; i < f.frames; ++i) {
        low[i] = static_cast<float>(std::pow(10.0, f.bands[0].energy_db[i] / 10.0) +
                                    std::pow(10.0, f.bands[1].energy_db[i] / 10.0));
    }
    low = box_mean(low, 2);
    const double beat_s = beat_s_of(grid);
    const double first_s = samples_to_seconds(grid.first_downbeat, grid.sample_rate_hz);
    for (const double phase : {0.0, 0.5}) {
        std::vector<float> strength, ratio, rise;
        for (double t = first_s + phase * beat_s; t < f.frames * frame_s; t += beat_s) {
            const int centre = static_cast<int>(std::lround(t / frame_s));
            int peak = -1;
            for (int i = std::max(1, centre - 4); i <= std::min(f.frames - 2, centre + 4); ++i) {
                peak = peak < 0 || click[i] > click[peak] ? i : peak;
            }
            if (peak < 4 || peak + 4 >= f.frames) {
                continue;
            }
            float before = low[peak];
            float after = low[peak];
            for (int k = 1; k <= 4; ++k) {
                before = std::min(before, low[peak - k]);
                after = std::max(after, low[peak + k]);
            }
            strength.push_back(static_cast<float>(click[peak] / click_p99));
            ratio.push_back(click[peak] / std::max(lowmid[peak], 1e-6f));
            rise.push_back(
                static_cast<float>(10.0 * std::log10((after + 1e-12) / (before + 1e-12))));
        }
        std::printf("  kick features at %s (%zu):\n", phase == 0.0 ? "beats" : "offbeats",
                    strength.size());
        print_quantiles("click / p99 (>= 0.4)", strength);
        print_quantiles("click / low-mid (>= 0.2)", ratio);
        print_quantiles("low rise dB (>= 6)", rise);
    }
}

// The frames [begin, end) of the features as a track of their own.
FrontEndResult slice(const FrontEndResult &f, int begin, int end) {
    FrontEndResult part;
    part.sample_rate_hz = f.sample_rate_hz;
    part.hop_samples = f.hop_samples;
    part.frames = end - begin;
    const auto rows = [&](const LogSpectrogram &image) {
        LogSpectrogram cut = image;
        cut.frames = end - begin;
        cut.db.assign(image.db.begin() + static_cast<std::ptrdiff_t>(begin) * image.rows,
                      image.db.begin() + static_cast<std::ptrdiff_t>(end) * image.rows);
        return cut;
    };
    part.spectrogram = rows(f.spectrogram);
    part.pitch_salience = rows(f.pitch_salience);
    const auto range = [&](const std::vector<float> &x) {
        return std::vector<float>(x.begin() + begin, x.begin() + end);
    };
    for (const BandTrack &band : f.bands) {
        part.bands.push_back(
            {band.band, range(band.energy_db), range(band.envelope), range(band.flux)});
    }
    part.flux_total = range(f.flux_total);
    return part;
}

void print_local_tempo(const FrontEndResult &f) {
    const double frame_s = static_cast<double>(f.hop_samples) / f.sample_rate_hz;
    const int minute = static_cast<int>(std::lround(60.0 / frame_s));
    std::printf("  tempo per minute:");
    for (int begin = 0; begin + minute / 2 <= f.frames; begin += minute) {
        const Result<BeatTrackResult> beats =
            track_beats(slice(f, begin, std::min(f.frames, begin + minute)), {});
        if (beats && beats.value().bpm > 0.0) {
            std::printf(" %.1f (%.2f)", beats.value().bpm, beats.value().tempo_confidence);
        } else {
            std::printf(" -");
        }
    }
    std::printf("\n");
}

int analyse(const std::filesystem::path &audio_path, bool kick_features, bool local_tempo) {
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
    std::printf("  tempo %.3f BPM (confidence %.2f); first downbeat %.3f s (confidence %.2f, "
                "%d kick breaks); phrases of 8 bars start %d bars later (confidence %.2f, %d "
                "breaks)\n",
                b.bpm, b.tempo_confidence, first_downbeat_s, b.downbeat_confidence,
                b.downbeat_breaks, b.phrase_bar_offset, b.phrase_confidence, b.phrase_breaks);

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

    if (local_tempo) {
        print_local_tempo(features.value());
    }
    if (kick_features) {
        print_kick_features(features.value(), grid);
    }
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
    bool kick_features = false;
    bool local_tempo = false;
    std::vector<std::wstring> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--kicks") == 0) {
            kick_features = true;
        } else if (std::wcscmp(argv[i], L"--tempo") == 0) {
            local_tempo = true;
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty()) {
        std::fprintf(stderr,
                     "usage: psyight_analyse [--kicks] [--tempo] <audio> [more audio...]\n");
        return 2;
    }
    int status = 0;
    for (const std::wstring &path : paths) {
        status = std::max(status, analyse(path, kick_features, local_tempo));
    }
    return status;
}
