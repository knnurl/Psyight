// psyight_replay: replays a recorded session and checks that it reproduces exactly (SPEC 4.2
// determinism, SPEC 11 replay).
//
//   psyight_replay <session folder>
//
// Re-analyses the recorded track with the recorded source and grid, rebuilds each recorded show
// configuration from params.jsonl, re-renders every recorded frame at its render time in order,
// and compares the bytes with frames.bin. Reports matching frames, the first mismatch and the
// largest byte difference. Gaps in the frame indices (frames the recorder dropped) are reported,
// since the limiters' history differs after one. Exits 0 when every frame matches.

#include "analysis/track_analysis.h"
#include "core/paths.h"
#include "io/session_record.h"
#include "io/text_file.h"
#include "output/show.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>

using namespace psyight;

namespace {

int replay(const std::filesystem::path &folder) {
    const Result<std::string> meta_text = read_text_file(folder / "meta.json");
    if (!meta_text) {
        std::fprintf(stderr, "%s\n", meta_text.error().message.c_str());
        return 2;
    }
    const Result<SessionMeta> meta = parse_meta_json(meta_text.value());
    const Result<std::string> params_text = read_text_file(folder / "params.jsonl");
    const Result<FramesFile> frames = read_frames(folder / "frames.bin");
    if (!meta || !params_text || !frames) {
        std::fprintf(stderr, "%s\n",
                     !meta          ? meta.error().message.c_str()
                     : !params_text ? params_text.error().message.c_str()
                                    : frames.error().message.c_str());
        return 2;
    }
    const Result<std::vector<ParamsEntry>> params = parse_params_jsonl(params_text.value());
    if (!params) {
        std::fprintf(stderr, "%s\n", params.error().message.c_str());
        return 2;
    }
    std::map<std::uint64_t, std::shared_ptr<ShowConfig>> configs;
    TrackAnalysisOptions options;
    options.source = meta.value().source;
    options.grid = meta.value().analysis_grid;
    const Result<TrackAnalysis> analysed = analyse_track(meta.value().track, options);
    if (!analysed) {
        std::fprintf(stderr, "%s: %s\n", path_to_utf8(meta.value().track).c_str(),
                     analysed.error().message.c_str());
        return 2;
    }
    auto analysis = std::make_shared<const TrackAnalysis>(analysed.value());
    for (const ParamsEntry &e : params.value()) {
        auto c = std::make_shared<ShowConfig>();
        c->version = e.version;
        c->analysis = analysis;
        c->grid = e.runtime.grid;
        c->sample_rate_hz = analysis->sample_rate_hz;
        c->latency_offset_s = 1e-3 * e.project.latency_offset_ms;
        c->rig = rig_of(e.project);
        c->patch = patch_of(e.project);
        c->output = e.project.output;
        c->limiter = e.project.limiter;
        c->pixel_caps = group_caps_per_pixel(c->rig, e.project.group_caps);
        c->automation = e.project.automation;
        c->scenes = e.project.scenes;
        c->palettes = e.project.palettes;
        c->manual_scene = e.runtime.manual_scene;
        c->blackout = e.runtime.blackout;
        c->hold = e.runtime.hold;
        configs[e.version] = c;
    }

    ShowRenderer renderer;
    long long matched = 0;
    long long compared = 0;
    long long first_mismatch = -1;
    int largest_difference = 0;
    long long gaps = 0;
    long long missing_config = 0;
    std::int64_t previous_index = -1;
    for (const RecordedFrame &f : frames.value().frames) {
        if (previous_index >= 0 && f.index != previous_index + 1) {
            ++gaps;
        }
        previous_index = f.index;
        const auto c = configs.find(f.config_version);
        if (c == configs.end()) {
            ++missing_config;
            continue;
        }
        const ShowFrame shown = renderer.render(*c->second, f.t_s);
        ++compared;
        int difference = shown.node_rgb.size() == f.rgb.size() ? 0 : 255;
        for (size_t i = 0; i < f.rgb.size() && i < shown.node_rgb.size(); ++i) {
            difference = std::max(difference, std::abs(int{shown.node_rgb[i]} - int{f.rgb[i]}));
        }
        if (difference == 0) {
            ++matched;
        } else if (first_mismatch < 0) {
            first_mismatch = f.index;
        }
        largest_difference = std::max(largest_difference, difference);
    }
    std::printf("%s: %s, %zu frames recorded, %zu configurations\n",
                path_to_utf8(folder.filename()).c_str(),
                path_to_utf8(meta.value().track.filename()).c_str(), frames.value().frames.size(),
                params.value().size());
    std::printf("  replayed %lld, identical %lld; largest byte difference %d", compared, matched,
                largest_difference);
    if (first_mismatch >= 0) {
        std::printf("; first mismatch at frame %lld", first_mismatch);
    }
    std::printf("\n");
    if (gaps > 0 || missing_config > 0) {
        std::printf("  %lld gaps in the frame indices (dropped frames), %lld frames without a "
                    "recorded configuration\n",
                    gaps, missing_config);
    }
    return compared > 0 && matched == compared ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: psyight_replay <session folder>\n");
        return 2;
    }
    return replay(argv[1]);
}
