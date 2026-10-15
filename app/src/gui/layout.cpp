#include "gui/layout.h"

#include <imgui_internal.h> // DockBuilder API.

namespace psyight {

namespace {

constexpr float kTransportHeightFraction = 0.22f;

} // namespace

void apply_default_layout(ImGuiID dockspace_id) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
    ImGuiID top = 0;
    ImGuiID bottom = 0;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Up, kTransportHeightFraction, &top, &bottom);
    ImGui::DockBuilderDockWindow("Transport", top);
    ImGui::DockBuilderDockWindow("Analysis", bottom);
    ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace psyight
