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

void SettingsView::draw(bool *open, bool &reset_layout, UserSettings &prefs) {
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

    ImGui::SeparatorText("Show mode");
    ImGui::Checkbox("Switch to the Show theme", &prefs.show_mode_theme);
    ImGui::Checkbox("Hide the editing panels (analysis, bass roll, note inspector)",
                    &prefs.show_mode_hides_editing);
    help("Show mode is React or Show on the top bar. With the editing panels hidden, the rig is "
         "drawn large in their place. Leaving show mode restores the theme and panels.");

    ImGui::SeparatorText("Project");
    if (project_.untitled()) {
        ImGui::TextWrapped("Untitled (not saved to a file yet)%s",
                           project_.dirty() ? ", unsaved changes" : "");
    } else {
        std::error_code error;
        const std::filesystem::path where = std::filesystem::absolute(project_.path(), error);
        ImGui::TextWrapped("%s%s", path_to_utf8(error ? project_.path() : where).c_str(),
                           project_.dirty() ? " (unsaved changes)" : "");
    }
    help("The rig, patch, output, limiters, scenes, palettes and MIDI. The file changes only when "
         "you save (Ctrl+S); until then a recovery copy is kept beside it, and offered back if "
         "Psyight closes without saving.");
    if (primary_button("Save")) {
        project_.request(project_.untitled() ? ProjectRequest::save_as : ProjectRequest::save);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as...")) {
        project_.request(ProjectRequest::save_as);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(project_.untitled() || !project_.dirty());
    if (ImGui::Button("Revert to saved")) {
        project_.request(ProjectRequest::open_path, project_.path());
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Opens the file again; you are asked before the changes are dropped.");

    ImGui::SeparatorText("Layout");
    if (ImGui::Button("Reset layout")) {
        reset_layout = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Also View > Reset layout.");
    ImGui::End();
}

} // namespace psyight
