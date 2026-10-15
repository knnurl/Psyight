// Shared pieces of every time-based plot: an X axis linked to the session's view range, the
// manual grid lines and the playhead. Call between ImPlot::BeginPlot and EndPlot.
#pragma once

#include "core/timebase.h"
#include "gui/session.h"

namespace psyight {

// Sets up ImAxis_X1 in seconds, linked to view.min_s and view.max_s.
void setup_time_axis(TimeView &view);

// Bar, beat and 16th lines over [from_s, to_s], thinned when zoomed out.
void draw_grid_lines(const Grid &grid, double from_s, double to_s);

void draw_playhead(double playhead_s);

} // namespace psyight
