// Rig panel (SPEC sections 7.1, 9.4 and 10, M3): the project (its name; New, Open, Save, Save
// as), a new rig from one of four sources (templates: pillars or a matrix panel from a few
// numbers; the built-in presets; the user's own rigs, which the current one can be saved to;
// an SVG or DXF drawing: engine/rig_templates.h, io/presets.h, io/svg_rig.h, io/dxf_rig.h) that
// replaces the strips
// after a confirmation, the strips (name, polyline, density, pixel count, colour order, groups),
// the patch layout and checks: strips running past their polyline, the 1,500-pixel budget, and the
// supply current at full white against the power budget. Tabbed with the preview, which shows the
// result.
#pragma once

#include "engine/rig_templates.h"
#include "gui/project_state.h"
#include "io/svg_rig.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

class RigView {
  public:
    explicit RigView(ProjectState &project) : project_(project) {}

    void draw();
    void draw_template_controls(); // The new rig's sources; also in the setup assistant.
    // The strip whose row is hovered (-1: none), and the one to highlight (hovered in the preview).
    int hovered_strip() const { return hovered_strip_; }
    void set_highlighted_strip(int strip) { highlighted_strip_ = strip; }
    // After the frame: the drawing's file dialog, when asked for (owner: the native window).
    void after_frame(void *owner);

  private:
    void draw_project();
    void draw_templates();
    // Each source sets the strips it would make (none: nothing chosen yet).
    void draw_generators(std::vector<StripSpec> &strips);
    void draw_presets(std::vector<StripSpec> &strips);
    // The user's rigs: saved in presets/rigs (io/presets.h); saving the current one.
    void draw_user_rigs(std::vector<StripSpec> &strips);
    void draw_save_rig();
    void draw_svg(std::vector<StripSpec> &strips); // A drawing: chosen, sized, built.
    void reload_user_rigs();
    int rig_list(const char *id, const std::vector<RigPreset> &rigs, int chosen);
    // The strips' summary against the budgets, and Replace the rig after a confirmation.
    void draw_replace(std::vector<StripSpec> strips);
    void draw_strips();
    void draw_patch();

    ProjectState &project_;
    // Text being edited per strip (polyline and groups), refreshed when the project changes.
    std::vector<std::string> polyline_texts_;
    std::vector<std::string> group_texts_;
    std::uint64_t texts_version_ = 0;
    int generator_ = 0; // Templates: 0 pillars, 1 matrix panel.
    int hovered_strip_ = -1;
    int highlighted_strip_ = -1;
    PillarsTemplate pillars_;
    MatrixTemplate matrix_;
    std::vector<RigPreset> presets_ = builtin_rig_presets();
    int preset_ = 0;
    std::vector<std::vector<double>> preset_values_;          // Per preset, its settings as edited.
    static constexpr const char *kUserPresetsDir = "presets"; // As the preset browser's.
    std::vector<RigPreset> user_rigs_;
    bool user_rigs_loaded_ = false;
    int user_rig_ = 0;
    std::string rig_name_;
    std::string rig_status_;
    bool svg_requested_ = false;
    std::string svg_file_; // The drawing's file name, and its text.
    std::string svg_text_;
    bool svg_dxf_ = false; // The drawing is a DXF (else an SVG).
    std::string svg_status_;
    SvgRigOptions svg_options_;
    std::optional<SvgRigOptions> svg_built_options_; // The options svg_built_ was made with.
    std::optional<Result<SvgRig>> svg_built_;
};

} // namespace psyight
