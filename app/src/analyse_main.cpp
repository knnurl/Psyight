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
// phrase grid lines up with a labelled one; then the totals over the tracks.

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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <filesystem>
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
            std::printf(" %s %.0f-%.0f s (%.2f, %s);", section.label.c_str(), section.start_s,
                        section.end_s, section.confidence,
                        kEnergy[std::clamp(section.energy, 0, 2)]);
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

std::optional<SectionStats> section_report(const std::filesystem::path &path) {
    TrackAnalysisOptions options;
    options.source =
        analysis_stems_exist(stem_paths_for(path)) ? AnalysisSource::stems : AnalysisSource::mix;
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
            section_bars(a.mix_features, a.beats, &a.residual);
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

int sections_report(const std::vector<std::wstring> &paths) {
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
        if (const std::optional<SectionStats> s = section_report(file); s) {
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

int sections_eval(const std::vector<std::wstring> &paths) {
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
    SectionScore total;
    int phrase_tracks = 0;
    int phrase_agree = 0;
    int scored = 0;
    for (const TrackEntry &track : tracks) {
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
        const Result<TrackAnalysis> analysis = analyse_track(track.path, options);
        if (!analysis) {
            std::printf("%-50s  %s\n", track.name.substr(0, 50).c_str(),
                        analysis.error().message.c_str());
            continue;
        }
        const TrackAnalysis &a = analysis.value();
        const std::vector<SectionLabel> detected = sections_from_detection(a.sections, l.grid);
        const SectionScore s = score_sections(l.sections, detected, l.grid, a.duration_s);
        total += s;
        ++scored;
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
            ++phrase_tracks;
            phrase_agree += shift == 0;
            phrase = shift == 0 ? "phrase grid agrees"
                                : "phrase grid off by " + std::to_string(shift) + " bars";
        }
        std::printf("%-50s  boundaries P %.2f R %.2f F %.2f (%d labelled, %d detected)  bars %.0f%%"
                    "  %s  (%s)\n",
                    track.name.substr(0, 50).c_str(), s.precision(), s.recall(), s.f1(),
                    s.labelled_boundaries, s.detected_boundaries, 100.0 * s.bar_accuracy(),
                    phrase.c_str(), options.source == AnalysisSource::stems ? "stems" : "mix");
        std::fflush(stdout);
    }
    if (scored == 0) {
        std::printf("No track marked done (label sections in the app: View > Label sections, then "
                    "Done).\n");
        return 1;
    }
    std::printf("\nOver %d track%s: boundaries within a bar P %.2f R %.2f F %.2f; %.0f%% of bars "
                "labelled alike; phrase grid agrees on %d of %d labelled\n",
                scored, scored == 1 ? "" : "s", total.precision(), total.recall(), total.f1(),
                100.0 * total.bar_accuracy(), phrase_agree, phrase_tracks);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    bool kick_features = false;
    bool local_tempo = false;
    bool bar_table = false;
    bool report = false;
    bool evaluate = false;
    TrackAnalysisOptions options;
    std::vector<std::wstring> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--kicks") == 0) {
            kick_features = true;
        } else if (std::wcscmp(argv[i], L"--tempo") == 0) {
            local_tempo = true;
        } else if (std::wcscmp(argv[i], L"--sections-report") == 0) {
            report = true;
        } else if (std::wcscmp(argv[i], L"--sections-eval") == 0) {
            evaluate = true;
        } else if (std::wcscmp(argv[i], L"--bars") == 0) {
            bar_table = true;
        } else if (std::wcscmp(argv[i], L"--stems") == 0) {
            options.source = AnalysisSource::stems;
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty()) {
        std::fprintf(stderr,
                     "usage: psyight_analyse [--kicks] [--tempo] [--bars] [--stems] <audio> [more "
                     "audio...]\n");
        return 2;
    }
    if (report) {
        return sections_report(paths);
    }
    if (evaluate) {
        return sections_eval(paths);
    }
    int status = 0;
    for (const std::wstring &path : paths) {
        status = std::max(status, analyse(path, options, kick_features, local_tempo, bar_table));
    }
    return status;
}
