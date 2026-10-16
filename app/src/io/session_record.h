// Session recording files (SPEC section 11), file mode v0. A session is a folder holding:
// - meta.json: versions, the track, the analysis source and grid, the rig's pixel count;
// - frames.bin: every output frame (render time, index, config version, flags, RGB bytes as sent);
// - params.jsonl: each published show configuration, keyed by version: the project as saved in
//   a .psyproj plus the runtime state (manual scene, blackout, hold, the grid);
// - events.jsonl: the detections (kicks, bass notes, sections, hats) the show ran on.
// Not yet: audio.wav (file mode replays from the track itself) and features.bin (the analysis is
// recomputed deterministically from the track and recorded grid).
#pragma once

#include "analysis/track_analysis.h"
#include "core/result.h"
#include "core/timebase.h"
#include "io/project.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace psyight {

inline constexpr int kSessionSchemaVersion = 1;

struct SessionMeta {
    std::string app_version;
    std::filesystem::path track;
    AnalysisSource source = AnalysisSource::mix;
    Grid analysis_grid; // The grid the analysis ran on (pass it again to reproduce it).
    int pixels = 0;
    double frame_rate_hz = 60.0;
};

struct RuntimeState {
    int manual_scene = -1;
    bool blackout = false;
    bool hold = false;
    Grid grid; // The show's grid (the labels' grid in the app).
};

struct ParamsEntry {
    std::uint64_t version = 0;
    Project project;
    RuntimeState runtime;
};

constexpr std::uint8_t kFrameBlackout = 1;
constexpr std::uint8_t kFrameHold = 2;

struct RecordedFrame {
    double t_s = 0.0;
    std::int64_t index = 0;
    std::uint64_t config_version = 0;
    std::uint8_t flags = 0;
    std::vector<std::uint8_t> rgb; // Three bytes per pixel, gamma applied, as sent.
};

Result<std::string> meta_json(const SessionMeta &meta);
Result<SessionMeta> parse_meta_json(std::string_view json);

// One line (with the newline).
Result<std::string> params_line(std::uint64_t version, const Project &project,
                                const RuntimeState &runtime);
Result<std::vector<ParamsEntry>> parse_params_jsonl(std::string_view text);

std::string events_jsonl(const TrackAnalysis &analysis);

// frames.bin: an 8-byte magic, the pixel count, then fixed-size records.
std::vector<std::uint8_t> frames_header(int pixels);
void append_frame(std::vector<std::uint8_t> &out, const RecordedFrame &frame);
struct FramesFile {
    int pixels = 0;
    std::vector<RecordedFrame> frames;
};
Result<FramesFile> parse_frames(std::span<const std::uint8_t> bytes);
Result<FramesFile> read_frames(const std::filesystem::path &path);

} // namespace psyight
