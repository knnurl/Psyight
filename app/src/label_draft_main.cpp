// psyight_label_draft: drafts a labels file from the file-mode analysis, for a person to check.
//
//   psyight_label_draft <audio> [more audio...]
//
// Runs the analysis (from the Demucs stems when they exist: the best detector here) and writes
// "<audio>.labels.json" with the detected grid, the detected sections, and one region of whole
// bars covering at least 60 s where the most kicks were detected, filled
// with the detected bass notes and kicks. The region is saved NOT complete: scoring ignores it
// until someone has listened, corrected the notes in the bass roll and ticked "Complete".
// Detector-made labels cannot measure that detector; checked ones can (SPEC 6.10). Never
// overwrites an existing labels file.

#include "analysis/bass_tracker.h"
#include "analysis/track_analysis.h"
#include "core/paths.h"
#include "io/labels.h"
#include "io/stems.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr double kRegionS = 60.0;

int draft(const std::filesystem::path &audio) {
    const std::string name = path_to_utf8(audio.filename());
    const std::filesystem::path out = labels_path_for(audio);
    if (std::filesystem::exists(out)) {
        std::printf("%s: %s exists; not overwritten\n", name.c_str(),
                    path_to_utf8(out.filename()).c_str());
        return 0;
    }
    TrackAnalysisOptions options;
    options.source =
        analysis_stems_exist(stem_paths_for(audio)) ? AnalysisSource::stems : AnalysisSource::mix;
    const Result<TrackAnalysis> analysed = analyse_track(audio, options);
    if (!analysed) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), analysed.error().message.c_str());
        return 1;
    }
    const TrackAnalysis &a = analysed.value();
    const double beat_s = 60.0 / a.grid.bpm;
    const double bar_s = 4.0 * beat_s;
    const double downbeat_s = samples_to_seconds(a.grid.first_downbeat, a.sample_rate_hz);

    // The bar-aligned window of whole bars (at least 60 s) holding the most detected kicks: the
    // solid groove, whatever the section labelling (Hagios has long beatless stretches).
    const int bars = static_cast<int>(std::ceil(kRegionS / bar_s));
    double start = downbeat_s;
    size_t most = 0;
    for (double t = downbeat_s; t + bars * bar_s <= a.duration_s; t += bar_s) {
        size_t kicks = 0;
        for (const double k : a.bass.kick_times_s) {
            kicks += k >= t && k < t + bars * bar_s;
        }
        if (kicks > most) {
            most = kicks;
            start = t;
        }
    }
    if (most == 0) {
        std::fprintf(stderr, "%s: no kicks detected; no region drafted\n", name.c_str());
        return 1;
    }
    const double end = start + bars * bar_s;

    Labels labels;
    labels.audio = name;
    labels.sample_rate = a.sample_rate_hz;
    labels.grid.bpm = a.grid.bpm;
    labels.grid.first_downbeat_s = downbeat_s;
    labels.regions.push_back({start, end, false});
    for (const BassNoteLabel &n : to_labels(a.bass.notes)) {
        if (n.onset_s >= start && n.onset_s < end) {
            labels.bass_notes.push_back(n);
        }
    }
    for (const double k : a.bass.kick_times_s) {
        if (k >= start && k < end) {
            labels.kicks.push_back({k});
        }
    }
    for (const DetectedSection &s : a.sections) {
        labels.sections.push_back({s.start_s, s.label});
    }
    if (Result<void> saved = save_labels(out, labels); !saved) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), saved.error().message.c_str());
        return 1;
    }
    std::printf("%s (%s): %.3f BPM, first downbeat %.3f s; region %.3f to %.3f s (%d bars, the "
                "most kicks): %zu notes, %zu kicks; %zu sections. Not complete: check it in the "
                "bass roll.\n",
                name.c_str(), options.source == AnalysisSource::stems ? "stems" : "mix", a.grid.bpm,
                downbeat_s, start, end, bars, labels.bass_notes.size(), labels.kicks.size(),
                labels.sections.size());
    return 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: psyight_label_draft <audio> [more audio...]\n");
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        status = std::max(status, draft(argv[i]));
    }
    return status;
}
