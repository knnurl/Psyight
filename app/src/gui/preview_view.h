// Preview (SPEC section 9.4, M3): the rig's pixels drawn at their positions, as the virtual node
// received them from the show thread (render at the playhead plus the latency offset, limiters,
// patch, packets), with the scene's layers, palette, limiter settings, output settings and the
// estimated supply current. File mode: the analysis is known ahead, so the render time may lead
// the playhead (SPEC 4.3).
// The panel owns the show thread: it publishes a new config when a setting, the track or the grid
// changes, and draws the latest frame; neither side waits for the other.
#pragma once

#include "gui/project_state.h"
#include "gui/session.h"
#include "output/show.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace psyight {

class PreviewView {
  public:
    PreviewView(Session &session, ProjectState &project);

    void draw();

  private:
    void sync_config(); // Publishes config_ when it or the track changed.
    void draw_controls(const ShowFrame &frame);
    void draw_scene_cards(const ShowFrame &frame);
    void draw_output(const ShowFrame &frame);
    void draw_rig(const ShowFrame &frame);

    Session &session_;
    ProjectState &project_;
    std::uint64_t project_version_ = 0; // The project version config_ was built from.
    ShowConfig config_;
    bool dirty_ = true;
    std::uint64_t version_ = 0;
    int edit_scene_ = 0; // Whose layers and palette the controls show.
    std::string address_text_ = "127.0.0.1";
    bool address_active_ = false;
    // Measured frame rate, from the frame index over about a second.
    std::chrono::steady_clock::time_point rate_since_ = std::chrono::steady_clock::now();
    std::int64_t rate_index_ = -1;
    double measured_fps_ = 0.0;
    ShowThread show_; // Last, so it is joined first on destruction.
};

} // namespace psyight
