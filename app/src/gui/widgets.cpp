#include "gui/widgets.h"

#include "gui/icons.h"
#include "gui/theme.h"

#include <cstdarg>

namespace psyight {

namespace {

constexpr float kHelpWrapEm = 28.0f; // Tooltip width, in font heights.

} // namespace

bool primary_button(const char *label, const ImVec2 &size) {
    push_filled_button(theme().accent);
    const bool clicked = ImGui::Button(label, size);
    pop_filled_button();
    return clicked;
}

bool toggle_button(const char *label, bool on, const ImVec4 *on_fill) {
    if (on) {
        push_filled_button(on_fill != nullptr ? *on_fill : theme().accent);
    }
    const bool clicked = ImGui::Button(label);
    if (on) {
        pop_filled_button();
    }
    return clicked;
}

void help_marker(const char *text) {
    ImGui::TextDisabled("%s", kIconHelp);
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(kHelpWrapEm * ImGui::GetFontSize());
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void mono_text(const char *format, ...) {
    ImGui::PushFont(theme_font(ThemeFont::mono));
    va_list args;
    va_start(args, format);
    ImGui::TextV(format, args);
    va_end(args);
    ImGui::PopFont();
}

void push_mono_field() {
    const float ui_px = ImGui::GetFontSize();
    ImGui::PushFont(theme_font(ThemeFont::mono));
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(padding.x, padding.y + 0.5f * (ui_px - ImGui::GetFontSize())));
}

void pop_mono_field() {
    ImGui::PopStyleVar();
    ImGui::PopFont();
}

bool drag_double(const char *label, double *value, float speed, double min, double max,
                 const char *format) {
    return mono_field(label, [&](const char *id) {
        return ImGui::DragScalar(id, ImGuiDataType_Double, value, speed, &min, &max, format,
                                 ImGuiSliderFlags_AlwaysClamp);
    });
}

bool drag_int(const char *label, int *value, float speed, int min, int max, const char *format) {
    return mono_field(label, [&](const char *id) {
        return ImGui::DragInt(id, value, speed, min, max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

} // namespace psyight
