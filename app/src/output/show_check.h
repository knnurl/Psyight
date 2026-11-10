// A whole track rendered as a show, offline and deterministically, with what the audience would
// get measured frame by frame (psyight_show_check, and the show baseline test). For every frame
// at `fps`: the scene mix before the limiters and the show renderer's output after them (patch
// and packets to the virtual node included, as in the app). Bytes are after to_bytes.
#pragma once

#include "analysis/track_analysis.h"
#include "output/show.h"

#include <functional>
#include <string>
#include <vector>

namespace psyight {

struct ShowCheckOptions {
    double fps = 60.0;
    bool per_layer = false;   // Every layer of every scene alone over the track, its flashes.
    bool keep_frames = false; // Fill ShowCheckResult::frames (the --dump rows).
    std::function<void(const ShowFrame &)> on_frame; // Each frame as shown (--record).
};

// One frame as shown.
struct ShowCheckFrame {
    double t_s = 0.0;
    std::string section;
    int scene_to = -1;
    int max_byte = 0;
    int lit_pixels = 0;     // Any channel byte above 0.
    double mean_byte = 0.0; // Over every channel of every pixel.
    double current_a = 0.0;
    double power_scale = 1.0;
    bool flash_limited = false;
};

// Per scene, over the frames it played (mix.to), and the scene alone over the whole track.
struct ShowCheckScene {
    std::string name;
    double seconds = 0.0;
    int max_byte = 0;
    double median_lit_pixels = 0.0;
    double quarter_above_20 = 0.0; // Share of its frames with over a quarter of the rig above 20.
    int flashes_alone = 0;         // The scene alone over the whole track (its own palette).
    int max_flashes_alone_in_1s = 0;
};

// Per strip group, over the whole track: the brightest channel of each pixel.
struct ShowCheckGroup {
    std::string name;
    int pixels = 0;
    double mean_byte = 0.0;
    int max_byte = 0;
    double dark_share = 0.0; // Frames in which the group is fully dark.
};

struct ShowCheckLayer {
    std::string name;
    int flashes = 0;
    int max_in_1s = 0;
};

struct ShowCheckResult {
    long long frame_count = 0;
    int flashes_before = 0;
    int max_before_in_1s = 0;
    int flashes_after = 0;
    int max_after_in_1s = 0;
    long long frames_clipped = 0;
    long long power_limited = 0;
    double current_mean_a = 0.0;
    double current_p95_a = 0.0;
    double current_max_a = 0.0;
    // Luminance under kShowCheckDarkLuminance after the limiters (the older figure).
    double dark_mean = 0.0;
    double dark_p10 = 0.0;
    double dark_p90 = 0.0;
    // Largest channel under kShowCheckDarkLevel in the scene mix before the limiters.
    double dark_before_mean = 0.0;
    double dark_before_p10 = 0.0;
    double dark_before_p90 = 0.0;
    double render_p50_ms = 0.0;
    double render_p99_ms = 0.0;
    double render_max_ms = 0.0;
    std::vector<ShowCheckScene> scenes;
    std::vector<ShowCheckGroup> groups;
    std::vector<ShowCheckLayer> layers; // With per_layer.
    std::vector<ShowCheckFrame> frames; // With keep_frames.
};

inline constexpr double kShowCheckDarkLuminance = 0.05;
inline constexpr double kShowCheckDarkLevel = 0.05;
inline constexpr int kShowCheckBrightByte = 20;

// config: as the app would publish it (analysis set; sections set for edited sections).
ShowCheckResult run_show_check(const ShowConfig &config, double duration_s,
                               const ShowCheckOptions &options);

} // namespace psyight
