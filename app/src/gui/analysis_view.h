// Analysis view (SPEC section 9.4, M1): scrolling log-frequency spectrogram, band energies and
// spectral flux on one shared time axis that follows the playhead, with the manual grid (BPM and
// a draggable first downbeat) drawn on every plot.
#pragma once

#include "gui/session.h"
#include "gui/spectrogram_textures.h"

namespace psyight {

class AnalysisView {
  public:
    explicit AnalysisView(Session &session) : session_(session) {}

    void draw();

  private:
    void draw_toolbar(const LoadedTrack &track);
    void draw_grid_lines(double from_s, double to_s) const;
    void draw_playhead(double playhead_s) const;

    Session &session_;
    SpectrogramTextures textures_;
    bool follow_ = true;
    float window_s_ = 8.0f;
    float min_db_ = -90.0f;
    float max_db_ = -10.0f;
    bool colours_changed_ = false;
};

} // namespace psyight
