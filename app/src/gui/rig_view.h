// Rig panel (SPEC sections 7.1, 9.4 and 10, M3): the project file (load and save), templates
// (pillars, matrix panel: engine/rig_templates.h) that replace the strips after a confirmation, the
// strips (name, polyline, density, pixel count, colour order, groups), the patch layout and checks:
// strips running past their polyline, the 1,500-pixel budget, and the supply current at full
// white against the power budget. Tabbed with the preview, which shows the result.
#pragma once

#include "engine/rig_templates.h"
#include "gui/project_state.h"

#include <cstdint>
#include <string>
#include <vector>

namespace psyight {

class RigView {
  public:
    explicit RigView(ProjectState &project) : project_(project) {}

    void draw();
    void draw_template_controls(); // Also in the setup assistant.
    // The strip whose row is hovered (-1: none), and the one to highlight (hovered in the preview).
    int hovered_strip() const { return hovered_strip_; }
    void set_highlighted_strip(int strip) { highlighted_strip_ = strip; }

  private:
    void draw_project();
    void draw_templates();
    void draw_strips();
    void draw_patch();

    ProjectState &project_;
    std::string path_text_;
    // Text being edited per strip (polyline and groups), refreshed when the project changes.
    std::vector<std::string> polyline_texts_;
    std::vector<std::string> group_texts_;
    std::uint64_t texts_version_ = 0;
    int template_ = 0; // 0: pillars, 1: matrix panel.
    int hovered_strip_ = -1;
    int highlighted_strip_ = -1;
    PillarsTemplate pillars_;
    MatrixTemplate matrix_;
};

} // namespace psyight
