#include "gui/widgets.h"

#include "gui/icons.h"
#include "gui/theme.h"

#include <imgui_internal.h> // TempInputScalar: typing into a knob.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace psyight {

namespace {

constexpr float kHelpWrapEm = 28.0f; // Tooltip width, in font heights.
constexpr float kKnobWidthEm = 3.9f;
constexpr float kKnobRadiusEm = 0.95f;
constexpr float kKnobDragPixels = 160.0f; // A full turn over this many pixels of vertical drag.
constexpr float kKnobFine = 0.1f;         // With Shift.
constexpr float kPi = 3.14159265f;
constexpr float kKnobStart = 0.75f * kPi; // Down-left, clockwise to down-right.
constexpr float kKnobSweep = 1.5f * kPi;
constexpr float kKnobRingMarginEm = 0.3f; // Above the knob, for the modulation ring.
constexpr float kDeviceRounding = 4.0f;
constexpr float kDeviceTitlePaddingY = 2.0f;

std::function<void()> g_knob_menu; // The next knob's extra menu items (set_knob_menu).

float knob_fraction(double value, double min, double max) {
    return max > min ? static_cast<float>(std::clamp((value - min) / (max - min), 0.0, 1.0)) : 0.0f;
}

void arc(ImDrawList *draw, ImVec2 centre, float radius, float from, float to, ImU32 colour,
         float thickness) {
    if (std::abs(to - from) < 1e-4f) {
        return;
    }
    draw->PathArcTo(centre, radius, std::min(from, to), std::max(from, to), 32);
    draw->PathStroke(colour, ImDrawFlags_None, thickness);
}

bool knob_impl(const char *label, double *value, double min, double max, const char *format,
               double default_value, const double *modulated, bool integer) {
    const float em = ImGui::GetFontSize();
    const ImGuiStyle &style = ImGui::GetStyle();
    ImFont *label_font = theme_font(ThemeFont::plot);
    ImFont *mono = theme_font(ThemeFont::mono);
    const float small_px = label_font != nullptr ? label_font->FontSize : em;
    const float mono_px = mono != nullptr ? mono->FontSize : em;
    const float width = knob_width();
    const float radius = kKnobRadiusEm * em;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, knob_height());
    const char *label_end = label;
    while (*label_end != '\0' && !(label_end[0] == '#' && label_end[1] == '#')) {
        ++label_end;
    }

    ImGui::BeginGroup(); // One item for the caller, also while the text field is open.
    ImGui::PushID(label);
    ImGui::InvisibleButton("##knob", size);
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const ImGuiID drag_key = ImGui::GetID("##drag value");
    bool changed = false;

    // Typing: double-click opens a text field over the value line.
    const ImRect value_rect(
        ImVec2(origin.x, origin.y + size.y - mono_px - 2.0f * style.FramePadding.y),
        ImVec2(origin.x + width, origin.y + size.y));
    bool typing = ImGui::TempInputIsActive(id);
    if (!typing && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        typing = true;
    }
    if (typing) {
        double typed = *value;
        ImGui::PushFont(mono);
        if (ImGui::TempInputScalar(value_rect, id, "##knob", ImGuiDataType_Double, &typed, format,
                                   &min, &max)) {
            *value = integer ? std::round(typed) : typed;
            changed = true;
        }
        ImGui::PopFont();
    } else if (active) {
        if (ImGui::IsItemActivated()) {
            storage->SetFloat(drag_key, static_cast<float>(*value));
        }
        const float dy = -ImGui::GetIO().MouseDelta.y;
        if (dy != 0.0f) {
            const float step = static_cast<float>(max - min) / kKnobDragPixels *
                               (ImGui::GetIO().KeyShift ? kKnobFine : 1.0f);
            const double dragged = std::clamp(
                static_cast<double>(storage->GetFloat(drag_key, static_cast<float>(*value))) +
                    dy * step,
                min, max);
            storage->SetFloat(drag_key, static_cast<float>(dragged));
            const double next = integer ? std::round(dragged) : dragged;
            if (next != *value) {
                *value = next;
                changed = true;
            }
        }
    }
    // The right-click menu: Reset, then the caller's items.
    const std::function<void()> menu = std::move(g_knob_menu);
    g_knob_menu = nullptr;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##knob menu");
    }
    if (ImGui::BeginPopup("##knob menu")) {
        if (ImGui::MenuItem("Reset to default", nullptr, false, *value != default_value)) {
            *value = default_value;
            changed = true;
        }
        if (menu) {
            menu();
        }
        ImGui::EndPopup();
    }

    // Drawing.
    const ThemeTokens &t = theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 label_size =
        label_font != nullptr ? label_font->CalcTextSizeA(small_px, width, 0.0f, label, label_end)
                              : ImGui::CalcTextSize(label, label_end);
    draw->PushClipRect(origin, ImVec2(origin.x + width, origin.y + size.y), true);
    draw->AddText(label_font, small_px,
                  ImVec2(origin.x + 0.5f * (width - std::min(label_size.x, width)), origin.y),
                  to_u32(hovered || active ? t.text : t.text_dim), label, label_end);
    const ImVec2 centre(origin.x + 0.5f * width, origin.y + small_px + style.ItemInnerSpacing.y +
                                                     kKnobRingMarginEm * em + radius);
    const float thickness = std::max(2.0f, 0.2f * radius);
    const float zero = knob_fraction(min < 0.0 && max > 0.0 ? 0.0 : min, min, max);
    const float at = knob_fraction(*value, min, max);
    const auto angle = [](float fraction) { return kKnobStart + fraction * kKnobSweep; };
    arc(draw, centre, radius, angle(0.0f), angle(1.0f),
        to_u32(hovered || active ? t.frame_hover : t.knob_track), thickness);
    arc(draw, centre, radius, angle(zero), angle(at), to_u32(t.knob_value), thickness);
    const ImVec2 tip(centre.x + std::cos(angle(at)) * radius,
                     centre.y + std::sin(angle(at)) * radius);
    const ImVec2 inner(centre.x + std::cos(angle(at)) * 0.35f * radius,
                       centre.y + std::sin(angle(at)) * 0.35f * radius);
    draw->AddLine(inner, tip, to_u32(t.knob_pointer), std::max(1.5f, 0.12f * radius));
    if (modulated != nullptr) {
        arc(draw, centre, radius + thickness + 1.5f, angle(at),
            angle(knob_fraction(*modulated, min, max)), to_u32(t.mod_ring),
            std::max(1.5f, 0.6f * thickness));
    }
    if (!typing) {
        char text[64];
        std::snprintf(text, sizeof(text), format, integer ? std::round(*value) : *value);
        const ImVec2 text_size = mono != nullptr ? mono->CalcTextSizeA(mono_px, FLT_MAX, 0.0f, text)
                                                 : ImGui::CalcTextSize(text);
        draw->AddText(mono, mono_px,
                      ImVec2(origin.x + 0.5f * (width - text_size.x),
                             origin.y + size.y - mono_px - style.FramePadding.y),
                      to_u32(t.text), text);
    }
    draw->PopClipRect();
    if (hovered && !active && !typing) {
        ImGui::SetTooltip("%.*s. Drag (Shift: fine); double-click to type; right-click for more.",
                          static_cast<int>(label_end - label), label);
    }
    ImGui::PopID();
    ImGui::EndGroup();
    return changed;
}

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

bool knob(const char *label, double *value, double min, double max, const char *format,
          double default_value, const double *modulated) {
    return knob_impl(label, value, min, max, format, default_value, modulated, false);
}

bool knob_int(const char *label, int *value, int min, int max, const char *format,
              int default_value, const double *modulated) {
    double v = *value;
    if (!knob_impl(label, &v, min, max, format, default_value, modulated, true)) {
        return false;
    }
    *value = static_cast<int>(std::lround(v));
    return true;
}

void set_knob_menu(std::function<void()> items) { g_knob_menu = std::move(items); }

float knob_width() { return kKnobWidthEm * ImGui::GetFontSize(); }

float device_padding() { return 0.4f * ImGui::GetFontSize(); }

float device_title_height() {
    return ImGui::GetFontSize() + 2.0f * kDeviceTitlePaddingY + 2.0f * device_padding();
}

void begin_device(const char *id, const ImVec2 &size) {
    const ThemeTokens &t = theme();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.device_bg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kDeviceRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(device_padding(), device_padding()));
    ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    const ImVec2 pos = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos, ImVec2(pos.x + ImGui::GetWindowWidth(), pos.y + device_title_height()),
        to_u32(t.device_title), kDeviceRounding, ImDrawFlags_RoundCornersTop);
}

void end_device() { ImGui::EndChild(); }

void push_device_title() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(0.3f * ImGui::GetFontSize(), kDeviceTitlePaddingY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(0.25f * ImGui::GetFontSize(), ImGui::GetStyle().ItemSpacing.y));
}

void pop_device_title() { ImGui::PopStyleVar(2); }

bool device_icon_button(const char *icon, const char *id, const char *tooltip) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    const bool clicked = ImGui::Button((std::string(icon) + "###" + id).c_str());
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
}

float knob_height() {
    const ImFont *label_font = theme_font(ThemeFont::plot);
    const ImFont *mono = theme_font(ThemeFont::mono);
    const float em = ImGui::GetFontSize();
    return (label_font != nullptr ? label_font->FontSize : em) +
           ImGui::GetStyle().ItemInnerSpacing.y + kKnobRingMarginEm * em +
           2.0f * kKnobRadiusEm * em + 2.0f * ImGui::GetStyle().FramePadding.y +
           (mono != nullptr ? mono->FontSize : em);
}

bool drag_int(const char *label, int *value, float speed, int min, int max, const char *format) {
    return mono_field(label, [&](const char *id) {
        return ImGui::DragInt(id, value, speed, min, max, format, ImGuiSliderFlags_AlwaysClamp);
    });
}

} // namespace psyight
