// Source and transport panel (SPEC section 9.4, M1): waveform overview, play and pause (Space),
// seek by clicking the overview, loop region with draggable bounds, volume.
#pragma once

#include "gui/session.h"

namespace psyight {

class TransportPanel {
  public:
    explicit TransportPanel(Session &session) : session_(session) {}

    void draw();

  private:
    void draw_overview(const LoadedTrack &track);

    Session &session_;
    const LoadedTrack *shown_track_ = nullptr; // Detects a newly loaded track.
    float volume_db_ = 0.0f;
};

} // namespace psyight
