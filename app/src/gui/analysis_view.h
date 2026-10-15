// Analysis view (SPEC section 9.4, M1): scrolling log-frequency spectrogram, band energies and
// spectral flux on one shared time axis that follows the playhead, with the manual grid (BPM and
// a draggable first downbeat) drawn on every plot.
#pragma once

#include <array>

#include "gui/session.h"
#include "gui/spectrogram_textures.h"

namespace psyight {

class AnalysisView {
  public:
    explicit AnalysisView(Session &session) : session_(session) {}

    void draw();

  private:
    void draw_toolbar(const LoadedTrack &track);

    Session &session_;
    SpectrogramTextures textures_;
    float min_db_ = -90.0f;
    float max_db_ = -10.0f;
    bool colours_changed_ = false;
    bool dragging_downbeat_ = false;
    // Plot heights. ImPlot writes divider drags into this, so it must persist between frames.
    std::array<float, 3> row_ratios_{0.55f, 0.25f, 0.20f};
};

} // namespace psyight
