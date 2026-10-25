// The rig drawn as its pixels in the preview and the Stage (gui/preview_view.h), with zoom and
// pan, and an edit mode for the strips.
//
// View: Front (x across, y up: as the audience sees it), Top (from above, the audience at the
// bottom), Side (from the right, the audience to the left) or Orbit (a drag turns the rig; far
// pixels are drawn first and dimmer). Names hides or shows the strip names and arrows. The mouse
// wheel zooms about the pointer; a drag on empty space (or the middle button) pans; Fit view (or a
// double-click on empty space) shows the whole rig again. Editing works in the Front view. Hovering
// shows each strip's data-in arrow and name, and outlines the strip under the pointer with a
// tooltip.
//
// Edit strips (a toggle above the view) edits the project's strips (gui/project_state.h, undoable
// as any other change), drawing each strip's path over its pixels:
// - click a strip to select it (Ctrl adds or removes); its points show as handles. The info line
//   gives its pixels, length, density, groups, and the node line and pin it lands on;
// - drag a handle to move that point, or a selected strip to move the selection; Shift snaps to
//   a 1 cm grid. Q and E (or the buttons) rotate the selection by 15 degrees about its middle;
// - right-click a strip: reverse it, split it there (a ring starts there instead), add a point
//   there, remove the point under the pointer, or delete it; Delete removes the selection;
// - Draw strip: click points (Shift snaps), double-click or Enter to finish, Esc to cancel. The
//   new strip fills its path at 60 pixels per metre.
// A strip whose pixels filled its path keeps filling it as it changes (engine/strip_edit.h).
#pragma once

#include "engine/rig.h"
#include "gui/project_state.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace psyight {

class StageCanvas {
  public:
    explicit StageCanvas(ProjectState &project) : project_(project) {}

    // shown: the rig the show renders and node_rgb its pixels' drive levels (gamma applied).
    // tools: the toolbar and editing (off in the Perform view: viewing only).
    void draw(const Rig &shown, const std::vector<std::uint8_t> &node_rgb, double gamma,
              int highlighted_strip, bool tools = true);
    int hovered_strip() const { return hovered_strip_; }
    bool editing() const { return editing_; }
    // "front", "top", "side" or "orbit" (capture mode's --view); false for another name.
    bool set_view(std::string_view name);

  private:
    enum class Drag { none, pan, point, strips, orbit };
    enum class View { front, top, side, orbit };

    // A rig point in the view: u across, v up (metres) and its depth (larger: nearer).
    Vec3 project(const Vec3 &q) const;

    void draw_toolbar(const Rig &rig);
    void handle_edit(const Rig &rig);
    void edit_menu();
    void rotate_selection(double angle_rad);
    void delete_selection();
    void finish_drawing();

    ProjectState &project_;
    bool editing_ = false;
    bool show_names_ = true; // Strip names and data-in arrows on hover.
    View view_ = View::front;
    double yaw_rad_ = 0.6; // Orbit: turned about the vertical, then tilted.
    double pitch_rad_ = 0.35;
    Vec3 centre_; // The rig's middle in 3D, the orbit's pivot.
    double zoom_ = 1.0;
    double pan_x_m_ = 0.0; // The view's centre, from the rig's middle.
    double pan_y_m_ = 0.0;
    // The fit (scale and middle) frozen while dragging, so the view stays put as the rig changes.
    double fit_scale_ = 1.0;
    double mid_x_m_ = 0.0;
    double mid_y_m_ = 0.0;
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    float size_x_ = 0.0f;
    float size_y_ = 0.0f;
    int hovered_strip_ = -1;
    std::vector<int> selected_;
    Drag drag_ = Drag::none;
    int drag_strip_ = -1;
    int drag_point_ = -1;
    double drag_start_x_m_ = 0.0;
    double drag_start_y_m_ = 0.0;
    std::vector<StripSpec> drag_originals_;
    bool drawing_ = false;
    std::vector<Vec3> new_points_;
    int menu_strip_ = -1;
    int menu_point_ = -1;
    Vec3 menu_at_;
    size_t menu_segment_ = 0;
};

} // namespace psyight
