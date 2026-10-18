#include "gui/status_bar.h"

#include "gui/theme.h"

#include <imgui_internal.h> // BeginViewportSideBar: the bar takes space from the dock space.

namespace psyight {

namespace {

ImVec4 tone_colour(ChipTone tone) {
    const ThemeTokens &t = theme();
    switch (tone) {
    case ChipTone::plain:
        return t.text;
    case ChipTone::dim:
        return t.text_dim;
    case ChipTone::good:
        return t.success;
    case ChipTone::warn:
        return t.warning;
    case ChipTone::bad:
        return t.danger;
    case ChipTone::accent:
        return t.accent;
    }
    return t.text;
}

// A chip: its text on a frame-coloured rounded rectangle.
void chip(const StatusChip &c) {
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 text = ImGui::CalcTextSize(c.text.c_str());
    const ImVec2 pad(0.5f * style.FramePadding.x, 0.5f * style.FramePadding.y);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 size(text.x + 2.0f * pad.x, text.y + 2.0f * pad.y);
    ImGui::Dummy(size);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), to_u32(theme().frame),
                        style.FrameRounding);
    draw->AddText(ImVec2(at.x + pad.x, at.y + pad.y), to_u32(tone_colour(c.tone)), c.text.c_str());
    if (!c.tooltip.empty()) {
        ImGui::SetItemTooltip("%s", c.tooltip.c_str());
    }
}

} // namespace

bool draw_status_bar(const StatusInfo &info) {
    const float height = ImGui::GetFrameHeight();
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    bool clicked = false;
    if (ImGui::BeginViewportSideBar("##status bar", ImGui::GetMainViewport(), ImGuiDir_Down, height,
                                    flags)) {
        if (ImGui::BeginMenuBar()) {
            // The banner: the worst failure, or the warnings, or all clear.
            std::string banner;
            ImVec4 colour = theme().success;
            if (info.failures > 0) {
                banner = info.fault;
                if (info.failures > 1) {
                    banner += " (+" + std::to_string(info.failures - 1) + " more)";
                }
                colour = theme().danger;
            } else if (info.warnings > 0) {
                banner = std::to_string(info.warnings) +
                         (info.warnings == 1 ? " preflight warning" : " preflight warnings");
                colour = theme().warning;
            } else {
                banner = "All preflight checks pass";
            }
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            if (ImGui::Selectable(banner.c_str(), false, 0,
                                  ImVec2(ImGui::CalcTextSize(banner.c_str()).x, 0.0f))) {
                clicked = true;
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Open the Preflight tab.");
            for (const StatusChip &c : info.chips) {
                ImGui::SameLine();
                chip(c);
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    return clicked;
}

} // namespace psyight
