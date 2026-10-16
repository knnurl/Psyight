// Shared pieces of every time-based plot: an X axis linked to the session's view range, the
// manual grid lines and the playhead. Call between ImPlot::BeginPlot and EndPlot.
#pragma once

#include "core/timebase.h"
#include "gui/session.h"

#include <vector>

namespace psyight {

// Sets up ImAxis_X1 in seconds, linked to view.min_s and view.max_s.
void setup_time_axis(TimeView &view);

// Bar, beat and 16th lines over [from_s, to_s], thinned when zoomed out.
void draw_grid_lines(const Grid &grid, double from_s, double to_s);

void draw_playhead(double playhead_s);

// Shades the loop region when it is enabled. With editable, its edges are draggable (ImPlot drag
// tool ids first_drag_id and first_drag_id + 1). Returns the edge times, for seek_on_click.
std::vector<double> draw_loop(Session &session, bool editable, int first_drag_id);

// Seeks to the clicked time on a click that did not drag (a drag pans), unless the click was
// within a few pixels of one of handle_times_s (a draggable line).
void seek_on_click(Session &session, const std::vector<double> &handle_times_s);

} // namespace psyight
