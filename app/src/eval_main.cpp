// psyight_eval: scores the bass tracker against a labels file, from the command line.
//
//   psyight_eval <audio> [labels.json] [--detail] [--stems] [--notes <estimates.json>]
//
// Decodes the audio, runs the file-mode analysis (analysis/track_analysis.h) with bass tracker v0
// on the labels' grid, and prints the SPEC 6.10 metrics for each complete region and in total.
// --stems analyses the offline Demucs stems instead of the mix (kicks from drums, notes from bass).
// The labels default to
// "<audio>.labels.json". --detail adds distributions that separate tracker bias from label or
// grid offsets: onset errors of notes matched within 50 ms, and detected kicks against the grid's
// beats. --notes scores the bass notes of another labels-format file (for example the M4
// student's, from tools/python/m4_eval.py) with the same scoring, instead of running the
// analysis. For private reference tracks: run locally, never in CI.

#include "analysis/bass_tracker.h"
#include "analysis/scoring.h"
#include "analysis/track_analysis.h"
#include "core/paths.h"
#include "io/labels.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

using namespace psyight;

namespace {

void print_score(const char *name, const NoteScore &s) {
    std::printf("%s\n", name);
    std::printf("  notes: %d labelled, %d detected, %d matched (onset +-20 ms)\n",
                s.reference_count, s.estimate_count, s.matched_count);
    std::printf("  onset: precision %.3f, recall %.3f, F %.3f\n", s.onset_precision, s.onset_recall,
                s.onset_f);
    std::printf("  offset error: median %+.1f ms, |median| %.1f ms, |p90| %.1f ms, hit rate %.3f\n",
                1e3 * s.offset_error_median_s, 1e3 * s.offset_error_abs_median_s,
                1e3 * s.offset_error_abs_p90_s, s.offset_hit_rate);
    std::printf("  pitch: accuracy %.3f (within 50 cents), octave errors %d (%.3f)\n",
                s.pitch_accuracy, s.octave_error_count, s.octave_error_rate);
    std::printf("  gate ratio MAE: %.3f\n", s.gate_ratio_mae);
}

// Signed percentiles, in ms.
void print_distribution(const char *name, const std::vector<double> &values_s) {
    if (values_s.empty()) {
        std::printf("  %s: none\n", name);
        return;
    }
    std::printf("  %s (n %zu): p5 %+.1f, p25 %+.1f, median %+.1f, p75 %+.1f, p95 %+.1f ms\n", name,
                values_s.size(), 1e3 * percentile(values_s, 0.05), 1e3 * percentile(values_s, 0.25),
                1e3 * percentile(values_s, 0.5), 1e3 * percentile(values_s, 0.75),
                1e3 * percentile(values_s, 0.95));
}

void print_detail(const Labels &labels, const BassTrackResult &tracked, const Grid &grid,
                  const LabelRegion &region) {
    std::vector<BassNoteLabel> references;
    for (const BassNoteLabel &note : labels.bass_notes) {
        if (note.onset_s >= region.start_s && note.onset_s < region.end_s) {
            references.push_back(note);
        }
    }
    const std::vector<BassNoteLabel> estimates = to_labels(tracked.notes);
    std::vector<double> onset_errors_s;
    for (const NoteMatch &m : match_note_onsets(references, estimates, 0.050)) {
        onset_errors_s.push_back(estimates[m.estimate_index].onset_s -
                                 references[m.reference_index].onset_s);
    }
    print_distribution("onset error, detected minus labelled, matched within 50 ms",
                       onset_errors_s);
    const double beat_s = kSlotsPerBeat * grid.slot_length_samples() / grid.sample_rate_hz;
    std::vector<double> kick_errors_s;
    for (const double t_s : tracked.kick_times_s) {
        if (t_s >= region.start_s && t_s < region.end_s) {
            const double beats =
                grid.slot_position(seconds_to_samples(t_s, grid.sample_rate_hz)) / kSlotsPerBeat;
            kick_errors_s.push_back((beats - std::round(beats)) * beat_s);
        }
    }
    print_distribution("detected kick minus nearest grid beat", kick_errors_s);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    std::vector<std::wstring> paths;
    bool detail = false;
    std::filesystem::path notes_path;
    TrackAnalysisOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--notes") == 0 && i + 1 < argc) {
            notes_path = argv[++i];
        } else if (std::wcscmp(argv[i], L"--detail") == 0) {
            detail = true;
        } else if (std::wcscmp(argv[i], L"--stems") == 0) {
            options.source = AnalysisSource::stems;
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty() || paths.size() > 2) {
        std::fprintf(stderr, "usage: psyight_eval <audio> [labels.json] [--detail] [--stems] "
                             "[--notes <estimates.json>]\n");
        return 2;
    }
    const std::filesystem::path audio_path = paths[0];
    const std::filesystem::path labels_path =
        paths.size() == 2 ? std::filesystem::path(paths[1]) : labels_path_for(audio_path);

    const Result<Labels> labels = load_labels(labels_path);
    if (!labels) {
        std::fprintf(stderr, "%s\n", labels.error().message.c_str());
        return 1;
    }
    const LabelGrid &label_grid = labels.value().grid;
    const int sample_rate_hz = labels.value().sample_rate;
    const Grid grid{label_grid.bpm, seconds_to_samples(label_grid.first_downbeat_s, sample_rate_hz),
                    sample_rate_hz};
    options.grid = grid;
    if (!notes_path.empty()) {
        const Result<Labels> estimates = load_labels(notes_path);
        if (!estimates) {
            std::fprintf(stderr, "%s\n", estimates.error().message.c_str());
            return 1;
        }
        const TrackScore external = score_track(labels.value(), estimates.value().bass_notes, {});
        std::printf("%s: %zu notes from %s\n", path_to_utf8(audio_path.filename()).c_str(),
                    estimates.value().bass_notes.size(),
                    path_to_utf8(notes_path.filename()).c_str());
        for (const RegionScore &region : external.regions) {
            char name[160];
            std::snprintf(name, sizeof(name), "Region %.3f to %.3f s", region.region.start_s,
                          region.region.end_s);
            print_score(name, region.score);
        }
        return 0;
    }
    const auto start = std::chrono::steady_clock::now();
    const Result<TrackAnalysis> analysis = analyse_track(audio_path, options);
    if (!analysis) {
        std::fprintf(stderr, "%s\n", analysis.error().message.c_str());
        return 1;
    }
    const BassTrackResult *const tracked = &analysis.value().bass;
    const double analysis_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::printf("%s (%s): %.1f s of audio analysed in %.1f s; grid %.2f BPM, first downbeat "
                "%.3f s\n",
                path_to_utf8(audio_path.filename()).c_str(),
                options.source == AnalysisSource::stems ? "Demucs stems, aligned" : "mix",
                analysis.value().duration_s, analysis_s, label_grid.bpm,
                label_grid.first_downbeat_s);
    if (options.source == AnalysisSource::stems) {
        const double lag_ms = 1e3 * analysis.value().stems_lag_s;
        std::printf("  stems ran %.1f ms %s the mix; aligned before analysis\n", std::abs(lag_ms),
                    lag_ms < 0.0 ? "ahead of" : "behind");
    }
    const TrackScore score = score_track(labels.value(), to_labels(tracked->notes), {});
    if (score.regions.empty()) {
        std::printf("No complete regions in %s: nothing to score.\n",
                    path_to_utf8(labels_path.filename()).c_str());
        return 0;
    }
    const double beat_s = 60.0 / label_grid.bpm;
    for (const RegionScore &region : score.regions) {
        int kicks = 0;
        for (const double t_s : tracked->kick_times_s) {
            kicks += t_s >= region.region.start_s && t_s < region.region.end_s;
        }
        char name[160];
        std::snprintf(name, sizeof(name), "Region %.3f to %.3f s (%d kicks detected, %.0f beats)",
                      region.region.start_s, region.region.end_s, kicks,
                      (region.region.end_s - region.region.start_s) / beat_s);
        print_score(name, region.score);
        if (detail) {
            print_detail(labels.value(), *tracked, grid, region.region);
        }
    }
    if (score.regions.size() > 1) {
        print_score("All complete regions", score.total);
    }
    return 0;
}
