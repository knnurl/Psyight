// Analysis view (SPEC section 9.4, M1): scrolling log-frequency spectrogram, band energies and
// spectral flux on one shared time axis that follows the playhead, with the manual grid (BPM and
// a draggable first downbeat) drawn on every plot, and the residual view (SPEC 9.4, M2): non-bass
// level, centroid and flatness with bass rests shaded and hats marked. Each plot folds under its
// own header; a splitter between two open plots moves height between them (double-click: the
// default shares again), and the shares persist in the user settings.
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
    // Sections by hand (the labels): boundaries dragged on the spectrogram, the strip's
    // right-click menu (label, split, merge, back to the detection).
    void draw_section_menu(const LoadedTrack &track);
    double snap_to_bar(double t_s) const; // Unless Alt is held.
    bool dragging_section_ = false;
    bool open_section_menu_ = false;
    double section_menu_s_ = 0.0;
    FieldGesture field_gesture_;                  // The grid fields: one undo step per drag.
    AnalysisSource source_ = AnalysisSource::mix; // For the next re-analysis.
    bool source_chosen_ = false;                  // Until chosen, it follows the loaded analysis.
    // The spectrogram, band energy, flux and residual plots: which are unfolded, and their shares
    // of the height.
    static constexpr size_t kPlots = 4;
    std::array<bool, kPlots> open_{true, true, true, false}; // The residual starts folded.
    std::array<float, kPlots> shares_ = UserSettings{}.analysis_plot_shares;
    // Under plot `above` (call right after it), the splitter shared with the next open plot, if
    // any: a pane_divider line across the gap; dragging it changes both heights and shares.
    void draw_plot_splitter(size_t above, std::array<float, kPlots> &heights);
};

} // namespace psyight
