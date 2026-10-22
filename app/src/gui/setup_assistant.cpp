#include "gui/setup_assistant.h"

#include "gui/theme.h"
#include "gui/widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace psyight {

namespace {

constexpr double kSoundMsPerMetre = 1000.0 / 343.0; // About 2.9 ms per metre (SPEC 4.3).
constexpr const char *kSteps[] = {"Rig", "Output", "Test patterns", "Latency", "Check"};
constexpr int kStepCount = 5;

void help(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

void SetupAssistant::draw() {
    if (!open_) {
        return;
    }
    const float em = ImGui::GetFontSize();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(48.0f * em, 36.0f * em), ImGuiCond_Appearing);
    if (!ImGui::Begin("Setup assistant", &open_,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    // The steps, as a segmented row: any can be visited.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, ImGui::GetStyle().ItemSpacing.y));
    for (int i = 0; i < kStepCount; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        char label[48];
        std::snprintf(label, sizeof(label), "%d  %s", i + 1, kSteps[i]);
        if (toggle_button(label, step_ == i)) {
            step_ = i;
        }
    }
    ImGui::PopStyleVar();
    ImGui::Separator();

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##step", ImVec2(0.0f, -footer));
    switch (step_) {
    case 0: {
        const Project &p = project_.project();
        const auto pixels = static_cast<int>(project_.rig().pixels.size());
        ImGui::Text("The rig now: %zu strips, %d pixels.", p.strips.size(), pixels);
        help("Start from a template that matches your strips (pillars or a matrix panel), a "
             "preset shape or one of your own saved rigs, or keep it and edit the strips by hand "
             "in the Rig tab. The preview (top right) shows "
             "the result; hover it to see each strip's name and where its data comes in.");
        ImGui::Spacing();
        rig_.draw_template_controls();
        break;
    }
    case 1: {
        help("How the show reaches the node: sACN or Art-Net, multicast or the node's address, "
             "and the first universe. Then find the node on the network.");
        ImGui::Spacing();
        const std::shared_ptr<const ShowFrame> frame = preview_.latest_frame();
        preview_.draw_output(frame != nullptr ? *frame : ShowFrame{});
        ImGui::SeparatorText("Nodes");
        preflight_.draw_discovery();
        break;
    }
    case 2:
        help("Check the wiring on the real strips: Chase shows each strip's direction and "
             "order, Colour order catches a wrong RGB order (fix it in the Rig tab). Turn the "
             "pattern off when done.");
        ImGui::Spacing();
        preflight_.draw_test_patterns();
        break;
    case 3:
        draw_latency();
        break;
    default:
        draw_check();
        break;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginDisabled(step_ == 0);
    if (ImGui::Button("Back")) {
        step_ = std::max(0, step_ - 1);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (step_ + 1 < kStepCount) {
        if (primary_button("Next")) {
            ++step_;
        }
    } else if (primary_button("Finish")) {
        open_ = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Step %d of %d. Everything here is also in the Rig, Show and Preflight "
                        "tabs.",
                        step_ + 1, kStepCount);
    ImGui::End();
}

void SetupAssistant::draw_latency() {
    help("The lights should flash when the audience hears the kick. Sound takes about 2.9 ms "
         "a metre, so the lights wait for it: a negative offset.");
    ImGui::Spacing();
    const float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(8.0f * em);
    drag_double("Speakers to the audience", &distance_m_, 0.1f, 0.0, 100.0, "%.1f m");
    const double suggested = -kSoundMsPerMetre * distance_m_;
    ImGui::SameLine();
    char label[64];
    std::snprintf(label, sizeof(label), "Set the offset to %+.0f ms", suggested);
    if (primary_button(label)) {
        project_.edit().latency_offset_ms = std::clamp(suggested, -200.0, 200.0);
        project_.changed();
    }
    double offset = project_.project().latency_offset_ms;
    ImGui::SetNextItemWidth(8.0f * em);
    if (drag_double("Latency offset now", &offset, 1.0f, -200.0, 200.0, "%+.0f ms")) {
        project_.edit().latency_offset_ms = offset;
        project_.changed();
    }
    ImGui::Spacing();
    help("Live mode measures it better: in the Show tab, start Live input, then use the tap test "
         "from where the audience stands. The network and the node add a little: finish by eye.");
}

void SetupAssistant::draw_check() {
    int problems = 0;
    for (const PreflightCheck &c : preflight_.checks()) {
        if (c.level != CheckLevel::fail && c.level != CheckLevel::warn) {
            continue;
        }
        ++problems;
        const ImVec4 colour = c.level == CheckLevel::fail ? theme().danger : theme().warning;
        ImGui::TextColored(colour, "%s", c.area.c_str());
        ImGui::SameLine(6.0f * ImGui::GetFontSize());
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextWrapped("%s", c.message.c_str());
        ImGui::PopStyleColor();
    }
    if (problems == 0) {
        ImGui::TextColored(theme().success, "Every preflight check passes.");
    }
    ImGui::Spacing();
    help("Before the show: step up the top bar from Preview to Ambient, React or Show to send to "
         "the lights. Blackout (B) and Hold (H) work in every state. Save the project (Rig tab, or "
         "it saves itself within a minute).");
}

} // namespace psyight
