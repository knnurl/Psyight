#include "output/show_check.h"

#include "engine/automation.h"
#include "engine/inputs.h"
#include "engine/limiters.h"

#include <algorithm>
#include <map>

namespace psyight {

namespace {

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
        dark += luminance(c) < kShowCheckDarkLuminance;
    }
    return frame.empty() ? 0.0 : static_cast<double>(dark) / static_cast<double>(frame.size());
}

double dark_share_by_channel(const Frame &frame) {
    size_t dark = 0;
    for (const Rgb &c : frame) {
        dark += std::max({c.r, c.g, c.b}) < kShowCheckDarkLevel;
    }
    return frame.empty() ? 0.0 : static_cast<double>(dark) / static_cast<double>(frame.size());
}

} // namespace

ShowCheckResult run_show_check(const ShowConfig &config, double duration_s,
                               const ShowCheckOptions &options) {
    ShowCheckResult result;
    if (config.analysis == nullptr) {
        return result;
    }
    const TrackAnalysis &analysis = *config.analysis;
    const SectionTimeline *timeline = config.sections.get();
    const size_t n_pixels = config.rig.pixels.size();
    const double fps = options.fps;

    ShowRenderer renderer;
    FlashMeter before(config.limiter);
    FlashMeter after(config.limiter);
    std::vector<double> current_a;
    std::vector<double> dark;
    std::vector<double> dark_before;
    std::vector<double> render_ms;

    // Per scene: frames played, lit pixel counts, and the scene alone.
    const size_t n_scenes = config.scenes.size();
    std::vector<std::vector<double>> scene_lit(n_scenes);
    std::vector<long long> scene_bright(n_scenes, 0);
    std::vector<int> scene_max(n_scenes, 0);
    std::vector<FlashMeter> scene_alone(n_scenes, FlashMeter(config.limiter));

    // Per strip group: its pixels, and running sums.
    std::map<std::string, std::vector<size_t>> group_pixels;
    for (size_t i = 0; i < n_pixels; ++i) {
        const auto s = static_cast<size_t>(config.rig.pixels[i].strip);
        if (s < config.rig.strips.size()) {
            for (const std::string &g : config.rig.strips[s].groups) {
                group_pixels[g].push_back(i);
            }
        }
    }
    std::vector<ShowCheckGroup> groups;
    std::vector<double> group_sum;
    std::vector<long long> group_dark;
    for (const auto &[name, pixels] : group_pixels) {
        ShowCheckGroup g;
        g.name = name;
        g.pixels = static_cast<int>(pixels.size());
        groups.push_back(g);
    }
    group_sum.assign(groups.size(), 0.0);
    group_dark.assign(groups.size(), 0);

    // Each layer alone, as a one-layer scene, with its own meter.
    std::vector<Scene> solo;
    std::vector<FlashMeter> solo_meters;
    if (options.per_layer) {
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
    const SceneVariants variants =
        all_variations(config.scenes, config.automation.vary, config.seed);
    const auto frames = static_cast<long long>(duration_s * fps);
    std::vector<int> pixel_max(n_pixels, 0);
    for (long long f = 0; f < frames; ++f) {
        const double t_s = static_cast<double>(f) / fps;
        EngineInputs inputs = inputs_at(analysis, config.grid, t_s, timeline);
        inputs.macros = config.macros;
        const SceneMix mix = automate(config.scenes, analysis, config.grid, inputs,
                                      config.automation, config.manual_scene, timeline);
        const Frame mixed = render_mix(config.scenes, mix, config.rig, inputs, config.palettes,
                                       config.seed, nullptr, &variants);
        before.add(mixed, t_s);
        dark_before.push_back(dark_share_by_channel(mixed));
        for (size_t i = 0; i < solo.size(); ++i) {
            solo_meters[i].add(
                render_scene(solo[i], config.rig, inputs, config.palettes, config.seed), t_s);
        }
        for (size_t s = 0; s < n_scenes; ++s) {
            scene_alone[s].add(
                render_scene(config.scenes[s], config.rig, inputs, config.palettes, config.seed),
                t_s);
        }
        const ShowFrame shown = renderer.render(config, t_s);
        if (options.on_frame) {
            options.on_frame(shown);
        }
        after.add(shown.frame, t_s);
        result.frames_clipped += shown.report.flash_limited;
        result.power_limited += shown.report.power_scale < 1.0;
        current_a.push_back(shown.report.current_a);
        dark.push_back(dark_share(shown.frame));
        render_ms.push_back(shown.render_ms);

        const std::vector<std::uint8_t> bytes =
            to_bytes(shown.frame, config.limiter.gamma, config.limiter.dither, shown.index);
        ShowCheckFrame row;
        row.t_s = t_s;
        row.section = shown.section;
        row.scene_to = shown.mix.to;
        row.current_a = shown.report.current_a;
        row.power_scale = shown.report.power_scale;
        row.flash_limited = shown.report.flash_limited;
        long long byte_sum = 0;
        int bright = 0;
        for (size_t i = 0; i < n_pixels && 3 * i + 2 < bytes.size(); ++i) {
            const int m = std::max({bytes[3 * i], bytes[3 * i + 1], bytes[3 * i + 2]});
            pixel_max[i] = m;
            byte_sum += bytes[3 * i] + bytes[3 * i + 1] + bytes[3 * i + 2];
            row.max_byte = std::max(row.max_byte, m);
            row.lit_pixels += m > 0;
            bright += m > kShowCheckBrightByte;
        }
        row.mean_byte =
            bytes.empty() ? 0.0 : static_cast<double>(byte_sum) / static_cast<double>(bytes.size());
        if (shown.mix.to >= 0 && static_cast<size_t>(shown.mix.to) < n_scenes) {
            const auto s = static_cast<size_t>(shown.mix.to);
            scene_lit[s].push_back(row.lit_pixels);
            scene_max[s] = std::max(scene_max[s], row.max_byte);
            scene_bright[s] += 4 * bright > static_cast<int>(n_pixels);
        }
        size_t gi = 0;
        for (const auto &[name, pixels] : group_pixels) {
            int gmax = 0;
            double gsum = 0.0;
            for (const size_t i : pixels) {
                gmax = std::max(gmax, pixel_max[i]);
                gsum += pixel_max[i];
            }
            groups[gi].max_byte = std::max(groups[gi].max_byte, gmax);
            group_sum[gi] += pixels.empty() ? 0.0 : gsum / static_cast<double>(pixels.size());
            group_dark[gi] += gmax == 0;
            ++gi;
        }
        if (options.keep_frames) {
            result.frames.push_back(std::move(row));
        }
    }
    const double n = static_cast<double>(std::max(1LL, frames));
    result.frame_count = frames;
    result.flashes_before = before.flashes();
    result.max_before_in_1s = before.max_in_any_second();
    result.flashes_after = after.flashes();
    result.max_after_in_1s = after.max_in_any_second();
    result.current_mean_a = mean(current_a);
    result.current_p95_a = quantile(current_a, 0.95);
    result.current_max_a = quantile(current_a, 1.0);
    result.dark_mean = mean(dark);
    result.dark_p10 = quantile(dark, 0.1);
    result.dark_p90 = quantile(dark, 0.9);
    result.dark_before_mean = mean(dark_before);
    result.dark_before_p10 = quantile(dark_before, 0.1);
    result.dark_before_p90 = quantile(dark_before, 0.9);
    result.render_p50_ms = quantile(render_ms, 0.5);
    result.render_p99_ms = quantile(render_ms, 0.99);
    result.render_max_ms = quantile(render_ms, 1.0);
    for (size_t s = 0; s < n_scenes; ++s) {
        ShowCheckScene sc;
        sc.name = config.scenes[s].name;
        sc.seconds = static_cast<double>(scene_lit[s].size()) / fps;
        sc.max_byte = scene_max[s];
        sc.median_lit_pixels = quantile(scene_lit[s], 0.5);
        sc.quarter_above_20 = scene_lit[s].empty() ? 0.0
                                                   : static_cast<double>(scene_bright[s]) /
                                                         static_cast<double>(scene_lit[s].size());
        sc.flashes_alone = scene_alone[s].flashes();
        sc.max_flashes_alone_in_1s = scene_alone[s].max_in_any_second();
        result.scenes.push_back(sc);
    }
    for (size_t g = 0; g < groups.size(); ++g) {
        groups[g].mean_byte = group_sum[g] / n;
        groups[g].dark_share = static_cast<double>(group_dark[g]) / n;
    }
    result.groups = std::move(groups);
    for (size_t i = 0; i < solo.size(); ++i) {
        result.layers.push_back(
            {solo[i].name, solo_meters[i].flashes(), solo_meters[i].max_in_any_second()});
    }
    return result;
}

} // namespace psyight
