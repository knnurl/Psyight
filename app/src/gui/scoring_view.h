// Scoring panel (SPEC sections 6.10 and 9.4, M1): the loaded track's detected bass notes scored
// against the labelled notes of every complete region and in total (analysis/scoring.h, the
// same numbers as psyight_eval), the offset error distribution, and every error, worst first.
// Clicking an error seeks there, so it can be heard and corrected in the bass roll. Rescores
// whenever the labels or the analysis change.
#pragma once

#include "analysis/scoring.h"
#include "gui/session.h"

#include <vector>

namespace psyight {

class ScoringView {
  public:
    explicit ScoringView(Session &session) : session_(session) {}

    void draw();

  private:
    void rescore_if_changed();
    void draw_table() const;
    void draw_offsets() const;
    void draw_errors();

    Session &session_;
    // What the current score was computed from.
    const TrackAnalysis *scored_analysis_ = nullptr;
    Labels scored_labels_;
    TrackScore score_;
    std::vector<NoteError> errors_;
    std::vector<double> offset_errors_ms_;
    int selected_error_ = -1;
};

} // namespace psyight
