// Preview (SPEC section 9.4, M3): the rig's pixels drawn at their positions, rendered by the
// engine at the playhead (plus the latency offset) through the limiters, with the scene's layers,
// palette, limiter settings and the estimated supply current. File mode: the analysis is known
// ahead, so the render time may lead the playhead (SPEC 4.3).
// v0 renders on the GUI thread (444 pixels, cheap); the fixed-rate engine thread comes with the
// network output.
#pragma once

#include "engine/limiters.h"
#include "engine/rig.h"
#include "engine/scene.h"
#include "gui/session.h"

#include <vector>

namespace psyight {

class PreviewView {
  public:
    explicit PreviewView(Session &session) : session_(session) {}

    void draw();

    // The last rendered frame, after the limiters (for the output).
    const Frame &frame() const { return frame_; }
    const Rig &rig() const { return rig_; }

  private:
    void draw_controls();
    void draw_rig();

    Session &session_;
    Rig rig_ = default_rig();
    Scene scene_ = default_scene();
    std::vector<Palette> palettes_ = default_palettes();
    Limiters limiters_;
    Frame frame_;
    LimiterReport report_;
    double last_render_s_ = 0.0;
    float latency_offset_ms_ = 0.0f;
    int flashes_limited_ = 0;
};

} // namespace psyight
