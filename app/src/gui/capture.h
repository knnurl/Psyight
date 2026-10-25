// UI capture mode (docs/RESTYLE.md, Phase 0): renders the app deterministically into an offscreen
// framebuffer and saves a PNG, so restyling can be compared capture to capture.
//
//   psyight --capture <out.png> --theme <desk|show> --audio <file> --time <seconds> --frames 60
//           [--view front|top|side|orbit] (the rig preview's view)
//
// The window is hidden and 1920x1080 at a DPI scale of 1.0; no ini file is read or written (the
// default layout is used); the crash marker is not touched. The app loads the audio, waits for
// the analysis, seeks, waits until the transport reports the new position, renders the frames
// with a fixed 1/60 s step, saves the last one and exits. The project file in the working
// directory still applies (the rig drawn in the preview), so run captures from the same folder.
// --theme is accepted and checked; it has no effect until a theme exists (RESTYLE Phase 1).
#pragma once

#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

inline constexpr int kCaptureWidth = 1920;
inline constexpr int kCaptureHeight = 1080;

struct CaptureArgs {
    std::filesystem::path out;
    std::string theme = "desk";
    std::filesystem::path audio;
    double time_s = 0.0;
    int frames = 60;
    std::string view = "front";
};

// Empty when the arguments do not ask for a capture; an error when they ask badly.
std::optional<Result<CaptureArgs>>
parse_capture_args(const std::vector<std::filesystem::path> &args);

// An RGBA8 framebuffer the size of the capture (OpenGL 3.0 entry points loaded through GLFW).
class CaptureTarget {
  public:
    CaptureTarget() = default;
    ~CaptureTarget();
    CaptureTarget(const CaptureTarget &) = delete;
    CaptureTarget &operator=(const CaptureTarget &) = delete;

    Result<void> create(int width, int height); // Needs a current GL context.
    void bind() const;                          // Draws go here until unbind().
    void unbind() const;
    std::vector<std::uint8_t> read_rgba() const; // Top row first.

  private:
    int width_ = 0;
    int height_ = 0;
    unsigned int framebuffer_ = 0;
    unsigned int texture_ = 0;
};

} // namespace psyight
