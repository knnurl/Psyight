// psyight_live_check: runs the live analysis (analysis/live_analysis.h) over a file as if it were
// arriving live, and measures it against the file-mode analysis of the same audio.
//
//   psyight_live_check <audio> [--block <samples>] [--student <weights>] [--trace <from s> <to s>]
//                      [--show <project.psyproj>]
//
// Feeds the decoded mono mix in blocks (default 480 samples, a 10 ms WASAPI period) and reports:
// kicks against the file analysis's (stems when they exist) at +-20 ms; when the tempo was
// measured and the loop locked; the tempo and beat phase against the file grid (the labelled one
// when the track has labels) once locked, and how often the bar position agrees; the live bass
// notes against the labels' complete regions and against the file-mode student; the live section
// changes beside the file sections; and the processing cost per hop. --trace prints the beat state
// at each kick between two times; --show renders the project's show from the live state as live
// mode does and prints a timeline (between --trace's times when given). Local tracks only.

#include "analysis/live_analysis.h"
#include "analysis/scoring.h"
#include "analysis/track_analysis.h"
#include "audio/audio_file.h"
#include "core/paths.h"
#include "engine/automation.h"
#include "engine/inputs.h"
#include "engine/limiters.h"
#include "engine/variations.h"
#include "io/labels.h"
#include "io/project.h"
#include "io/stems.h"
#include "ml/student.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

using namespace psyight;

namespace {

std::vector<BassNoteLabel> as_events(const std::vector<double> &times_s) {
    std::vector<BassNoteLabel> out;
    for (const double t : times_s) {
        out.push_back({t, t + 0.01, 0, 0.0, false, false, 1.0});
    }
    return out;
}

void print_notes(const char *name, const NoteScore &s) {
    std::printf("  %s: %d reference, %d live, %d matched: P %.3f, R %.3f, F %.3f; pitch %.3f; "
                "offsets |median| %.1f ms; gate MAE %.3f\n",
                name, s.reference_count, s.estimate_count, s.matched_count, s.onset_precision,
                s.onset_recall, s.onset_f, s.pitch_accuracy, 1e3 * s.offset_error_abs_median_s,
                s.gate_ratio_mae);
}

// The lit share of a frame: pixels at or above luminance 0.05.
double lit_share(const Frame &frame) {
    size_t lit = 0;
    for (const Rgb &c : frame) {
        lit += luminance(c) >= 0.05;
    }
    return frame.empty() ? 0.0 : static_cast<double>(lit) / static_cast<double>(frame.size());
}

double mean_luminance(const Frame &frame) {
    double sum = 0.0;
    for (const Rgb &c : frame) {
        sum += luminance(c);
    }
    return frame.empty() ? 0.0 : sum / static_cast<double>(frame.size());
}

// --show: the project's show rendered from the live states as live mode renders it (automation,
// the scene mix and the fade on silence; no limiters), at 60 fps. Prints a line every 2 s (only
// between --trace's times when given): the section, the scene, the automation's brightness, the
// intensity and riser, the mean luminance and lit share, and each of the scene's layers alone.
int show_timeline(const std::vector<LiveState> &states, const std::filesystem::path &path,
                  double from_s, double to_s, double duration_s) {
    const Result<Project> loaded = load_project(path);
    if (!loaded) {
        std::fprintf(stderr, "%s\n", loaded.error().message.c_str());
        return 1;
    }
    const Project &project = loaded.value();
    const Rig rig = rig_of(project);
    constexpr std::uint64_t kSeed = 1; // ShowConfig's.
    const SceneVariants variants = all_variations(project.scenes, project.automation.vary, kSeed);
    constexpr double kSilenceFadeS = 2.0; // As output/show.cpp.
    constexpr double kFps = 60.0;
    constexpr double kEveryS = 2.0;
    if (from_s < 0.0) {
        from_s = 0.0;
        to_s = duration_s;
    }
    std::printf("  show from the live state (%s), every %.0f s from %.0f to %.0f s:\n",
                path_to_utf8(path.filename()).c_str(), kEveryS, from_s, to_s);
    std::printf("    time  section    scene (weight)        bright intens riser  lum   lit  "
                "layers alone (lum)\n");
    ModMemory memory;
    size_t k = 0;
    double next_print_s = from_s;
    for (long long f = 0; static_cast<double>(f) / kFps <= std::min(to_s, duration_s); ++f) {
        const double t_s = static_cast<double>(f) / kFps;
        while (k + 1 < states.size() && states[k + 1].analysed_s <= t_s) {
            ++k;
        }
        const LiveState &s = states[k];
        EngineInputs inputs = inputs_at(s, t_s);
        inputs.macros = project.macro_values;
        const SceneMix mix = automate_live(project.scenes, s, inputs, project.automation, -1);
        Frame frame = render_mix(project.scenes, mix, rig, inputs, project.palettes, kSeed, &memory,
                                 &variants);
        if (s.silent) {
            const double fade =
                std::clamp(1.0 - (t_s - s.silent_since_s) / kSilenceFadeS, 0.0, 1.0);
            for (Rgb &c : frame) {
                c = scaled(c, fade);
            }
        }
        if (t_s + 1e-9 < next_print_s) {
            continue;
        }
        next_print_s += kEveryS;
        const bool in_scene = mix.to >= 0 && mix.to < static_cast<int>(project.scenes.size());
        char scene[40];
        std::snprintf(scene, sizeof(scene), "%s (%.2f)",
                      in_scene ? project.scenes[static_cast<size_t>(mix.to)].name.c_str() : "none",
                      mix.from >= 0 ? mix.to_weight : 1.0);
        std::printf("    %5.0f  %-9s  %-20s  %5.2f  %5.2f  %4.2f  %5.3f  %3.0f%% ", t_s,
                    s.silent ? "silent" : live_section_name(s.section), scene, mix.brightness,
                    inputs.intensity, inputs.riser, mean_luminance(frame),
                    100.0 * lit_share(frame));
        if (in_scene) {
            const Scene &whole = project.scenes[static_cast<size_t>(mix.to)];
            for (const Layer &layer : whole.layers) {
                Scene one;
                one.palette = whole.palette;
                one.layers.push_back(layer);
                one.layers.back().blend = BlendMode::add;
                std::printf(
                    " %s %.3f", layer.effect->name(),
                    mean_luminance(render_scene(one, rig, inputs, project.palettes, kSeed)));
            }
        }
        std::printf("\n");
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    std::filesystem::path audio_path;
    std::filesystem::path weights = "data/m4/student.weights";
    std::filesystem::path project_path;
    int block = 480;
    double trace_from_s = -1.0, trace_to_s = -1.0;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--block") == 0 && i + 1 < argc) {
            block = std::max(1, _wtoi(argv[++i]));
        } else if (std::wcscmp(argv[i], L"--trace") == 0 && i + 2 < argc) {
            trace_from_s = _wtof(argv[++i]);
            trace_to_s = _wtof(argv[++i]);
        } else if (std::wcscmp(argv[i], L"--student") == 0 && i + 1 < argc) {
            weights = argv[++i];
        } else if (std::wcscmp(argv[i], L"--show") == 0 && i + 1 < argc) {
            project_path = argv[++i];
        } else {
            audio_path = argv[i];
        }
    }
    if (audio_path.empty()) {
        std::fprintf(stderr, "usage: psyight_live_check <audio> [--block <samples>] "
                             "[--student <weights>] [--trace <from s> <to s>] [--show "
                             "<project>]\n");
        return 2;
    }
    const Result<StudentModel> model = load_student(weights);
    Result<AudioBuffer> audio = decode_audio_file(audio_path);
    if (!model || !audio) {
        std::fprintf(stderr, "%s\n", (!model ? model.error() : audio.error()).message.c_str());
        return 1;
    }
    const int sr = audio.value().sample_rate_hz;
    const std::vector<float> mono = mono_mix(audio.value());

    // The file-mode reference: stems when they exist, the labelled grid when there are labels.
    TrackAnalysisOptions options;
    options.source = analysis_stems_exist(stem_paths_for(audio_path)) ? AnalysisSource::stems
                                                                      : AnalysisSource::mix;
    Labels labels;
    const bool labelled = std::filesystem::exists(labels_path_for(audio_path));
    if (labelled) {
        if (Result<Labels> l = load_labels(labels_path_for(audio_path)); l) {
            labels = l.value();
            options.grid =
                Grid{labels.grid.bpm, seconds_to_samples(labels.grid.first_downbeat_s, sr), sr};
        }
    }
    const Result<TrackAnalysis> file = analyse_track(mono, sr, audio_path, options);
    if (!file) {
        std::fprintf(stderr, "file analysis: %s\n", file.error().message.c_str());
        return 1;
    }
    const TrackAnalysis &f = file.value();

    LiveAnalyser live;
    if (Result<void> r = live.prepare(sr, model.value()); !r) {
        std::fprintf(stderr, "%s\n", r.error().message.c_str());
        return 1;
    }
    live.set_recording(true);
    std::vector<double> block_us;
    for (size_t at = 0; at < mono.size(); at += static_cast<size_t>(block)) {
        const size_t n = std::min(static_cast<size_t>(block), mono.size() - at);
        const auto start = std::chrono::steady_clock::now();
        live.process(std::span(mono).subspan(at, n));
        block_us.push_back(
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                .count());
    }
    const double duration_s = static_cast<double>(mono.size()) / sr;
    std::sort(block_us.begin(), block_us.end());
    double total_us = 0.0;
    for (const double us : block_us) {
        total_us += us;
    }
    std::printf("%s: %.1f s, live analysis in blocks of %d samples; reference: file mode from the "
                "%s%s\n",
                path_to_utf8(audio_path.filename()).c_str(), duration_s, block,
                options.source == AnalysisSource::stems ? "stems" : "mix",
                labelled ? ", labelled grid" : ", detected grid");
    std::printf("  cost: %.2f%% of real time; per block median %.0f us, p99 %.0f us, max %.0f us\n",
                100.0 * total_us * 1e-6 / duration_s, block_us[block_us.size() / 2],
                block_us[block_us.size() * 99 / 100], block_us.back());
    std::printf("  decision latency: kicks %.1f ms, bass onsets %.1f ms (plus up to one %.1f ms "
                "hop)\n",
                1e3 * (live.state().latency_s - 2.0 * live.frame_s()), 1e3 * live.state().latency_s,
                1e3 * live.frame_s());

    // --trace a b: the beat state at every kick between a and b seconds.
    long long kicks_seen = 0;
    for (const LiveState &s : live.recorded_states()) {
        if (s.kicks != kicks_seen) {
            kicks_seen = s.kicks;
            if (s.last_kick_s >= trace_from_s && s.last_kick_s <= trace_to_s) {
                std::printf("  kick %.3f s: beat %lld, streak %d, last run beat %lld, error %+.1f "
                            "ms, %s, %s\n",
                            s.last_kick_s, s.last_kick_beat, s.kick_streak, s.last_run_beat,
                            s.last_error_ms, s.locked ? "locked" : "not locked",
                            live_section_name(s.section));
            }
        }
    }

    // Kicks.
    const NoteScore kicks =
        score_notes(as_events(f.bass.kick_times_s), as_events(live.recorded_kicks()), 1.0, {});
    std::printf("  kicks: %d file, %d live, %d matched within 20 ms: P %.3f, R %.3f, F %.3f\n",
                kicks.reference_count, kicks.estimate_count, kicks.matched_count,
                kicks.onset_precision, kicks.onset_recall, kicks.onset_f);

    // Beat: after lock, the predicted beat against the file grid.
    const Grid &grid = f.grid;
    const double file_beat_s = 60.0 / grid.bpm;
    double tempo_at_s = -1.0, lock_at_s = -1.0;
    std::vector<double> phase_errors_ms;
    long long bar_checks = 0, bar_agree = 0;
    std::array<long long, 4> bar_offsets{}; // Live beat in bar minus file's, mod 4.
    for (const LiveState &s : live.recorded_states()) {
        if (s.tempo_known && tempo_at_s < 0.0) {
            tempo_at_s = s.analysed_s;
        }
        if (s.locked && lock_at_s < 0.0) {
            lock_at_s = s.analysed_s;
        }
        if (!s.locked || s.frames % 16 != 0) {
            continue;
        }
        const double t = s.analysed_s;
        const EngineInputs in = inputs_at(s, t);
        const double live_beat = static_cast<double>(in.beat_index) + in.beat_phase;
        const double file_slot = grid.slot_position(seconds_to_samples(t, sr));
        const double file_beat = file_slot / kSlotsPerBeat;
        const double phase = std::remainder(live_beat - file_beat, 1.0); // In beats.
        phase_errors_ms.push_back(1e3 * phase * file_beat_s);
        const auto file_beat_in_bar =
            static_cast<int>(((static_cast<long long>(std::floor(file_beat)) % 4) + 4) % 4);
        // Compare bar positions only where the two agree on the beat (half a beat).
        if (std::abs(phase) < 0.25) {
            const int live_in_bar = static_cast<int>(
                ((static_cast<long long>(std::llround(std::floor(live_beat - phase))) -
                  s.downbeat_beat) %
                     4 +
                 4) %
                4);
            ++bar_checks;
            bar_agree += live_in_bar == file_beat_in_bar;
            ++bar_offsets[static_cast<size_t>(((live_in_bar - file_beat_in_bar) % 4 + 4) % 4)];
        }
    }
    const LiveState &end = live.state();
    std::printf("  tempo measured at %.1f s, locked at %.1f s; final %.3f BPM (file %.3f)\n",
                tempo_at_s, lock_at_s, end.tempo_known ? 60.0 / end.period_s : 0.0, grid.bpm);
    if (!phase_errors_ms.empty()) {
        std::vector<double> abs_ms;
        for (const double e : phase_errors_ms) {
            abs_ms.push_back(std::abs(e));
        }
        std::printf("  beat phase after lock (live minus file, %zu checks): median %+.1f ms, "
                    "|median| %.1f ms, |p95| %.1f ms; within 20 ms %.1f%%; bar position agrees "
                    "%.1f%%\n",
                    phase_errors_ms.size(), percentile(phase_errors_ms, 0.5),
                    percentile(abs_ms, 0.5), percentile(abs_ms, 0.95),
                    100.0 *
                        static_cast<double>(std::count_if(abs_ms.begin(), abs_ms.end(),
                                                          [](double e) { return e <= 20.0; })) /
                        static_cast<double>(abs_ms.size()),
                    bar_checks > 0 ? 100.0 * bar_agree / bar_checks : 0.0);
        std::printf("  bar offset (live minus file, beats): 0: %lld, +1: %lld, +2: %lld, +3: %lld; "
                    "final downbeat from %s\n",
                    bar_offsets[0], bar_offsets[1], bar_offsets[2], bar_offsets[3],
                    end.downbeat_from_novelty ? "novelty" : "a kick break or the first lock");
    }

    // Bass.
    const std::vector<BassNoteLabel> live_notes = to_labels(live.recorded_notes());
    if (labelled) {
        const TrackScore score = score_track(labels, live_notes, {});
        for (const RegionScore &r : score.regions) {
            char name[96];
            std::snprintf(name, sizeof(name), "bass vs labels %.1f to %.1f s", r.region.start_s,
                          r.region.end_s);
            print_notes(name, r.score);
        }
    }
    if (Result<StudentResult> offline = analyse_student(model.value(), mono, sr, grid); offline) {
        print_notes("bass vs the file-mode student (whole track)",
                    score_notes(to_labels(offline.value().notes), live_notes,
                                grid.slot_length_samples() / sr, {}));
    }

    // Sections.
    std::printf("  file sections:");
    for (const DetectedSection &s : f.sections) {
        std::printf(" %s@%.0f", s.label.c_str(), s.start_s);
    }
    std::printf("\n  live sections:");
    LiveSection last = LiveSection::intro;
    int count = 0;
    for (const LiveState &s : live.recorded_states()) {
        if (s.section_count != count) {
            count = s.section_count;
            last = s.section;
            std::printf(" %s@%.0f(+%.0f)", live_section_name(last), s.section_start_s,
                        s.analysed_s - s.section_start_s);
        }
    }
    std::printf("\n  (live: start@time(+seconds until it was known))\n");
    if (!project_path.empty()) {
        return show_timeline(live.recorded_states(), project_path, trace_from_s, trace_to_s,
                             duration_s);
    }
    return 0;
}
