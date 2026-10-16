// Analysis view (SPEC section 9.4, M1): scrolling log-frequency spectrogram, band energies and
// spectral flux on one shared time axis that follows the playhead, with the manual grid (BPM and
// a draggable first downbeat) drawn on every plot. Each plot folds under its own header.
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
    // Which of the spectrogram, band energy and flux plots are unfolded.
    std::array<bool, 3> open_{true, true, true};
};

} // namespace psyight
