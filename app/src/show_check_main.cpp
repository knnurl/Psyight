// psyight_show_check: renders a whole track as a show, offline and deterministically, and reports
// what the audience would get (SPEC 7.7 and the M3 exit's "flash limiter verified").
//
//   psyight_show_check [--stems] [--layers] [--project <file.psyproj>] [--pack <name>]
//                      [--fps <rate>] [--detected-sections] [--dump <file.csv>] [--groups]
//                      [--record <folder>] <audio>...
//
// For every frame at the given rate (default 60): the scene mix before the limiters and the
// show renderer's output after them (patch and packets to the virtual node included, as in the
// app; output/show_check.h). Reports flashes before and after the flash limiter (total and the
// most in any 1 s), how many frames it clipped, the supply current (mean, p95, max) and
// power-limited frames, the share of dark pixels (SPEC 7.7 suggests 30 to 50% dark most of the
// time) after the limiters by luminance and before them by the largest channel, time per scene
// with each scene's brightest byte, lit pixels and flashes alone, and the render cost per frame.
// Uses the labelled grid when "<audio>.labels.json" exists, and the labels' edited sections as
// the app does (--detected-sections: the analysis's own).
// --pack applies a built-in show pack to the project in memory (as the Show tab does); the
// project file is not written.
// --dump writes one CSV row per frame (bytes after to_bytes).
// --record writes the show as a session folder (io/session_record.h, as the app records one, with
// the project's macro values), for psyight_replay; the folder must not exist yet.
// --groups reports each strip group's mean and brightest byte and the share of frames it is dark.
// --layers also renders every layer of every scene alone over the whole track and counts its
// flashes, to find which effect needs the limiter.

#include "analysis/track_analysis.h"
#include "audio/audio_file.h"
#include "core/paths.h"
#include "engine/inputs.h"
#include "engine/presets.h"
#include "io/labels.h"
#include "io/presets.h"
#include "io/project.h"
#include "io/section_marks.h"
#include "io/session_record.h"
#include "output/show_check.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace psyight;

namespace {

struct Options {
    double fps = 60.0;
    bool per_layer = false;
    bool detected_sections = false;
    bool groups = false;
    std::filesystem::path dump;
    std::filesystem::path record;
};

int check(const std::filesystem::path &audio_path, const Project &project,
          TrackAnalysisOptions analysis_options, const Options &opt) {
    const std::filesystem::path labels_path = labels_path_for(audio_path);
    Labels labels;
    bool have_labels = false;
    if (std::filesystem::exists(labels_path)) {
        if (const Result<Labels> loaded = load_labels(labels_path); loaded) {
            labels = loaded.value();
            have_labels = true;
            const LabelGrid &g = labels.grid;
            analysis_options.grid =
                Grid{g.bpm, seconds_to_samples(g.first_downbeat_s, kAnalysisSampleRateHz),
                     kAnalysisSampleRateHz};
        }
    }
    const Result<TrackAnalysis> analysed = analyse_track(audio_path, analysis_options);
    if (!analysed) {
        std::fprintf(stderr, "%s: %s\n", path_to_utf8(audio_path.filename()).c_str(),
                     analysed.error().message.c_str());
        return 1;
    }
    auto analysis = std::make_shared<const TrackAnalysis>(std::move(analysed.value()));
    ShowConfig config;
    config.analysis = analysis;
    config.grid = analysis->grid;
    config.sample_rate_hz = analysis->sample_rate_hz;
    config.rig = rig_of(project);
    config.patch = patch_of(project);
    config.output = project.output;
    config.limiter = project.limiter;
    config.pixel_caps = group_caps_per_pixel(config.rig, project.group_caps);
    config.automation = project.automation;
    config.scenes = project.scenes;
    config.palettes = project.palettes;
    // The labels' edited sections, as the app's session builds them (Session::edited_sections).
    const bool edited =
        !opt.detected_sections && have_labels && labels.sections_edited && !labels.sections.empty();
    if (edited) {
        config.sections = std::make_shared<const SectionTimeline>(section_timeline(
            *analysis, config.grid, engine_sections(labels.sections, analysis->duration_s)));
    }

    ShowCheckOptions options;
    options.fps = opt.fps;
    options.per_layer = opt.per_layer;
    options.keep_frames = !opt.dump.empty();
    std::vector<std::uint8_t> recorded;
    if (!opt.record.empty()) { // As psyight_replay rebuilds the configuration.
        config.version = 1;
        config.macros = project.macro_values;
        recorded = frames_header(static_cast<int>(config.rig.pixels.size()));
        options.on_frame = [&](const ShowFrame &shown) {
            RecordedFrame f;
            f.t_s = shown.t_s;
            f.index = shown.index;
            f.config_version = config.version;
            f.rgb = shown.node_rgb;
            append_frame(recorded, f);
        };
    }
    const ShowCheckResult r = run_show_check(config, analysis->duration_s, options);
    if (!opt.record.empty()) {
        SessionMeta meta;
        meta.app_version = "psyight_show_check";
        meta.track = std::filesystem::absolute(audio_path);
        meta.source = analysis->source;
        meta.analysis_grid = analysis->grid;
        meta.pixels = static_cast<int>(config.rig.pixels.size());
        meta.frame_rate_hz = opt.fps;
        RuntimeState runtime;
        runtime.grid = config.grid;
        if (config.sections != nullptr) {
            runtime.sections = config.sections->sections;
        }
        const Result<std::string> meta_text = meta_json(meta);
        const Result<std::string> line = params_line(config.version, project, runtime);
        std::error_code ec;
        if (!meta_text || !line || std::filesystem::exists(opt.record) ||
            !std::filesystem::create_directories(opt.record, ec)) {
            std::fprintf(stderr, "cannot record into %s\n", path_to_utf8(opt.record).c_str());
            return 1;
        }
        std::ofstream(opt.record / "meta.json") << meta_text.value();
        std::ofstream(opt.record / "params.jsonl") << line.value();
        std::ofstream(opt.record / "events.jsonl") << events_jsonl(*analysis);
        std::ofstream(opt.record / "frames.bin", std::ios::binary)
            .write(reinterpret_cast<const char *>(recorded.data()),
                   static_cast<std::streamsize>(recorded.size()));
    }

    const double n = static_cast<double>(std::max(1LL, r.frame_count));
    std::printf(
        "%s (%s, %s sections%s): %.1f s, %lld frames at %.0f fps, %zu pixels, %.3f BPM\n",
        path_to_utf8(audio_path.filename()).c_str(),
        analysis->source == AnalysisSource::stems ? "stems" : "mix", edited ? "edited" : "detected",
        project.show_pack.empty() ? "" : (", pack " + project.show_pack).c_str(),
        analysis->duration_s, r.frame_count, opt.fps, config.rig.pixels.size(), config.grid.bpm);
    std::printf("  flashes before the limiter: %d (most in any 1 s: %d); after: %d (most in any "
                "1 s: %d, limit %.0f); frames clipped: %lld\n",
                r.flashes_before, r.max_before_in_1s, r.flashes_after, r.max_after_in_1s,
                config.limiter.max_flashes_per_s, r.frames_clipped);
    std::printf("  current: mean %.1f A, p95 %.1f A, max %.1f A (budget %.1f A); power limited "
                "%.1f%% of frames\n",
                r.current_mean_a, r.current_p95_a, r.current_max_a, config.limiter.budget_a,
                100.0 * static_cast<double>(r.power_limited) / n);
    std::printf("  dark pixels (luminance < %.2f): mean %.0f%%, p10 %.0f%%, p90 %.0f%%; before "
                "the limiters (largest channel < %.2f): mean %.0f%%, p10 %.0f%%, p90 %.0f%%\n",
                kShowCheckDarkLuminance, 100.0 * r.dark_mean, 100.0 * r.dark_p10,
                100.0 * r.dark_p90, kShowCheckDarkLevel, 100.0 * r.dark_before_mean,
                100.0 * r.dark_before_p10, 100.0 * r.dark_before_p90);
    std::printf("  scenes:");
    for (size_t i = 0; i < r.scenes.size(); ++i) {
        std::printf(" %s %.0f%%%s", r.scenes[i].name.c_str(),
                    100.0 * r.scenes[i].seconds / analysis->duration_s,
                    i + 1 < r.scenes.size() ? "," : "\n");
    }
    for (const ShowCheckScene &s : r.scenes) {
        std::printf("  scene %s: %.1f s, max byte %d, median lit %.0f px, over a quarter above "
                    "byte %d in %.0f%% of its frames; alone: %d flashes, most in any 1 s %d\n",
                    s.name.c_str(), s.seconds, s.max_byte, s.median_lit_pixels,
                    kShowCheckBrightByte, 100.0 * s.quarter_above_20, s.flashes_alone,
                    s.max_flashes_alone_in_1s);
    }
    std::printf("  render, limit, patch and send: p50 %.3f ms, p99 %.3f ms, max %.3f ms\n",
                r.render_p50_ms, r.render_p99_ms, r.render_max_ms);
    if (opt.groups) {
        for (const ShowCheckGroup &g : r.groups) {
            std::printf("  group %s (%d px): mean byte %.2f, max %d, fully dark %.0f%% of frames\n",
                        g.name.c_str(), g.pixels, g.mean_byte, g.max_byte, 100.0 * g.dark_share);
        }
    }
    for (const ShowCheckLayer &l : r.layers) {
        std::printf("  alone over the whole track: %s: %d flashes, most in any 1 s %d\n",
                    l.name.c_str(), l.flashes, l.max_in_1s);
    }
    if (!opt.dump.empty()) {
        std::ofstream f(opt.dump);
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", path_to_utf8(opt.dump).c_str());
            return 1;
        }
        f << "t_s,section,scene_to,max_byte,lit_pixels,mean_byte,current_a,power_scale,"
             "flash_limited\n";
        char line[256];
        for (const ShowCheckFrame &row : r.frames) {
            std::snprintf(line, sizeof line, "%.4f,%s,%d,%d,%d,%.4f,%.4f,%.5f,%d\n", row.t_s,
                          row.section.c_str(), row.scene_to, row.max_byte, row.lit_pixels,
                          row.mean_byte, row.current_a, row.power_scale, row.flash_limited ? 1 : 0);
            f << line;
        }
    }
    return r.max_after_in_1s > config.limiter.max_flashes_per_s ? 3 : 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    TrackAnalysisOptions analysis_options;
    Project project = default_project();
    Options opt;
    std::wstring pack;
    std::vector<std::wstring> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--stems") == 0) {
            analysis_options.source = AnalysisSource::stems;
        } else if (std::wcscmp(argv[i], L"--project") == 0 && i + 1 < argc) {
            const Result<Project> loaded = load_project(argv[++i]);
            if (!loaded) {
                std::fprintf(stderr, "%s\n", loaded.error().message.c_str());
                return 2;
            }
            project = loaded.value();
        } else if (std::wcscmp(argv[i], L"--pack") == 0 && i + 1 < argc) {
            pack = argv[++i];
        } else if (std::wcscmp(argv[i], L"--layers") == 0) {
            opt.per_layer = true;
        } else if (std::wcscmp(argv[i], L"--detected-sections") == 0) {
            opt.detected_sections = true;
        } else if (std::wcscmp(argv[i], L"--groups") == 0) {
            opt.groups = true;
        } else if (std::wcscmp(argv[i], L"--record") == 0 && i + 1 < argc) {
            opt.record = argv[++i];
        } else if (std::wcscmp(argv[i], L"--dump") == 0 && i + 1 < argc) {
            opt.dump = argv[++i];
        } else if (std::wcscmp(argv[i], L"--fps") == 0 && i + 1 < argc) {
            opt.fps = std::clamp(std::wcstod(argv[++i], nullptr), 1.0, 1000.0);
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty()) {
        std::fprintf(stderr,
                     "usage: psyight_show_check [--stems] [--layers] [--project <file.psyproj>] "
                     "[--pack <name>] [--fps <rate>] [--detected-sections] [--dump <file.csv>] "
                     "[--groups] [--record <folder>] <audio> [more audio...]\n");
        return 2;
    }
    if (!pack.empty()) { // Applied after the project, whatever the argument order.
        const std::string name = path_to_utf8(std::filesystem::path(pack));
        bool found = false;
        for (const ShowPack &p : builtin_show_packs()) {
            if (p.name == name) {
                apply_show_pack(project, p);
                found = true;
            }
        }
        if (!found) {
            std::fprintf(stderr, "no built-in show pack named %s\n", name.c_str());
            return 2;
        }
    }
    if (paths.size() > 1 && (!opt.dump.empty() || !opt.record.empty())) {
        std::fprintf(stderr, "--dump and --record take one audio file\n");
        return 2;
    }
    int status = 0;
    for (const std::wstring &path : paths) {
        status = std::max(status, check(path, project, analysis_options, opt));
    }
    return status;
}
