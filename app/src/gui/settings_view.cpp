#include "gui/settings_view.h"

#include "core/paths.h"
#include "gui/theme.h"
#include "gui/tier.h"
#include "gui/widgets.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <filesystem>

namespace psyight {

namespace {

// Dim help text that wraps to the window (a long line would be cut off).
void help(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

void SettingsView::draw(bool *open, bool &reset_layout) {
    if (!*open) {
        return;
    }
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(34.0f * em, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Appearance");
    const ThemeMode mode = theme_mode();
    if (ImGui::RadioButton("Desk", mode == ThemeMode::desk)) {
        apply_psyight_theme(ThemeMode::desk);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Show (dark room)", mode == ThemeMode::show)) {
        apply_psyight_theme(ThemeMode::show);
    }
    help("Colours can be overridden in theme.json next to psyight.exe.");

    ImGui::SeparatorText("Interface depth");
    for (const Tier t : {Tier::operate, Tier::tune, Tier::engineer}) {
        if (ImGui::RadioButton(tier_name(t), current_tier() == t)) {
            set_current_tier(t);
        }
        if (t != Tier::engineer) {
            ImGui::SameLine();
        }
    }
    help("Operate: show controls. Tune: scenes and settings. Engineer: everything.");

    ImGui::SeparatorText("Project");
    std::error_code error;
    const std::filesystem::path where = std::filesystem::absolute(project_.path(), error);
    ImGui::TextWrapped("%s%s", path_to_utf8(error ? project_.path() : where).c_str(),
                       project_.dirty() ? " (unsaved changes)" : "");
    help("The rig, patch, output, limiters, scenes, palettes and MIDI. Saved by itself a minute "
         "after a change and on exit.");
    if (primary_button("Save now")) {
        if (const Result<void> saved = project_.save(); !saved) {
            spdlog::error("{}", saved.error().message);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!std::filesystem::exists(project_.path()));
    if (ImGui::Button("Reload from disk")) {
        if (const Result<void> loaded = project_.load(); !loaded) {
            spdlog::error("{}", loaded.error().message);
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Discards changes since the last save.");

    ImGui::SeparatorText("Layout");
    if (ImGui::Button("Reset layout")) {
        reset_layout = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Also View > Reset layout.");
    ImGui::End();
}

} // namespace psyight
