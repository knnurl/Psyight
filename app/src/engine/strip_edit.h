// Editing a strip's geometry (the Stage's edit mode, gui/preview_view.h): finding the nearest
// point on a strip, inserting and removing points, splitting, moving, rotating and snapping.
// Pure functions of the strips, in metres in the x-y plane (z kept). Counts stand: the project
// fits them after the edit with every strip in view (fit_counts, engine/rig.h), so a strip that
// fills its path keeps filling it, joints included.
#pragma once

#include "engine/rig.h"

#include <utility>
#include <vector>

namespace psyight {

// The nearest point on the polyline to p: the segment it lies on (from point `segment` to the
// next), the point, and its distance.
struct PolylineHit {
    size_t segment = 0;
    Vec3 point;
    double distance_m = 0.0;
};
PolylineHit nearest_on_polyline(const std::vector<Vec3> &polyline, const Vec3 &p);

bool is_closed(const StripSpec &strip); // Its last point on its first (a ring).

// Point `index` moved to `to`; a ring's first and last points move together.
StripSpec with_point_moved(StripSpec strip, size_t index, const Vec3 &to);
// A point inserted on segment `segment` (after point `segment`).
StripSpec with_point_inserted(StripSpec strip, size_t segment, const Vec3 &at);
// Point `index` removed (unless two would be left; a ring keeps three).
StripSpec with_point_removed(StripSpec strip, size_t index);
// The strip cut at `at` on segment `segment`: the part before (named "<name> a") and after
// ("<name> b"), pixels shared by length. A ring starts there instead (one strip, still a ring,
// its data-in end moved to the cut).
std::vector<StripSpec> split_strip(const StripSpec &strip, size_t segment, const Vec3 &at);
// Moved by (dx, dy); rotated by angle_rad (anticlockwise) about centre.
StripSpec moved(StripSpec strip, double dx_m, double dy_m);
StripSpec rotated(StripSpec strip, const Vec3 &centre, double angle_rad);
// x and y onto a grid of step_m.
Vec3 snapped(const Vec3 &p, double step_m);

} // namespace psyight
