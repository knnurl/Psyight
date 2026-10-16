// Shared pieces of every time-based plot: an X axis linked to the session's view range, the
// manual grid lines and the playhead. Call between ImPlot::BeginPlot and EndPlot.
#pragma once

#include "core/timebase.h"
#include "gui/session.h"

#include <string>
#include <string_view>
#include <vector>

namespace psyight {

// Sets up ImAxis_X1 in seconds, linked to view.min_s and view.max_s.
void setup_time_axis(TimeView &view);

// Every time plot's y tick labels are this many characters wide and every y axis has a title:
// with the monospaced font, all time axes then start at the same x and line up.
inline constexpr int kValueLabelChars = 5;
std::string fixed_width_label(std::string_view text); // Right-aligned in kValueLabelChars.
// ImPlot formatter: the value with *static_cast<int *>(decimals) decimals, fixed width.
int format_fixed_width(double value, char *buffer, int size, void *decimals);

// Bar, beat and 16th lines over [from_s, to_s], thinned when zoomed out.
void draw_grid_lines(const Grid &grid, double from_s, double to_s);

void draw_playhead(double playhead_s);

// Detected sections (SPEC 6.6) as a labelled, colour-coded strip along the top of the plot.
// Phrase starts are marked as white ticks on the strip.
void draw_sections(const std::vector<DetectedSection> &sections,
                   const std::vector<double> &phrase_starts_s, double from_s, double to_s);

// Shades the loop region when it is enabled and draws its edges: draggable with editable (ImPlot
// drag tool ids first_drag_id and first_drag_id + 1), plain lines otherwise. Returns the edge
// times, for seek_on_click.
std::vector<double> draw_loop(Session &session, bool editable, int first_drag_id);

// Seeks to the clicked time on a click that did not drag (a drag pans), unless the click was
// within a few pixels of one of handle_times_s (a draggable line).
void seek_on_click(Session &session, const std::vector<double> &handle_times_s);

} // namespace psyight
