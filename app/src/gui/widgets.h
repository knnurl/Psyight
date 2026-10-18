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
#pragma once

#include <imgui.h>

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
bool drag_int(const char *label, int *value, float speed, int min, int max,
              const char *format = "%d");

} // namespace psyight
