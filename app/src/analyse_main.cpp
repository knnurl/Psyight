// psyight_analyse: the file-mode analysis of a track without labels, as a report.
//
//   psyight_analyse [--kicks] [--tempo] [--bars] [--stems] <audio> [more audio...]
//   psyight_analyse --sections-report <audio or folder> [more...]
//   psyight_analyse --sections-eval <audio or folder> [more...]
//
// Runs the front end, the beat tracker and bass tracker v0 on the automatic grid, and prints
// tempo, grid, downbeat and phrase (with confidences), kick coverage of the beats, and bass note
// statistics: rate, position in the bar, pitch and confidence. When "<audio>.labels.json" exists,
// it also compares the automatic grid with the labelled one. For sanity checks on unlabelled
// tracks; accuracy needs labels (psyight_eval). --kicks prints the kick detector's features at
// the grid's beats against its offbeats (in psytrance kicks sit on beats, hats on offbeats), to
// check its thresholds without kick labels. --tempo prints the tempo of each minute alone, which
// shows tempo changes (DJ mixes) and unstable estimates. --stems analyses the offline Demucs stems
// (kicks and beats from drums, notes from bass) instead of the mix. --bars prints what the section
// rules see per bar (kick state, hats level, the residual's flatness and centroid) with the
// detected section, to check the breakdown, build and drop rules. --sections-report prints one
// line per track (folders are searched for WAV, FLAC and MP3), analysed as the app would (stems
// when they exist): sections per minute, median section length in bars, sections under 4 bars
// (builds aside), the share of boundaries within half a bar of the 8-bar phrase grid, and kick
// state flips per minute; then the medians over the tracks. It measures how fragmented the
// section structure is, the problem docs/PLAN_SECTIONS_EFFECTS.md part 1 addresses.
// --sections-eval scores the detection against the sections labelled in the labelling mode, for
// every track marked done (folders searched as the labelling mode does): boundary precision,
// recall and F within one bar, the share of bars with the same label, and whether the detected
// phrase grid lines up with a labelled one; then the totals over the tracks. With --split, each
// track line ends in T (tune) or H (held out), the fixed split of docs/PLAN_SECTIONS_BATCHES.md 1.1
// (held_out_track), and the totals follow for each set with its tracks per tempo band. --scores
// adds a line per track and a block after the totals (each set's too with --split): boundaries on
// the right bar (the labelled boundary's own bar), hit rates within 0.5 s and 3 s (all matched one
// to one), the signed offset in bars of the nearest detected boundary within 4 bars of each
// labelled one, recall and offsets for kick-change boundaries (the Viterbi kick state differs in
// the bars either side) and kick-continuous ones, recall per labelled transition (under 5 cases
// pooled as other), the confusion of bars by labelled and detected kind (a predrop counts as the
// section before it), and the detected over labelled boundary count per track with its median.
// --json <file> writes the --param overrides, one row per track (its set, tempo band and every
// score) and the totals for all, tune and held out, under the printed names in snake case, so
// two runs can be diffed by a script.
// --only tune|held_out scores one set of the split alone (a sweep tunes on the tune set without
// running the held-out tracks). --exclude <audio> (repeatable) leaves a file out of --sections-eval
// and --labels-stats (a second labelled copy of a track). --labels-stats <audio or folder>
// [more...] reads the labels of the tracks marked done, without analysis (the audio is decoded only
// for its length): per track its tempo, sections per minute, what else is labelled and its set (T
// or H); then, over all tracks and per tempo band (under 160, 160 to 175, over 175 BPM, a stand-in
// for subgenre), the median sections per minute, section lengths in bars per label (bars between
// nearest downbeats), the bars since the previous boundary (from bar 1 for the first) by their
// place on the 4 and 8-bar lines, breakdown lengths by what follows once builds and pre-drops are
// passed, and how many tracks carry a labelled phrase grid, phases, roles and events; and the
// labelled drops: their length (1, 2, 3 to 4, 5 to 8, 9 to 16, over 16 bars), the section before
// (its label and length) and the one after.
// --drops <audio or folder> [more...] (with --exclude, --param) analyses the tracks marked done
// and prints the detector's view of every labelled drop, of the groove after a drop and of a
// groove after a build (drops_study), then counts and medians per kind. Read-only.
// --kick-coverage <audio or folder> [more...] [--labelled-only] (Batch H1, read-only) prints per
// track the share of grid beats with a detected kick in the kick-on bars and in the labelled
// grooves and drops, and at the beats there without one whether the click sounds like a kick
// (kick_coverage).
// --param <id>=<value> (repeatable, every mode) sets a section parameter declared by
// declare_section_params, for example --param section.texture_step=1.2; an unknown id or a value
// outside the declared range is an error. The overrides are printed on the first line.
// --texture-step x stays as before (unchecked); it cannot be combined with
// --param section.texture_step.

#include "analysis/bass_tracker.h"
#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "analysis/sections.h"
#include "analysis/series.h"
#include "analysis/track_analysis.h"
#include "audio/audio_file.h"
#include "core/paths.h"
#include "core/pitch.h"
#include "io/labels.h"
#include "io/section_marks.h"
#include "io/stems.h"
#include "io/track_library.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
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

// One line per bar: what the section rules see, and the section the bar landed in.
void print_section_bars(const TrackAnalysis &a) {
    const Result<std::vector<SectionBar>> bars = section_bars(a.mix_features, a.beats, &a.residual);
    if (!bars) {
        return;
    }
    std::printf("  bars: index, start s, kicked beats, hats dB, flatness, centroid Hz, section\n");
    for (size_t m = 0; m < bars.value().size(); ++m) {
        const SectionBar &bar = bars.value()[m];
        const char *label = "";
        for (const DetectedSection &s : a.sections) {
            if (bar.start_s + 1e-3 >= s.start_s && bar.start_s < s.end_s) {
                label = s.label.c_str();
            }
        }
        std::printf("  %4zu %7.1f %c%d %6.1f %5.3f %6.0f %s\n", m, bar.start_s,
                    bar.kick ? 'K' : '.', bar.kick_beats, bar.high_db, bar.flatness,
                    bar.centroid_hz, label);
    }
}

int analyse(const std::filesystem::path &audio_path, const TrackAnalysisOptions &options,
            bool kick_features, bool local_tempo, bool bar_table) {
    const auto start = std::chrono::steady_clock::now();
    const Result<TrackAnalysis> analysis = analyse_track(audio_path, options);
    if (!analysis) {
        std::fprintf(stderr, "%s: %s\n", path_to_utf8(audio_path.filename()).c_str(),
                     analysis.error().message.c_str());
        return 1;
    }
    const double analysis_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const TrackAnalysis &a = analysis.value();
    const BeatTrackResult &b = a.beats;
    const FrontEndResult &features = a.mix_features;
    const BassTrackResult &bass = a.bass;
    const int sample_rate_hz = a.sample_rate_hz;
    const double duration_s = a.duration_s;
    std::printf("%s (%s): %.1f s\n", path_to_utf8(audio_path.filename()).c_str(),
                options.source == AnalysisSource::stems ? "Demucs stems" : "mix", duration_s);
    const Grid &grid = a.grid;
    const double beat_s = beat_s_of(grid);
    const double first_downbeat_s = samples_to_seconds(grid.first_downbeat, sample_rate_hz);
    std::printf("  analysed in %.1f s\n", analysis_s);
    std::printf("  tempo %.3f BPM (confidence %.2f); first downbeat %.3f s (confidence %.2f, "
                "%d kick cues); phrases of 8 bars start %d bars later (confidence %.2f, %d "
                "breaks)\n",
                b.bpm, b.tempo_confidence, first_downbeat_s, b.downbeat_confidence, b.downbeat_cues,
                b.phrase_bar_offset, b.phrase_confidence, b.phrase_cues);

    // Kicks against the grid: coverage of beats, and timing.
    const std::vector<double> &kicks = bass.kick_times_s;
    std::vector<double> kick_offsets_s;
    for (const double t : kicks) {
        kick_offsets_s.push_back(beat_offset_s(grid, t));
    }
    const double grid_beats = std::floor((duration_s - first_downbeat_s) / beat_s);
    std::printf("  kicks: %zu on %.0f grid beats (%.0f%%); kick minus beat median %+.1f ms\n",
                kicks.size(), grid_beats, 100.0 * kicks.size() / std::max(1.0, grid_beats),
                1e3 * median(kick_offsets_s));

    // Bass notes.
    const std::vector<BassNote> &notes = bass.notes;
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

    {
        // Hats: how many, and the share on offbeat 8ths (where psytrance puts them).
        int offbeat = 0;
        for (const HatOnset &hat : a.residual.hats) {
            const double beats_in = beat_offset_s(grid, hat.t_s - 0.5 * beat_s) / beat_s;
            offbeat += std::abs(beats_in) < 0.1;
        }
        std::vector<float> centroid;
        std::vector<float> flatness;
        for (size_t i = 0; i < a.residual.centroid_hz.size(); ++i) {
            if (a.residual.centroid_hz[i] > 0.0f) {
                centroid.push_back(a.residual.centroid_hz[i]);
                flatness.push_back(a.residual.flatness[i]);
            }
        }
        std::printf("  hats: %zu (%.0f%% on offbeats); residual centroid median %.0f Hz, "
                    "flatness median %.2f\n",
                    a.residual.hats.size(),
                    100.0 * offbeat / std::max<size_t>(1, a.residual.hats.size()),
                    quantile(centroid, 0.5), quantile(flatness, 0.5));
        std::vector<double> bar_notes;
        std::vector<double> bar_rests;
        std::vector<double> bar_gates;
        for (const BassAggregate &bar : a.aggregates.bars) {
            bar_notes.push_back(bar.notes);
            bar_rests.push_back(bar.rests);
            if (bar.notes > 0) {
                bar_gates.push_back(bar.gate_mean);
            }
        }
        std::printf("  per bar (median of %zu bars): %.0f notes, %.0f rests, gate mean %.2f; "
                    "%zu phrases\n",
                    a.aggregates.bars.size(), median(bar_notes), median(bar_rests),
                    median(bar_gates), a.aggregates.phrases.size());
    }
    {
        std::printf("  sections:");
        for (const DetectedSection &section : a.sections) {
            static constexpr const char *kEnergy[] = {"low", "mid", "high"};
            const std::string phase = phase_name(section.phase);
            std::printf(" %s%s%s%s%s %.0f-%.0f s (%.2f, %s)", section.label.c_str(),
                        phase.empty() ? "" : " ", phase.c_str(), section.role.empty() ? "" : " ",
                        section.role.c_str(), section.start_s, section.end_s, section.confidence,
                        kEnergy[std::clamp(section.energy, 0, 2)]);
            if (section.predrop_s >= 0.0) {
                std::printf(" pre-drop from %.1f s", section.predrop_s);
            }
            std::printf(";");
        }
        std::printf("\n  events:");
        for (const DetectedEvent &e : a.events) {
            std::printf(" %s %.1f s (%.1f s);", e.kind.c_str(), e.start_s, e.length_s);
        }
        std::printf("\n");
    }
    if (local_tempo) {
        print_local_tempo(features);
    }
    if (bar_table) {
        print_section_bars(a);
    }
    if (kick_features) {
        print_kick_features(features, grid);
        // Kicks per beat, one bar per group from the first grid beat: K kick, . none.
        std::printf("  kick timeline from %.3f s (bars of 4 beats from the first downbeat):\n  ",
                    b.first_kick_beat_s);
        const auto lead =
            static_cast<int>(std::lround((first_downbeat_s - b.first_kick_beat_s) / beat_s));
        for (int k = 0; k < static_cast<int>(b.beat_has_kick.size()); ++k) {
            if (k >= lead && (k - lead) % 4 == 0) {
                std::printf((k - lead) % 64 == 0 ? "\n  %4d: " : " ", (k - lead) / 4);
            }
            std::printf("%c", b.beat_has_kick[k] ? 'K' : '.');
        }
        std::printf("\n");
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

struct SectionStats {
    double per_minute = 0.0;
    double median_bars = 0.0;
    int short_sections = 0;
    double on_grid = 0.0; // Share of boundaries on the phrase grid.
    double flips_per_minute = 0.0;
};

std::optional<SectionStats> section_report(const std::filesystem::path &path,
                                           const SectionParams &section_params) {
    TrackAnalysisOptions options;
    options.source =
        analysis_stems_exist(stem_paths_for(path)) ? AnalysisSource::stems : AnalysisSource::mix;
    options.sections = section_params;
    const Result<TrackAnalysis> analysis = analyse_track(path, options);
    if (!analysis || analysis.value().beats.bpm <= 0.0) {
        std::printf("%-60s  no tempo\n", path_to_utf8(path.filename()).substr(0, 60).c_str());
        return std::nullopt;
    }
    const TrackAnalysis &a = analysis.value();
    const double bar_s = 4.0 * 60.0 / a.grid.bpm;
    const double first_s = samples_to_seconds(a.grid.first_downbeat, a.grid.sample_rate_hz);
    const double minutes = a.duration_s / 60.0;
    SectionStats s;
    std::vector<double> lengths;
    int boundaries = 0;
    int on_grid = 0;
    for (size_t i = 0; i < a.sections.size(); ++i) {
        const DetectedSection &sec = a.sections[i];
        const double bars = (sec.end_s - sec.start_s) / bar_s;
        lengths.push_back(bars);
        s.short_sections += bars < 3.5 && sec.label != "build";
        if (i > 0) {
            ++boundaries;
            const double phrase_pos =
                std::fmod((sec.start_s - first_s) / bar_s - a.beats.phrase_bar_offset, 8.0);
            const double off =
                std::min(std::fabs(phrase_pos), std::fabs(8.0 - std::fabs(phrase_pos)));
            on_grid += off <= 0.5;
        }
    }
    s.per_minute = a.sections.size() / std::max(1e-9, minutes);
    s.median_bars = median(lengths);
    s.on_grid = boundaries > 0 ? static_cast<double>(on_grid) / boundaries : 1.0;
    if (const Result<std::vector<SectionBar>> bars =
            section_bars(a.mix_features, a.beats, &a.residual, section_params);
        bars) {
        int flips = 0;
        for (size_t m = 1; m < bars.value().size(); ++m) {
            flips += bars.value()[m].kick != bars.value()[m - 1].kick;
        }
        s.flips_per_minute = flips / std::max(1e-9, minutes);
    }
    std::printf("%-60s  %5.2f/min  median %5.1f bars  short %2d  on grid %3.0f%%  flips %5.2f/min"
                "  (%s)\n",
                path_to_utf8(path.filename()).substr(0, 60).c_str(), s.per_minute, s.median_bars,
                s.short_sections, 100.0 * s.on_grid, s.flips_per_minute,
                options.source == AnalysisSource::stems ? "stems" : "mix");
    std::fflush(stdout);
    return s;
}

int sections_report(const std::vector<std::wstring> &paths, const SectionParams &section_params) {
    std::vector<std::filesystem::path> files;
    for (const std::wstring &p : paths) {
        const std::filesystem::path path(p);
        if (std::filesystem::is_directory(path)) {
            for (const auto &entry : std::filesystem::directory_iterator(path)) {
                std::wstring ext = entry.path().extension().wstring();
                for (wchar_t &c : ext) {
                    c = static_cast<wchar_t>(std::towlower(c));
                }
                if (entry.is_regular_file() &&
                    (ext == L".mp3" || ext == L".wav" || ext == L".flac")) {
                    files.push_back(entry.path());
                }
            }
        } else {
            files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end());
    std::vector<double> per_minute;
    std::vector<double> median_bars;
    std::vector<double> short_sections;
    std::vector<double> on_grid;
    std::vector<double> flips;
    for (const std::filesystem::path &file : files) {
        if (const std::optional<SectionStats> s = section_report(file, section_params); s) {
            per_minute.push_back(s->per_minute);
            median_bars.push_back(s->median_bars);
            short_sections.push_back(s->short_sections);
            on_grid.push_back(s->on_grid);
            flips.push_back(s->flips_per_minute);
        }
    }
    std::printf("\nMedians over %zu tracks: %.2f sections/min, median %.1f bars, %.0f short, "
                "%.0f%% of boundaries on the phrase grid, %.2f kick flips/min\n",
                per_minute.size(), median(per_minute), median(median_bars), median(short_sections),
                100.0 * median(on_grid), median(flips));
    return per_minute.empty() ? 1 : 0;
}

// The Viterbi kick state of the analysis's bar holding t (bar_s long): K, - or ? (no bar).
char kick_at(const TrackAnalysis &a, double bar_s, double t) {
    for (const SectionBar &b : a.section_bars) {
        if (t >= b.start_s && t < b.start_s + bar_s) {
            return b.kick ? 'K' : '-';
        }
    }
    return '?';
}

// --detail: every labelled boundary (L) and every detected one (D) with its change, its bar
// (from the labelled bar 1, and its place in an 8-bar phrase from there or from the labelled
// phrase grid), whether the other side has one within a bar, and the kick either side.
void print_boundary_detail(const Labels &l, const std::vector<SectionLabel> &detected,
                           const TrackAnalysis &a) {
    const double bar_s = bar_length_s(l.grid);
    const double tolerance_s = bar_s + 1e-6;
    const auto near_one = [&](const std::vector<SectionLabel> &list, double t) {
        for (size_t j = 1; j < list.size(); ++j) {
            if (std::fabs(list[j].start_s - t) <= tolerance_s) {
                return true;
            }
        }
        return false;
    };
    const LabelPhrases phrases =
        l.phrases.set ? l.phrases : LabelPhrases{true, l.grid.first_downbeat_s, 8};
    const auto print = [&](char kind, const std::vector<SectionLabel> &list,
                           const std::vector<SectionLabel> &other) {
        for (size_t i = 1; i < list.size(); ++i) {
            const double t = list[i].start_s;
            const LabelPhrases eight{true, phrases.start_s, 8};
            std::printf("    %c %8.2f s  %-9s -> %-9s  bar %4lld  in phrase %d  %s  kick %c%c\n",
                        kind, t, list[i - 1].label.c_str(), list[i].label.c_str(),
                        bar_index_at(l.grid, t + 1e-6) + 1, bar_in_phrase(l.grid, eight, t + 1e-6),
                        near_one(other, t) ? "hit " : "miss", kick_at(a, bar_s, t - 0.5 * bar_s),
                        kick_at(a, bar_s, t + 0.5 * bar_s));
        }
    };
    print('L', l.sections, detected);
    print('D', detected, l.sections);
}

// Tempo bands, as a stand-in for subgenre: under 160, 160 to 175, over 175 BPM.
constexpr const char *kTempoBands[] = {"under 160 BPM", "160 to 175 BPM", "over 175 BPM"};
int tempo_band(double bpm) { return bpm < 160.0 ? 0 : bpm <= 175.0 ? 1 : 2; }

// --sections-eval's sums over a set of tracks: all, tune or held out (--split).
struct EvalTotals {
    int tracks = 0;
    SectionScore score;
    int phrase_tracks = 0;
    int phrase_agree = 0;
    std::array<int, 3> tempo_bands{}; // Tracks per kTempoBands, by the labelled tempo.
    BoundaryScores boundaries;
    std::vector<double> count_ratios; // Detected over labelled boundaries, per track.
};
constexpr const char *kSetNames[] = {"All", "Tune", "Held out"};

struct EvalFlags {
    bool detail = false;        // --detail
    bool split = false;         // --split
    bool scores = false;        // --scores
    int only = -1;              // --only: 0 the tune set, 1 the held-out set; -1 both.
    std::filesystem::path json; // --json <file>; empty: none.
};

// --json: the figures --scores prints, under their printed names in snake case.
nlohmann::json scores_json(const SectionScore &s, const BoundaryScores &b) {
    nlohmann::json j;
    j["labelled"] = s.labelled_boundaries;
    j["detected"] = s.detected_boundaries;
    j["boundaries_p"] = s.precision();
    j["boundaries_r"] = s.recall();
    j["boundaries_f"] = s.f1();
    j["bars_labelled_alike"] = s.bar_accuracy();
    const auto hits = [&](const char *name, const BoundaryHits &h) {
        j[std::string(name) + "_p"] = h.precision();
        j[std::string(name) + "_r"] = h.recall();
        j[std::string(name) + "_f"] = h.f1();
    };
    hits("right_bar", b.right_bar);
    hits("within_0_5_s", b.within_half_s);
    hits("within_3_s", b.within_3_s);
    const auto kind = [&](const char *name, const BoundaryOffsets &o) {
        j[name] = {{"labelled", o.labelled},
                   {"recall_within_a_bar", o.recall()},
                   {"offsets_minus_4_to_plus_4", o.bars},
                   {"none", o.none}};
    };
    kind("all_boundaries", b.all);
    kind("kick_change", b.kick_change);
    kind("kick_continuous", b.kick_continuous);
    nlohmann::json transitions = nlohmann::json::object();
    for (const auto &[name, counts] : b.transitions) {
        transitions[name] = {{"labelled", counts.first}, {"matched", counts.second}};
    }
    j["by_transition"] = transitions;
    nlohmann::json confusion = nlohmann::json::object();
    for (size_t r = 0; r < kSectionKinds; ++r) {
        for (size_t c = 0; c < kSectionKinds; ++c) {
            confusion[kSectionNames[r]][kSectionNames[c]] = b.confusion[r][c];
        }
    }
    j["confusion"] = confusion;
    return j;
}

nlohmann::json totals_json(const EvalTotals &t) {
    nlohmann::json j = scores_json(t.score, t.boundaries);
    j["tracks"] = t.tracks;
    j["phrase_grid_agrees"] = t.phrase_agree;
    j["phrase_grid_labelled"] = t.phrase_tracks;
    j["count_ratio_median"] = median(t.count_ratios);
    for (size_t band = 0; band < t.tempo_bands.size(); ++band) {
        j["tracks_by_tempo_band"][kTempoBands[band]] = t.tempo_bands[band];
    }
    return j;
}

std::string offsets_text(const BoundaryOffsets &o) {
    std::string text;
    char item[32];
    for (int k = -kOffsetBars; k <= kOffsetBars; ++k) {
        std::snprintf(item, sizeof(item), " %+d: %d", k,
                      o.bars[static_cast<size_t>(k + kOffsetBars)]);
        text += item;
    }
    std::snprintf(item, sizeof(item), ", none: %d", o.none);
    return text + item;
}

// The --scores block for one set of tracks.
void print_boundary_scores(const char *set, const EvalTotals &t) {
    const BoundaryScores &b = t.boundaries;
    std::printf(
        "%s, %d track%s: right bar P %.2f R %.2f F %.2f; within 0.5 s P %.2f R %.2f F %.2f; "
        "within 3 s P %.2f R %.2f F %.2f; count ratio median %.2f\n",
        set, t.tracks, t.tracks == 1 ? "" : "s", b.right_bar.precision(), b.right_bar.recall(),
        b.right_bar.f1(), b.within_half_s.precision(), b.within_half_s.recall(),
        b.within_half_s.f1(), b.within_3_s.precision(), b.within_3_s.recall(), b.within_3_s.f1(),
        median(t.count_ratios));
    const auto kind = [](const char *name, const BoundaryOffsets &o) {
        std::printf(
            "  %s, %d labelled: recall within a bar %.2f; offsets (detected minus labelled, "
            "bars)%s\n",
            name, o.labelled, o.recall(), offsets_text(o).c_str());
    };
    kind("all boundaries", b.all);
    kind("kick change", b.kick_change);
    kind("kick-continuous", b.kick_continuous);
    std::vector<std::pair<std::string, std::pair<int, int>>> transitions;
    std::pair<int, int> other{0, 0};
    for (const auto &[name, counts] : b.transitions) {
        if (counts.first < 5) {
            other.first += counts.first;
            other.second += counts.second;
        } else {
            transitions.emplace_back(name, counts);
        }
    }
    std::stable_sort(transitions.begin(), transitions.end(),
                     [](const auto &x, const auto &y) { return x.second.first > y.second.first; });
    if (other.first > 0) {
        transitions.emplace_back("other", other);
    }
    std::printf("  by transition (recall within a bar, of labelled):");
    for (const auto &[name, counts] : transitions) {
        std::printf(" %s %.2f of %d;", name.c_str(), 1.0 * counts.second / counts.first,
                    counts.first);
    }
    std::printf("\n  confusion (bars; rows labelled, columns detected):\n    %-9s", "");
    for (size_t c = 0; c < kSectionKinds; ++c) {
        std::printf(" %9s", kSectionNames[c]);
    }
    std::printf("\n");
    for (size_t r = 0; r < kSectionKinds; ++r) {
        std::printf("    %-9s", kSectionNames[r]);
        for (size_t c = 0; c < kSectionKinds; ++c) {
            std::printf(" %9d", b.confusion[r][c]);
        }
        std::printf("\n");
    }
    const auto row = [&](size_t r) {
        int n = 0;
        for (const int v : b.confusion[r]) {
            n += v;
        }
        return n;
    };
    constexpr size_t kGroove = 1;
    constexpr size_t kDrop = 4;
    std::printf("  labelled drop bars detected as groove: %.0f%% of %d; labelled groove bars "
                "detected as drop: %.0f%% of %d (SPEC 18 A9)\n",
                100.0 * b.confusion[kDrop][kGroove] / std::max(1, row(kDrop)), row(kDrop),
                100.0 * b.confusion[kGroove][kDrop] / std::max(1, row(kGroove)), row(kGroove));
}

// The tracks named, then those under the folders (searched as the labelling mode does), less
// the excluded files (--exclude, the same file by any path).
std::vector<TrackEntry> tracks_from(const std::vector<std::wstring> &paths,
                                    const std::vector<std::filesystem::path> &excludes) {
    std::vector<std::filesystem::path> folders;
    std::vector<TrackEntry> tracks;
    for (const std::wstring &p : paths) {
        const std::filesystem::path path(p);
        if (std::filesystem::is_directory(path)) {
            folders.push_back(path);
        } else {
            TrackEntry entry;
            entry.path = path;
            entry.name = path_to_utf8(path.filename());
            entry.status = label_status(path, &entry.sections);
            tracks.push_back(entry);
        }
    }
    for (TrackEntry &t : scan_track_folders(folders)) {
        tracks.push_back(std::move(t));
    }
    std::erase_if(tracks, [&](const TrackEntry &t) {
        return std::any_of(excludes.begin(), excludes.end(), [&](const std::filesystem::path &x) {
            std::error_code error;
            return std::filesystem::equivalent(t.path, x, error);
        });
    });
    return tracks;
}

int sections_eval(const std::vector<std::wstring> &paths, const EvalFlags &flags,
                  const SectionParams &section_params, const ParamValues &overrides,
                  const std::vector<std::filesystem::path> &excludes) {
    nlohmann::json json_tracks = nlohmann::json::array();
    const std::vector<TrackEntry> tracks = tracks_from(paths, excludes);
    std::array<EvalTotals, 3> totals; // All, tune, held out.
    DetailScore detail_total;
    for (const TrackEntry &track : tracks) {
        if (track.status != LabelStatus::done ||
            (flags.only >= 0 &&
             held_out_track(path_to_utf8(track.path.filename())) != (flags.only == 1))) {
            continue;
        }
        const Result<Labels> labels = load_labels(labels_path_for(track.path));
        if (!labels || labels.value().sections.empty()) {
            continue;
        }
        const Labels &l = labels.value();
        TrackAnalysisOptions options;
        options.source = analysis_stems_exist(stem_paths_for(track.path)) ? AnalysisSource::stems
                                                                          : AnalysisSource::mix;
        options.sections = section_params;
        const Result<TrackAnalysis> analysis = analyse_track(track.path, options);
        if (!analysis) {
            std::printf("%-50s  %s\n", track.name.substr(0, 50).c_str(),
                        analysis.error().message.c_str());
            continue;
        }
        const TrackAnalysis &a = analysis.value();
        const std::vector<SectionLabel> detected = sections_from_detection(a.sections, l.grid);
        const SectionScore s = score_sections(l.sections, detected, l.grid, a.duration_s);
        const double label_bar_s = bar_length_s(l.grid);
        const BoundaryScores boundaries =
            score_boundaries(l.sections, detected, l.grid, a.duration_s, [&](double t) {
                const char before = kick_at(a, label_bar_s, t - 0.5 * label_bar_s);
                const char after = kick_at(a, label_bar_s, t + 0.5 * label_bar_s);
                return before != '?' && after != '?' && before != after;
            });
        const bool held_out = held_out_track(path_to_utf8(track.path.filename()));
        EvalTotals *const sets[] = {&totals[0], &totals[held_out ? 2 : 1]};
        for (EvalTotals *t : sets) {
            t->score += s;
            t->boundaries += boundaries;
            if (s.labelled_boundaries > 0) {
                t->count_ratios.push_back(1.0 * s.detected_boundaries / s.labelled_boundaries);
            }
            ++t->tracks;
            ++t->tempo_bands[static_cast<size_t>(tempo_band(l.grid.bpm))];
        }
        std::string phrase = "phrase grid not labelled";
        if (l.phrases.set && a.beats.bpm > 0.0) {
            const double bar_s = 4.0 * 60.0 / a.beats.bpm;
            const double detected_start =
                samples_to_seconds(a.beats.grid.first_downbeat, a.beats.grid.sample_rate_hz) +
                a.beats.phrase_bar_offset * bar_s;
            const long long off = bar_index_at(l.grid, detected_start + 1e-6) -
                                  bar_index_at(l.grid, l.phrases.start_s + 1e-6);
            const long long bars = std::max(1, l.phrases.bars);
            const long long wrapped = ((off % bars) + bars) % bars;
            const long long shift = wrapped > bars / 2 ? wrapped - bars : wrapped;
            for (EvalTotals *t : sets) {
                ++t->phrase_tracks;
                t->phrase_agree += shift == 0;
            }
            phrase = shift == 0 ? "phrase grid agrees"
                                : "phrase grid off by " + std::to_string(shift) + " bars";
        }
        std::printf("%-50s  boundaries P %.2f R %.2f F %.2f (%d labelled, %d detected)  bars %.0f%%"
                    "  %s  (%s)%s\n",
                    track.name.substr(0, 50).c_str(), s.precision(), s.recall(), s.f1(),
                    s.labelled_boundaries, s.detected_boundaries, 100.0 * s.bar_accuracy(),
                    phrase.c_str(), options.source == AnalysisSource::stems ? "stems" : "mix",
                    !flags.split ? ""
                    : held_out   ? "  H"
                                 : "  T");
        {
            std::vector<EventLabel> found;
            for (const DetectedEvent &e : a.events) {
                found.push_back({e.kind, e.start_s, e.length_s});
            }
            const DetailScore d =
                score_section_detail(l.sections, detected, l.events, found, l.grid, a.duration_s);
            if (d.labelled()) {
                detail_total += d;
                std::printf("%-50s  phases %.2f agree (bar pairs), roles %.0f%% of %d bars, "
                            "events P %.2f R %.2f (%d labelled, %d detected)\n",
                            "", d.phase_agreement(), 100.0 * d.role_accuracy(), d.role_bars,
                            d.event_precision(), d.event_recall(), d.labelled_events,
                            d.detected_events);
            }
        }
        {
            nlohmann::json row = scores_json(s, boundaries);
            row["name"] = track.name;
            row["set"] = held_out ? "held_out" : "tune";
            row["bpm"] = l.grid.bpm;
            row["tempo_band"] = kTempoBands[tempo_band(l.grid.bpm)];
            row["source"] = options.source == AnalysisSource::stems ? "stems" : "mix";
            row["phrase_grid"] = phrase;
            row["count_ratio"] =
                s.labelled_boundaries > 0
                    ? nlohmann::json(1.0 * s.detected_boundaries / s.labelled_boundaries)
                    : nlohmann::json(nullptr);
            json_tracks.push_back(std::move(row));
        }
        if (flags.scores) {
            std::printf("%-50s  right bar P %.2f R %.2f F %.2f; within 0.5 s F %.2f, within 3 s F "
                        "%.2f; kick change recall %.2f of %d, kick-continuous %.2f of %d; count "
                        "ratio %.2f\n",
                        "", boundaries.right_bar.precision(), boundaries.right_bar.recall(),
                        boundaries.right_bar.f1(), boundaries.within_half_s.f1(),
                        boundaries.within_3_s.f1(), boundaries.kick_change.recall(),
                        boundaries.kick_change.labelled, boundaries.kick_continuous.recall(),
                        boundaries.kick_continuous.labelled,
                        s.labelled_boundaries > 0
                            ? 1.0 * s.detected_boundaries / s.labelled_boundaries
                            : 0.0);
        }
        if (flags.detail) {
            print_boundary_detail(l, detected, a);
        }
        std::fflush(stdout);
    }
    const EvalTotals &all = totals[0];
    if (all.tracks == 0) {
        std::printf("No track marked done (label sections in the app: View > Label sections, then "
                    "Done).\n");
        return 1;
    }
    std::printf("\nOver %d track%s: boundaries within a bar P %.2f R %.2f F %.2f; %.0f%% of bars "
                "labelled alike; phrase grid agrees on %d of %d labelled\n",
                all.tracks, all.tracks == 1 ? "" : "s", all.score.precision(), all.score.recall(),
                all.score.f1(), 100.0 * all.score.bar_accuracy(), all.phrase_agree,
                all.phrase_tracks);
    if (flags.split) {
        for (size_t set = 1; set < totals.size(); ++set) {
            const EvalTotals &t = totals[set];
            std::printf("%s, %d track%s (%s %d, %s %d, %s %d): boundaries within a bar P %.2f "
                        "R %.2f F %.2f; %.0f%% of bars labelled alike; phrase grid agrees on %d of "
                        "%d labelled\n",
                        kSetNames[set], t.tracks, t.tracks == 1 ? "" : "s", kTempoBands[0],
                        t.tempo_bands[0], kTempoBands[1], t.tempo_bands[1], kTempoBands[2],
                        t.tempo_bands[2], t.score.precision(), t.score.recall(), t.score.f1(),
                        100.0 * t.score.bar_accuracy(), t.phrase_agree, t.phrase_tracks);
        }
    }
    if (detail_total.labelled()) {
        std::printf("Finer detail: phases %.2f agree, roles %.0f%% of %d bars, events P %.2f "
                    "R %.2f (%d labelled)\n",
                    detail_total.phase_agreement(), 100.0 * detail_total.role_accuracy(),
                    detail_total.role_bars, detail_total.event_precision(),
                    detail_total.event_recall(), detail_total.labelled_events);
    } else {
        std::printf("Finer detail: none labelled yet (phases, roles, breaks and fills in the "
                    "labelling mode, Detailed on).\n");
    }
    if (flags.scores) {
        std::printf("\nBoundary scores (docs/PLAN_SECTIONS_BATCHES.md A2):\n");
        for (size_t set = 0; set < (flags.split ? totals.size() : 1); ++set) {
            print_boundary_scores(kSetNames[set], totals[set]);
        }
    }
    if (!flags.json.empty()) {
        nlohmann::json out;
        out["parameters"] = overrides;
        out["tracks"] = json_tracks;
        out["all"] = totals_json(totals[0]);
        out["tune"] = totals_json(totals[1]);
        out["held_out"] = totals_json(totals[2]);
        std::ofstream file(flags.json, std::ios::binary);
        file << out.dump(2) << "\n";
        if (!file) {
            std::fprintf(stderr, "could not write %s\n", path_to_utf8(flags.json).c_str());
            return 1;
        }
    }
    return 0;
}

// --param <id>=<value>: one override of a parameter declared by declare_section_params, checked
// against its declared range. Adds it to `overrides`; false (with a message) when it is not valid.
bool parse_param(const wchar_t *arg, ParamValues &overrides) {
    const std::string text = path_to_utf8(std::filesystem::path(arg));
    const size_t eq = text.find('=');
    ParamRegistry registry;
    (void)declare_section_params(registry);
    const ParamSpec *spec = eq == std::string::npos ? nullptr : registry.find(text.substr(0, eq));
    if (spec == nullptr) {
        std::fprintf(stderr, "--param %s: expected <id>=<value> with a section.* id\n",
                     text.c_str());
        return false;
    }
    const std::string number = text.substr(eq + 1);
    char *end = nullptr;
    const double value = std::strtod(number.c_str(), &end);
    if (number.empty() || end != number.c_str() + number.size() || !std::isfinite(value) ||
        value < spec->min || value > spec->max) {
        std::fprintf(stderr, "--param %s: the value must be a number from %g to %g\n", text.c_str(),
                     spec->min, spec->max);
        return false;
    }
    overrides[spec->id] = value;
    return true;
}

// --labels-stats: the labelled sections alone (no analysis), over a set of tracks.
constexpr const char *kLengthBins[] = {"1", "2",    "3",  "4",     "5-7",
                                       "8", "9-15", "16", "17-31", "32+"};
constexpr size_t kLengthBinCount = std::size(kLengthBins);
size_t length_bin(long long bars) {
    if (bars <= 4) {
        return static_cast<size_t>(std::max(1LL, bars) - 1);
    }
    return bars <= 7 ? 4 : bars == 8 ? 5 : bars <= 15 ? 6 : bars == 16 ? 7 : bars <= 31 ? 8 : 9;
}

struct LabelStats {
    int tracks = 0;
    std::vector<double> per_minute;
    std::map<std::string, std::array<int, kLengthBinCount>> lengths; // Per label.
    // Bars since the previous boundary (bar 1 for the first): a multiple of 8, of 4 but not 8,
    // one bar off a multiple of 4, other (two bars off).
    std::array<int, 4> spacing{};
    // Breakdown lengths by what follows once builds and pre-drops are passed: drop, groove, other.
    std::map<std::string, std::array<int, kLengthBinCount>> breakdowns;
    int phrase_grids = 0;
    int phases = 0;
    int roles = 0;
    int events = 0;
    // Labelled drops: length (kDropBins), the section before (label: its lengths by
    // kLengthBins) and the one after (label: count).
    std::array<int, 6> drop_lengths{};
    std::map<std::string, std::array<int, kLengthBinCount>> drop_before;
    std::map<std::string, int> drop_after;
};

constexpr const char *kDropBins[] = {"1", "2", "3-4", "5-8", "9-16", "17+"};
size_t drop_bin(long long bars) {
    return bars <= 1 ? 0 : bars == 2 ? 1 : bars <= 4 ? 2 : bars <= 8 ? 3 : bars <= 16 ? 4 : 5;
}

void add_label_stats(LabelStats &st, const Labels &l, double duration_s) {
    const std::vector<SectionLabel> &sec = l.sections;
    const double bar_s = bar_length_s(l.grid);
    const auto bar_of = [&](double t) {
        return bar_s > 0.0 ? std::llround((t - l.grid.first_downbeat_s) / bar_s) : 0LL;
    };
    ++st.tracks;
    st.per_minute.push_back(static_cast<double>(sec.size()) / std::max(1e-9, duration_s / 60.0));
    for (size_t i = 0; i < sec.size(); ++i) {
        const double end_s = i + 1 < sec.size() ? sec[i + 1].start_s : duration_s;
        const long long bars = bar_of(end_s) - bar_of(sec[i].start_s);
        auto &bins = st.lengths[sec[i].label];
        ++bins[length_bin(bars)];
        if (i > 0) {
            const long long since =
                bar_of(sec[i].start_s) - (i == 1 ? 0LL : bar_of(sec[i - 1].start_s));
            const long long mod4 = ((since % 4) + 4) % 4;
            ++st.spacing[mod4 == 0 ? (((since % 8) + 8) % 8 == 0 ? 0 : 1) : mod4 == 2 ? 3 : 2];
        }
        if (sec[i].label == "drop") {
            ++st.drop_lengths[drop_bin(bars)];
            const std::string before = i > 0 ? sec[i - 1].label : "start";
            const long long before_bars =
                i > 0 ? bar_of(sec[i].start_s) - bar_of(sec[i - 1].start_s) : 0;
            ++st.drop_before[before][length_bin(before_bars)];
            ++st.drop_after[i + 1 < sec.size() ? sec[i + 1].label : "end"];
        }
        if (sec[i].label == "breakdown") {
            size_t j = i + 1;
            while (j < sec.size() && (sec[j].label == "build" || sec[j].label == "predrop")) {
                ++j;
            }
            const std::string next =
                j < sec.size() && (sec[j].label == "drop" || sec[j].label == "groove")
                    ? sec[j].label
                    : "other";
            ++st.breakdowns[next][length_bin(bars)];
        }
    }
    st.phrase_grids += l.phrases.set;
    st.phases +=
        std::any_of(sec.begin(), sec.end(), [](const SectionLabel &x) { return !x.phase.empty(); });
    st.roles +=
        std::any_of(sec.begin(), sec.end(), [](const SectionLabel &x) { return !x.role.empty(); });
    st.events += !l.events.empty();
}

void print_label_stats(const char *set, const LabelStats &st) {
    std::printf("\n%s: %d track%s; sections per minute median %.2f\n", set, st.tracks,
                st.tracks == 1 ? "" : "s", median(st.per_minute));
    if (st.tracks == 0) {
        return;
    }
    std::printf("  section length in bars:%*s", 9, "");
    for (const char *bin : kLengthBins) {
        std::printf(" %5s", bin);
    }
    std::printf("\n");
    const auto bins = [](const std::string &name, const std::array<int, kLengthBinCount> &b) {
        std::printf("    %-30s", name.c_str());
        for (const int n : b) {
            std::printf(" %5d", n);
        }
        std::printf("\n");
    };
    for (const auto &[label, b] : st.lengths) {
        bins(label, b);
    }
    const int boundaries = st.spacing[0] + st.spacing[1] + st.spacing[2] + st.spacing[3];
    const auto share = [&](int n) { return 100.0 * n / std::max(1, boundaries); };
    std::printf(
        "  bars since the previous boundary, of %d boundaries: a multiple of 8 %.0f%%, of 4 "
        "but not 8 %.0f%%, one bar off a multiple of 4 %.0f%%, other %.0f%%\n",
        boundaries, share(st.spacing[0]), share(st.spacing[1]), share(st.spacing[2]),
        share(st.spacing[3]));
    std::printf("  breakdown length in bars, by what follows (builds and pre-drops passed):\n");
    for (const auto &[next, b] : st.breakdowns) {
        bins("then " + next, b);
    }
    std::printf("  tracks with a labelled phrase grid %d, phases %d, roles %d, events %d\n",
                st.phrase_grids, st.phases, st.roles, st.events);
    int drops = 0;
    for (const int n : st.drop_lengths) {
        drops += n;
    }
    std::printf("  drops, %d: length in bars", drops);
    for (size_t b = 0; b < st.drop_lengths.size(); ++b) {
        std::printf(" %s: %d", kDropBins[b], st.drop_lengths[b]);
    }
    std::printf("\n  the section before a drop, by its length in bars:\n");
    for (const auto &[label, b] : st.drop_before) {
        bins("after " + label, b);
    }
    std::printf("  the section after a drop:");
    for (const auto &[label, n] : st.drop_after) {
        std::printf(" %s %d;", label.c_str(), n);
    }
    std::printf("\n");
}

int labels_stats(const std::vector<std::wstring> &paths,
                 const std::vector<std::filesystem::path> &excludes) {
    std::array<LabelStats, 4> stats; // All, then per tempo band.
    for (const TrackEntry &track : tracks_from(paths, excludes)) {
        if (track.status != LabelStatus::done) {
            continue;
        }
        const Result<Labels> labels = load_labels(labels_path_for(track.path));
        if (!labels || labels.value().sections.empty()) {
            continue;
        }
        const Labels &l = labels.value();
        const Result<AudioBuffer> audio = decode_audio_file(track.path);
        if (!audio) {
            std::printf("%-50s  %s\n", track.name.substr(0, 50).c_str(),
                        audio.error().message.c_str());
            continue;
        }
        const double duration_s =
            static_cast<double>(audio.value().frames()) / audio.value().sample_rate_hz;
        const int band = tempo_band(l.grid.bpm);
        add_label_stats(stats[0], l, duration_s);
        add_label_stats(stats[static_cast<size_t>(band) + 1], l, duration_s);
        std::printf("%-50s  %6.2f BPM  %3zu sections in %5.0f s: %.2f/min  %s%s%s%s  %s\n",
                    track.name.substr(0, 50).c_str(), l.grid.bpm, l.sections.size(), duration_s,
                    stats[0].per_minute.back(), l.phrases.set ? " phrase-grid" : "",
                    std::any_of(l.sections.begin(), l.sections.end(),
                                [](const SectionLabel &x) { return !x.phase.empty(); })
                        ? " phases"
                        : "",
                    std::any_of(l.sections.begin(), l.sections.end(),
                                [](const SectionLabel &x) { return !x.role.empty(); })
                        ? " roles"
                        : "",
                    l.events.empty() ? "" : " events",
                    held_out_track(path_to_utf8(track.path.filename())) ? "H" : "T");
        std::fflush(stdout);
    }
    if (stats[0].tracks == 0) {
        std::printf("No track marked done.\n");
        return 1;
    }
    print_label_stats("All", stats[0]);
    for (size_t band = 0; band < 3; ++band) {
        print_label_stats(kTempoBands[band], stats[band + 1]);
    }
    return 0;
}

// --drops: the detector's view of every labelled drop, of the groove after it and of a groove
// that follows a build without being labelled drop (the owner's drops,
// docs/PLAN_SECTIONS_BATCHES.md section 11). Per section: its bars, the kick state and kicked beats
// of the two bars before and of its first bar (the detection's Viterbi bars), the bar level (dB
// from the track's median bar) and intensity over the 4 bars before, its own bars and the 4 bars
// after, and the detected labels over its bars. Read-only: no rule changes.
struct DropView {
    std::string kind; // "drop", "groove after drop", "groove after build".
    std::string track;
    double start_s = 0.0;
    long long bars = 0;
    std::string before;
    long long before_bars = 0;
    std::string after;
    std::string kicks;                  // Bars -2, -1, 0: K or -, with kicked beats.
    std::array<double, 3> level_db{};   // Before, during, after.
    std::array<double, 3> intensity{};  // Before, during, after.
    std::map<std::string, int> covered; // Detected label per bar.
};

int drops_study(const std::vector<std::wstring> &paths,
                const std::vector<std::filesystem::path> &excludes,
                const SectionParams &section_params) {
    std::vector<DropView> views;
    for (const TrackEntry &track : tracks_from(paths, excludes)) {
        if (track.status != LabelStatus::done) {
            continue;
        }
        const Result<Labels> labels = load_labels(labels_path_for(track.path));
        if (!labels || labels.value().sections.empty()) {
            continue;
        }
        const Labels &l = labels.value();
        TrackAnalysisOptions options;
        options.source = analysis_stems_exist(stem_paths_for(track.path)) ? AnalysisSource::stems
                                                                          : AnalysisSource::mix;
        options.sections = section_params;
        const Result<TrackAnalysis> analysis = analyse_track(track.path, options);
        if (!analysis) {
            std::printf("%-50s  %s\n", track.name.substr(0, 50).c_str(),
                        analysis.error().message.c_str());
            continue;
        }
        const TrackAnalysis &a = analysis.value();
        const std::vector<SectionBar> &bars = a.section_bars;
        if (bars.empty()) {
            continue;
        }
        std::vector<double> levels;
        for (const SectionBar &b : bars) {
            levels.push_back(b.level_db);
        }
        const double median_level = median(levels);
        // The detection's bar nearest a time.
        const auto bar_at = [&](double t) {
            size_t best = 0;
            for (size_t m = 1; m < bars.size(); ++m) {
                if (std::fabs(bars[m].start_s - t) < std::fabs(bars[best].start_s - t)) {
                    best = m;
                }
            }
            return static_cast<long long>(best);
        };
        const auto mean_over = [&](long long from, long long to, bool level) {
            double sum = 0.0;
            int n = 0;
            for (long long m = std::max(0LL, from);
                 m < std::min(to, static_cast<long long>(bars.size())); ++m) {
                if (level) {
                    sum += bars[static_cast<size_t>(m)].level_db - median_level;
                } else if (static_cast<size_t>(m) < a.intensity.size()) {
                    sum += a.intensity[static_cast<size_t>(m)];
                } else {
                    continue;
                }
                ++n;
            }
            return n > 0 ? sum / n : std::nan("");
        };
        const std::vector<SectionLabel> detected = sections_from_detection(a.sections, l.grid);
        const std::vector<SectionLabel> &sec = l.sections;
        const double bar_s = bar_length_s(l.grid);
        for (size_t i = 1; i < sec.size(); ++i) {
            std::string kind;
            if (sec[i].label == "drop") {
                kind = "drop";
            } else if (sec[i].label == "groove" && sec[i - 1].label == "drop") {
                kind = "groove after drop";
            } else if (sec[i].label == "groove" &&
                       (sec[i - 1].label == "build" || sec[i - 1].label == "predrop")) {
                kind = "groove after build";
            } else {
                continue;
            }
            DropView v;
            v.kind = kind;
            v.track = track.name;
            v.start_s = sec[i].start_s;
            const double end_s = i + 1 < sec.size() ? sec[i + 1].start_s : a.duration_s;
            v.bars = std::llround((end_s - sec[i].start_s) / bar_s);
            v.before = sec[i - 1].label;
            v.before_bars = std::llround((sec[i].start_s - sec[i - 1].start_s) / bar_s);
            v.after = i + 1 < sec.size() ? sec[i + 1].label : "end";
            const long long first = bar_at(sec[i].start_s);
            const long long last = std::max(first + 1, bar_at(end_s));
            for (long long m = first - 2; m <= first; ++m) {
                if (m < 0) {
                    v.kicks += "?? ";
                    continue;
                }
                const SectionBar &b = bars[static_cast<size_t>(m)];
                v.kicks += std::string(1, b.kick ? 'K' : '-') + std::to_string(b.kick_beats) + " ";
            }
            v.level_db = {mean_over(first - 4, first, true), mean_over(first, last, true),
                          mean_over(last, last + 4, true)};
            v.intensity = {mean_over(first - 4, first, false), mean_over(first, last, false),
                           mean_over(last, last + 4, false)};
            for (long long m = first; m < last && m < static_cast<long long>(bars.size()); ++m) {
                const int d =
                    section_at(detected, bars[static_cast<size_t>(m)].start_s + 0.5 * bar_s);
                ++v.covered[d >= 0 ? detected[static_cast<size_t>(d)].label : "none"];
            }
            std::printf("%-18s  %-40s %7.1f s  %3lld bars  after %-9s %3lld bars  then %-9s  "
                        "kicks %s level %+5.1f %+5.1f %+5.1f dB  intensity %.2f %.2f %.2f  "
                        "detected",
                        v.kind.c_str(), v.track.substr(0, 40).c_str(), v.start_s, v.bars,
                        v.before.c_str(), v.before_bars, v.after.c_str(), v.kicks.c_str(),
                        v.level_db[0], v.level_db[1], v.level_db[2], v.intensity[0], v.intensity[1],
                        v.intensity[2]);
            for (const auto &[label, n] : v.covered) {
                std::printf(" %s %d", label.c_str(), n);
            }
            std::printf("\n");
            views.push_back(std::move(v));
        }
        std::fflush(stdout);
    }
    // Per kind: counts and medians.
    for (const char *kind : {"drop", "groove after drop", "groove after build"}) {
        std::vector<double> bars_n, before_bars, lb, ld, la, ib, id, ia;
        std::array<int, 6> lengths{};
        std::map<std::string, int> before, after, covered_by;
        int kick_off_before = 0, kick_on_first = 0, n = 0;
        for (const DropView &v : views) {
            if (v.kind != kind) {
                continue;
            }
            ++n;
            bars_n.push_back(static_cast<double>(v.bars));
            ++lengths[drop_bin(v.bars)];
            ++before[v.before];
            before_bars.push_back(static_cast<double>(v.before_bars));
            ++after[v.after];
            kick_off_before += v.kicks.size() >= 6 && v.kicks[0] == '-' && v.kicks[3] == '-';
            kick_on_first += v.kicks.size() >= 7 && v.kicks[6] == 'K';
            const auto add = [](std::vector<double> &to, double x) {
                if (std::isfinite(x)) {
                    to.push_back(x);
                }
            };
            add(lb, v.level_db[0]);
            add(ld, v.level_db[1]);
            add(la, v.level_db[2]);
            add(ib, v.intensity[0]);
            add(id, v.intensity[1]);
            add(ia, v.intensity[2]);
            std::string most;
            int most_n = -1;
            for (const auto &[label, c] : v.covered) {
                if (c > most_n) {
                    most = label;
                    most_n = c;
                }
            }
            ++covered_by[most];
        }
        std::printf("\n%s, %d: length in bars", kind, n);
        for (size_t b = 0; b < lengths.size(); ++b) {
            std::printf(" %s: %d", kDropBins[b], lengths[b]);
        }
        std::printf(" (median %.0f)\n  before:", median(bars_n));
        for (const auto &[label, c] : before) {
            std::printf(" %s %d;", label.c_str(), c);
        }
        std::printf(" (median %.0f bars)\n  after:", median(before_bars));
        for (const auto &[label, c] : after) {
            std::printf(" %s %d;", label.c_str(), c);
        }
        std::printf("\n  detected kick off in both bars before: %d; on in the first bar: %d\n",
                    kick_off_before, kick_on_first);
        std::printf("  level (dB from the track's median bar), median: before %+.1f, during "
                    "%+.1f, after %+.1f; intensity: %.2f, %.2f, %.2f\n",
                    median(lb), median(ld), median(la), median(ib), median(id), median(ia));
        std::printf("  detected label over most of its bars:");
        for (const auto &[label, c] : covered_by) {
            std::printf(" %s %d;", label.c_str(), c);
        }
        std::printf("\n");
    }
    return views.empty() ? 1 : 0;
}

// --kick-coverage (Batch H1, read-only): how many grid beats carry a detected kick where the
// music has one. Per track: the share of grid beats with a kick by the beat tracker
// (beat_has_kick, what the sections read) and by the bass tracker (kick_times_s within 35 ms)
// inside the bars the sections' kick state calls kick-on, and inside the labelled grooves and
// drops when the track has labelled sections; and, at the beats without a beat-tracker kick
// there, the click (peak click-band flux within 2 frames, over the track's 99th percentile), its
// ratio to the low-mid flux, and the low band's rise in dB across it (as --kicks measures them). A
// beat whose click is at least 0.4 of the 99th percentile and at least 0.2 of the low-mid flux
// sounds like a kick the detector missed; one with a weak click is a real gap.
struct KickView {
    double click = 0.0; // Over the track's 99th percentile.
    double ratio = 0.0; // Click over low-mid flux.
    double rise_db = 0.0;
};

int kick_coverage(const std::vector<std::wstring> &paths,
                  const std::vector<std::filesystem::path> &excludes,
                  const TrackAnalysisOptions &base_options, bool labelled_only) {
    std::vector<double> labelled_cov;
    int tracks = 0;
    std::printf(
        "track | beats with a kick in kick-on bars: beat tracker, bass tracker | in labelled "
        "grooves and drops: beat tracker, bass tracker | beats there without a kick: "
        "count, kick-like click, median low rise dB (kick-like), median low rise dB (all "
        "beats with a kick) | labelled drops: beat-tracker kick share\n");
    for (const TrackEntry &track : tracks_from(paths, excludes)) {
        Labels labels;
        bool has_labels = false;
        if (std::filesystem::exists(labels_path_for(track.path))) {
            if (Result<Labels> l = load_labels(labels_path_for(track.path)); l) {
                labels = l.value();
                has_labels = !labels.sections.empty() && labels.sections_done;
            }
        }
        if (labelled_only && !has_labels) {
            continue;
        }
        TrackAnalysisOptions options = base_options;
        options.source = analysis_stems_exist(stem_paths_for(track.path)) ? AnalysisSource::stems
                                                                          : AnalysisSource::mix;
        const Result<TrackAnalysis> analysis = analyse_track(track.path, options);
        if (!analysis || analysis.value().beats.bpm <= 0.0) {
            std::printf("%s | no tempo\n", track.name.c_str());
            continue;
        }
        const TrackAnalysis &a = analysis.value();
        const FrontEndResult &f = a.mix_features;
        const double frame_s = static_cast<double>(f.hop_samples) / f.sample_rate_hz;
        const double period_s = 60.0 / a.beats.bpm;
        const std::vector<float> &click = f.bands[3].flux;
        const std::vector<float> &lowmid = f.bands[2].flux;
        const double click_p99 = std::max(1e-9, static_cast<double>(quantile(click, 0.99)));
        std::vector<float> low(f.frames);
        for (int i = 0; i < f.frames; ++i) {
            low[i] = static_cast<float>(std::pow(10.0, f.bands[0].energy_db[i] / 10.0) +
                                        std::pow(10.0, f.bands[1].energy_db[i] / 10.0));
        }
        low = box_mean(low, 2);
        const auto view = [&](double t) {
            KickView v;
            const int centre = static_cast<int>(std::lround(t / frame_s));
            int peak = -1;
            for (int i = std::max(1, centre - 2); i <= std::min(f.frames - 2, centre + 2); ++i) {
                peak = peak < 0 || click[i] > click[peak] ? i : peak;
            }
            if (peak < 4 || peak + 4 >= f.frames) {
                return v;
            }
            float before = low[peak];
            float after = low[peak];
            for (int k = 1; k <= 4; ++k) {
                before = std::min(before, low[peak - k]);
                after = std::max(after, low[peak + k]);
            }
            v.click = click[peak] / click_p99;
            v.ratio = click[peak] / std::max(lowmid[peak], 1e-6f);
            v.rise_db = 10.0 * std::log10((after + 1e-12) / (before + 1e-12));
            return v;
        };
        const auto bass_kick_near = [&](double t) {
            const std::vector<double> &k = a.bass.kick_times_s;
            const auto it = std::lower_bound(k.begin(), k.end(), t - 0.035);
            return it != k.end() && *it <= t + 0.035;
        };
        const auto kick_on_bar = [&](double t) {
            for (const SectionBar &b : a.section_bars) {
                if (t >= b.start_s && t < b.start_s + 4.0 * period_s) {
                    return b.kick;
                }
            }
            return false;
        };
        const auto labelled_kick = [&](double t) {
            const int i = section_at(labels.sections, t);
            if (i < 0) {
                return false;
            }
            std::string label = labels.sections[static_cast<size_t>(i)].label;
            return label == "groove" || label == "drop";
        };
        const auto labelled_drop = [&](double t) {
            const int i = section_at(labels.sections, t);
            return i >= 0 && labels.sections[static_cast<size_t>(i)].label == "drop";
        };
        int on = 0, on_beat = 0, on_bass = 0, lab = 0, lab_beat = 0, lab_bass = 0;
        int drop = 0, drop_beat = 0;
        int gaps = 0, kick_like = 0;
        std::vector<double> rise_missed, rise_kicked;
        for (size_t k = 0; k < a.beats.beat_has_kick.size(); ++k) {
            const double t = a.beats.first_kick_beat_s + static_cast<double>(k) * period_s;
            const bool kicked = a.beats.beat_has_kick[k];
            const bool bass = bass_kick_near(t);
            const bool in_on = kick_on_bar(t);
            const bool in_lab = has_labels && labelled_kick(t);
            on += in_on;
            on_beat += in_on && kicked;
            on_bass += in_on && bass;
            lab += in_lab;
            lab_beat += in_lab && kicked;
            lab_bass += in_lab && bass;
            const bool in_drop = has_labels && labelled_drop(t);
            drop += in_drop;
            drop_beat += in_drop && kicked;
            const KickView v = view(t);
            if (kicked) {
                rise_kicked.push_back(v.rise_db);
            } else if (has_labels ? in_lab : in_on) {
                ++gaps;
                if (v.click >= 0.4 && v.ratio >= 0.2) {
                    ++kick_like;
                    rise_missed.push_back(v.rise_db);
                }
            }
        }
        ++tracks;
        const auto pct = [](int n, int d) { return d > 0 ? 100.0 * n / d : 0.0; };
        std::printf(
            "%s | %.0f%%, %.0f%% of %d | %s | %d, %d (%.0f%%), %.2f, %.2f | drops %.0f%% of %d\n",
            track.name.c_str(), pct(on_beat, on), pct(on_bass, on), on,
            has_labels
                ? (std::to_string(static_cast<int>(std::lround(pct(lab_beat, lab)))) + "%, " +
                   std::to_string(static_cast<int>(std::lround(pct(lab_bass, lab)))) + "% of " +
                   std::to_string(lab))
                      .c_str()
                : "-",
            gaps, kick_like, pct(kick_like, gaps), median(rise_missed), median(rise_kicked),
            pct(drop_beat, drop), drop);
        std::fflush(stdout);
        if (has_labels) {
            labelled_cov.push_back(pct(lab_beat, lab));
        }
    }
    std::printf("\n%d tracks; labelled grooves and drops, beat-tracker kick share median %.0f%%\n",
                tracks, median(labelled_cov));
    return tracks > 0 ? 0 : 1;
}

int wmain(int argc, wchar_t **argv) {
    bool kick_features = false;
    bool local_tempo = false;
    bool bar_table = false;
    bool report = false;
    bool evaluate = false;
    bool label_statistics = false;
    bool drops = false;
    bool coverage = false;
    bool coverage_labelled_only = false;
    EvalFlags eval_flags;
    std::vector<std::filesystem::path> excludes;
    TrackAnalysisOptions options;
    ParamValues overrides;
    std::optional<double> texture_step;
    std::vector<std::wstring> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--param") == 0 && i + 1 < argc) {
            if (!parse_param(argv[++i], overrides)) {
                return 2;
            }
        } else if (std::wcscmp(argv[i], L"--kicks") == 0) {
            kick_features = true;
        } else if (std::wcscmp(argv[i], L"--tempo") == 0) {
            local_tempo = true;
        } else if (std::wcscmp(argv[i], L"--sections-report") == 0) {
            report = true;
        } else if (std::wcscmp(argv[i], L"--sections-eval") == 0) {
            evaluate = true;
        } else if (std::wcscmp(argv[i], L"--labels-stats") == 0) {
            label_statistics = true;
        } else if (std::wcscmp(argv[i], L"--drops") == 0) {
            drops = true;
        } else if (std::wcscmp(argv[i], L"--kick-coverage") == 0) {
            coverage = true;
        } else if (std::wcscmp(argv[i], L"--labelled-only") == 0) {
            coverage_labelled_only = true;
        } else if (std::wcscmp(argv[i], L"--texture-step") == 0 && i + 1 < argc) {
            texture_step = std::wcstod(argv[++i], nullptr);
        } else if (std::wcscmp(argv[i], L"--detail") == 0) {
            eval_flags.detail = true;
        } else if (std::wcscmp(argv[i], L"--split") == 0) {
            eval_flags.split = true;
        } else if (std::wcscmp(argv[i], L"--exclude") == 0 && i + 1 < argc) {
            excludes.emplace_back(argv[++i]);
        } else if (std::wcscmp(argv[i], L"--only") == 0 && i + 1 < argc) {
            const std::wstring set = argv[++i];
            if (set != L"tune" && set != L"held_out") {
                std::fprintf(stderr, "--only takes tune or held_out\n");
                return 2;
            }
            eval_flags.only = set == L"tune" ? 0 : 1;
        } else if (std::wcscmp(argv[i], L"--scores") == 0) {
            eval_flags.scores = true;
        } else if (std::wcscmp(argv[i], L"--json") == 0 && i + 1 < argc) {
            eval_flags.json = argv[++i];
        } else if (std::wcscmp(argv[i], L"--bars") == 0) {
            bar_table = true;
        } else if (std::wcscmp(argv[i], L"--stems") == 0) {
            options.source = AnalysisSource::stems;
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty()) {
        std::fprintf(
            stderr,
            "usage: psyight_analyse [--kicks] [--tempo] [--bars] [--stems] [--param <id>=<value>] "
            "<audio> [more "
            "audio...]\n");
        return 2;
    }
    if (texture_step && overrides.contains("section.texture_step")) {
        std::fprintf(stderr, "give --texture-step or --param section.texture_step, not both\n");
        return 2;
    }
    options.sections = section_params_from(overrides); // The defaults without --param.
    if (texture_step) {
        options.sections.texture_step = *texture_step;
    }
    if (!overrides.empty()) {
        std::printf("Parameters:");
        for (const auto &[id, value] : overrides) {
            std::printf(" %s=%g", id.c_str(), value);
        }
        std::printf("\n");
    }
    if (report) {
        return sections_report(paths, options.sections);
    }
    if (label_statistics) {
        return labels_stats(paths, excludes);
    }
    if (drops) {
        return drops_study(paths, excludes, options.sections);
    }
    if (coverage) {
        return kick_coverage(paths, excludes, options, coverage_labelled_only);
    }
    if (evaluate) {
        return sections_eval(paths, eval_flags, options.sections, overrides, excludes);
    }
    int status = 0;
    for (const std::wstring &path : paths) {
        status = std::max(status, analyse(path, options, kick_features, local_tempo, bar_table));
    }
    return status;
}
