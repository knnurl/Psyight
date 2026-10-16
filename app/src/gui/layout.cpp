#include "gui/layout.h"

#include <imgui_internal.h> // DockBuilder API.

namespace psyight {

namespace {

constexpr float kTransportHeightFraction = 0.18f;
constexpr float kInspectorWidthFraction = 0.22f;
constexpr float kAnalysisHeightFraction = 0.5f;
constexpr float kPreviewHeightFraction = 0.45f; // Of the inspector column.

} // namespace

void apply_default_layout(ImGuiID dockspace_id) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
    ImGuiID top = 0;
    ImGuiID rest = 0;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Up, kTransportHeightFraction, &top, &rest);
    ImGuiID inspector = 0;
    ImGuiID views = 0;
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, kInspectorWidthFraction, &inspector, &views);
    ImGuiID preview = 0;
    ImGui::DockBuilderSplitNode(inspector, ImGuiDir_Down, kPreviewHeightFraction, &preview,
                                &inspector);
    ImGuiID analysis = 0;
    ImGuiID bass_roll = 0;
    ImGui::DockBuilderSplitNode(views, ImGuiDir_Up, kAnalysisHeightFraction, &analysis, &bass_roll);
    ImGui::DockBuilderDockWindow("Transport", top);
    ImGui::DockBuilderDockWindow("Note inspector", inspector);
    ImGui::DockBuilderDockWindow("Rig", preview); // Tabbed behind the preview.
    ImGui::DockBuilderDockWindow("Preflight", preview);
    ImGui::DockBuilderDockWindow("Preview", preview);
    ImGui::DockBuilderDockWindow("Analysis", analysis);
    ImGui::DockBuilderDockWindow("Bass roll", bass_roll);
    ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace psyight
