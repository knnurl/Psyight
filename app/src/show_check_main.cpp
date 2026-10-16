// psyight_show_check: renders a whole track as a show, offline and deterministically, and reports
// what the audience would get (SPEC 7.7 and the M3 exit's "flash limiter verified").
//
//   psyight_show_check [--stems] [--layers] [--project <file.psyproj>] [--fps <rate>] <audio>...
//
// For every frame at the given rate (default 60): the scene mix before the limiters and the
// show renderer's output after them (patch and packets to the virtual node included, as in the
// app). Reports flashes before and after the flash limiter (total and the most in any 1 s), how
// many frames it clipped, the supply current (mean, p95, max) and power-limited frames, the share
// of dark pixels (SPEC 7.7 suggests 30 to 50% dark most of the time), time per scene, and the
// render cost per frame. Uses the labelled grid when "<audio>.labels.json" exists, as the app does.
// --layers also renders every layer of every scene alone over the whole track and counts its
// flashes, to find which effect needs the limiter.

#include "analysis/track_analysis.h"
#include "audio/audio_file.h"
#include "core/paths.h"
#include "engine/automation.h"
#include "engine/inputs.h"
#include "engine/limiters.h"
#include "io/labels.h"
#include "io/project.h"
#include "output/show.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr double kDarkLuminance = 0.05;

double quantile(std::vector<double> values, double q) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>(q * static_cast<double>(values.size() - 1))];
}

double mean(const std::vector<double> &values) {
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double dark_share(const Frame &frame) {
    size_t dark = 0;
    for (const Rgb &c : frame) {
        dark += luminance(c) < kDarkLuminance;
    }
    return frame.empty() ? 0.0 : static_cast<double>(dark) / static_cast<double>(frame.size());
}

int check(const std::filesystem::path &audio_path, const Project &project,
          TrackAnalysisOptions options, double fps, bool per_layer) {
    const std::filesystem::path labels_path = labels_path_for(audio_path);
    if (std::filesystem::exists(labels_path)) {
        if (const Result<Labels> labels = load_labels(labels_path); labels) {
            const LabelGrid &g = labels.value().grid;
            options.grid =
                Grid{g.bpm, seconds_to_samples(g.first_downbeat_s, kAnalysisSampleRateHz),
                     kAnalysisSampleRateHz};
        }
    }
    const Result<TrackAnalysis> analysed = analyse_track(audio_path, options);
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
    config.automation = project.automation;

    ShowRenderer renderer;
    FlashMeter before(config.limiter);
    FlashMeter after(config.limiter);
    std::vector<double> current_a;
    std::vector<double> dark;
    std::vector<double> render_ms;
    std::vector<double> scene_s(config.scenes.size(), 0.0);
    long long power_limited = 0;
    long long clipped = 0;
    // Each layer alone, as a one-layer scene, with its own meter.
    std::vector<Scene> solo;
    std::vector<FlashMeter> solo_meters;
    if (per_layer) {
        for (const Scene &scene : config.scenes) {
            for (const Layer &layer : scene.layers) {
                Scene one;
                one.name = scene.name + " / " + layer.effect->name();
                one.palette = scene.palette;
                one.layers.push_back(layer);
                one.layers.back().blend = BlendMode::add;
                solo.push_back(std::move(one));
                solo_meters.emplace_back(config.limiter);
            }
        }
    }
    const auto frames = static_cast<long long>(analysis->duration_s * fps);
    for (long long f = 0; f < frames; ++f) {
        const double t_s = static_cast<double>(f) / fps;
        const EngineInputs inputs = inputs_at(*analysis, config.grid, t_s);
        const SceneMix mix = automate(config.scenes, *analysis, config.grid, inputs,
                                      config.automation, config.manual_scene);
        before.add(render_mix(config.scenes, mix, config.rig, inputs, config.palettes, config.seed),
                   t_s);
        for (size_t i = 0; i < solo.size(); ++i) {
            solo_meters[i].add(
                render_scene(solo[i], config.rig, inputs, config.palettes, config.seed), t_s);
        }
        const ShowFrame shown = renderer.render(config, t_s);
        after.add(shown.frame, t_s);
        clipped += shown.report.flash_limited;
        power_limited += shown.report.power_scale < 1.0;
        current_a.push_back(shown.report.current_a);
        dark.push_back(dark_share(shown.frame));
        render_ms.push_back(shown.render_ms);
        if (shown.mix.to >= 0 && shown.mix.to < static_cast<int>(scene_s.size())) {
            scene_s[static_cast<size_t>(shown.mix.to)] += 1.0 / fps;
        }
    }
    const double n = static_cast<double>(std::max(1LL, frames));
    std::printf("%s (%s): %.1f s, %lld frames at %.0f fps, %zu pixels, %.3f BPM\n",
                path_to_utf8(audio_path.filename()).c_str(),
                analysis->source == AnalysisSource::stems ? "stems" : "mix", analysis->duration_s,
                frames, fps, config.rig.pixels.size(), config.grid.bpm);
    std::printf("  flashes before the limiter: %d (most in any 1 s: %d); after: %d (most in any "
                "1 s: %d, limit %.0f); frames clipped: %lld\n",
                before.flashes(), before.max_in_any_second(), after.flashes(),
                after.max_in_any_second(), config.limiter.max_flashes_per_s, clipped);
    std::printf("  current: mean %.1f A, p95 %.1f A, max %.1f A (budget %.1f A); power limited "
                "%.1f%% of frames\n",
                mean(current_a), quantile(current_a, 0.95), quantile(current_a, 1.0),
                config.limiter.budget_a, 100.0 * static_cast<double>(power_limited) / n);
    std::printf("  dark pixels (luminance < %.2f): mean %.0f%%, p10 %.0f%%, p90 %.0f%%\n",
                kDarkLuminance, 100.0 * mean(dark), 100.0 * quantile(dark, 0.1),
                100.0 * quantile(dark, 0.9));
    std::printf("  scenes:");
    for (size_t i = 0; i < scene_s.size(); ++i) {
        std::printf(" %s %.0f%%%s", config.scenes[i].name.c_str(),
                    100.0 * scene_s[i] / analysis->duration_s, i + 1 < scene_s.size() ? "," : "\n");
    }
    std::printf("  render, limit, patch and send: p50 %.3f ms, p99 %.3f ms, max %.3f ms\n",
                quantile(render_ms, 0.5), quantile(render_ms, 0.99), quantile(render_ms, 1.0));
    for (size_t i = 0; i < solo.size(); ++i) {
        std::printf("  alone over the whole track: %s: %d flashes, most in any 1 s %d\n",
                    solo[i].name.c_str(), solo_meters[i].flashes(),
                    solo_meters[i].max_in_any_second());
    }
    return after.max_in_any_second() > config.limiter.max_flashes_per_s ? 3 : 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    TrackAnalysisOptions options;
    Project project = default_project();
    double fps = 60.0;
    bool per_layer = false;
    std::vector<std::wstring> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--stems") == 0) {
            options.source = AnalysisSource::stems;
        } else if (std::wcscmp(argv[i], L"--project") == 0 && i + 1 < argc) {
            const Result<Project> loaded = load_project(argv[++i]);
            if (!loaded) {
                std::fprintf(stderr, "%s\n", loaded.error().message.c_str());
                return 2;
            }
            project = loaded.value();
        } else if (std::wcscmp(argv[i], L"--layers") == 0) {
            per_layer = true;
        } else if (std::wcscmp(argv[i], L"--fps") == 0 && i + 1 < argc) {
            fps = std::clamp(std::wcstod(argv[++i], nullptr), 1.0, 1000.0);
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty()) {
        std::fprintf(stderr, "usage: psyight_show_check [--stems] [--layers] [--project "
                             "<file.psyproj>] [--fps <rate>] <audio> [more audio...]\n");
        return 2;
    }
    int status = 0;
    for (const std::wstring &path : paths) {
        status = std::max(status, check(path, project, options, fps, per_layer));
    }
    return status;
}
