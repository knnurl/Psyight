// Analysis view (SPEC section 9.4, M1): scrolling log-frequency spectrogram, band energies and
// spectral flux on one shared time axis that follows the playhead, with the manual grid (BPM and
// a draggable first downbeat) drawn on every plot, and the residual view (SPEC 9.4, M2): non-bass
// level, centroid and flatness with bass rests shaded and hats marked. Each plot folds under its
// own header.
#pragma once

#include <array>

#include "gui/field_gesture.h"
#include "gui/session.h"
#include "gui/spectrogram_textures.h"
#include "gui/theme.h"
#include "io/user_settings.h"

namespace psyight {

class AnalysisView {
  public:
    explicit AnalysisView(Session &session) : session_(session) {}

    void draw();
    void apply_settings(const UserSettings &settings); // The spectrogram's colours.
    void store_settings(UserSettings &settings) const;

  private:
    void draw_toolbar(const LoadedTrack &track);
    // The residual's level, centroid and flatness, bass rests shaded, hats as ticks.
    void draw_residual(const TrackAnalysis &analysis, const Grid &grid, double frame_s,
                       double from_s, double to_s);

    Session &session_;
    SpectrogramTextures textures_;
    float min_db_ = -90.0f;
    float max_db_ = -10.0f;
    bool colours_changed_ = false;
    Colormap colormap_ = theme().spectrogram_colormap; // The spectrogram's colours.
    bool dragging_downbeat_ = false;
    FieldGesture field_gesture_;                  // The grid fields: one undo step per drag.
    AnalysisSource source_ = AnalysisSource::mix; // For the next re-analysis.
    bool source_chosen_ = false;                  // Until chosen, it follows the loaded analysis.
    // Which of the spectrogram, band energy, flux and residual plots are unfolded.
    std::array<bool, 4> open_{true, true, true, false}; // The residual starts folded.
};

} // namespace psyight
