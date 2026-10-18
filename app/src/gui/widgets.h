// Control helpers (RESTYLE Phase 3), styled from the theme (gui/theme.h):
// - primary_button: filled with the accent, for the main action of a panel (Play, Save).
// - secondary_button: neutral, for everything else (ImGui's own button, named for clarity).
// - toggle_button: neutral when off, filled when on (the accent unless another fill is given).
// - help_marker: a dim help icon with the text in a tooltip, for instructions that would
//   otherwise take panel space.
// - mono_text and mono_field: numeric readouts and fields in the mono font. A field's label
//   stays in the UI font and the widget and label form one item, so IsItemActive and
//   SetItemTooltip after it work as for the plain widget.
// - drag_double and drag_int: number fields that drag (double-click or Ctrl+click to type),
//   clamped to their range.
// - knob and knob_int: rotary knobs (Ableton style): the label above in the small font, a
//   270-degree arc (bipolar ranges grow from 0), the value below in the mono font. Drag up or
//   down (Shift for fine), double-click to type, right-click for a menu: Reset to default, then
//   the caller's items (set_knob_menu, just before the knob). With
//   `modulated`, a thin outer ring runs from the value to where modulation moves it. The format
//   prints a double ("%.2f", "%.0f px"), also for knob_int.
#pragma once

#include <imgui.h>

#include <functional>

namespace psyight {

bool primary_button(const char *label, const ImVec2 &size = ImVec2(0.0f, 0.0f));
inline bool secondary_button(const char *label, const ImVec2 &size = ImVec2(0.0f, 0.0f)) {
    return ImGui::Button(label, size);
}
// Returns true when clicked; the caller flips its state.
bool toggle_button(const char *label, bool on, const ImVec4 *on_fill = nullptr);
void help_marker(const char *text);

void mono_text(const char *format, ...) IM_FMTARGS(1);
// The mono font, with the frame padding adjusted so a field keeps the UI font's height.
void push_mono_field();
void pop_mono_field();

template <typename Widget> bool mono_field(const char *label, Widget &&widget) {
    const char *end = label;
    while (*end != '\0' && !(end[0] == '#' && end[1] == '#')) {
        ++end;
    }
    ImGui::BeginGroup();
    ImGui::PushID(label);
    push_mono_field();
    const bool changed = widget("##value");
    pop_mono_field();
    ImGui::PopID();
    if (end != label) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label, end);
    }
    ImGui::EndGroup();
    return changed;
}

bool drag_double(const char *label, double *value, float speed, double min, double max,
                 const char *format);

bool knob(const char *label, double *value, double min, double max, const char *format,
          double default_value, const double *modulated = nullptr);
bool knob_int(const char *label, int *value, int min, int max, const char *format,
              int default_value, const double *modulated = nullptr);
// Items the next knob's right-click menu shows after Reset (drawn inside the menu, while the knob
// is drawn). Applies to the next knob only.
void set_knob_menu(std::function<void()> items);
float knob_width(); // Pixels: lay knobs out in columns of this width.
// Knob size multiplier for the knobs drawn until the pop (the Perform view's big knobs).
void push_knob_scale(float scale);
void pop_knob_scale();

// A device panel (the Show tab's racks): a child window of `size` in the device colour with a
// title bar strip of device_title_height() at the top. Draw the title row first (compact
// buttons: push_device_title / pop_device_title), then the body; always pair with end_device.
void begin_device(const char *id, const ImVec2 &size);
void end_device();
float device_title_height();
float device_padding();   // Inside a device, each side.
void push_device_title(); // Compact frames for the title row.
void pop_device_title();
// An icon-only button in a device title, frameless until hovered.
bool device_icon_button(const char *icon, const char *id, const char *tooltip);
float knob_height(); // Pixels, label and value included.
bool drag_int(const char *label, int *value, float speed, int min, int max,
              const char *format = "%d");

} // namespace psyight
