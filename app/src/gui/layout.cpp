#include "gui/layout.h"

#include <imgui_internal.h> // DockBuilder API, private dock node flags.

#include <algorithm>

namespace psyight {

namespace {

constexpr float kLeftColumnWidthFraction = 0.76f;
constexpr float kTransportHeightEm = 5.6f;       // Two rows of controls, in font heights.
constexpr float kPreviewHeightFraction = 0.30f;  // Of the right column.
constexpr float kInspectorHeightFraction = 0.5f; // Of the right column under the preview.
constexpr float kAnalysisHeightFraction = 0.5f;  // Of the left column under the transport.

ImGuiWindowClass pinned_class(ImGuiDockNodeFlags extra) {
    ImGuiWindowClass c;
    c.DockNodeFlagsOverrideSet =
        ImGuiDockNodeFlags_NoUndocking |
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoDockingOverMe) | extra;
    return c;
}

} // namespace

const ImGuiWindowClass &transport_window_class() {
    static const ImGuiWindowClass c =
        pinned_class(static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoTabBar));
    return c;
}

const ImGuiWindowClass &preview_window_class() {
    static const ImGuiWindowClass c = pinned_class(ImGuiDockNodeFlags_None);
    return c;
}

void apply_default_layout(ImGuiID dockspace_id) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
    // Each split leaves the central node on the side away from the split direction, so the
    // bottom right node ends up central.
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, kLeftColumnWidthFraction, &left,
                                &right);
    ImGuiID preview = 0;
    ImGuiID right_rest = 0;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, kPreviewHeightFraction, &preview, &right_rest);
    ImGuiID inspector = 0;
    ImGuiID tabs = 0;
    ImGui::DockBuilderSplitNode(right_rest, ImGuiDir_Up, kInspectorHeightFraction, &inspector,
                                &tabs);
    ImGuiID transport = 0;
    ImGuiID views = 0;
    const float transport_fraction =
        kTransportHeightEm * ImGui::GetFontSize() / std::max(1.0f, viewport->WorkSize.y);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, transport_fraction, &transport, &views);
    ImGuiID analysis = 0;
    ImGuiID bass_roll = 0;
    ImGui::DockBuilderSplitNode(views, ImGuiDir_Up, kAnalysisHeightFraction, &analysis, &bass_roll);
    ImGui::DockBuilderDockWindow("Transport", transport);
    ImGui::DockBuilderDockWindow("Analysis", analysis);
    ImGui::DockBuilderDockWindow("Bass roll", bass_roll);
    ImGui::DockBuilderDockWindow("Stage", bass_roll); // Show mode only, while the roll is hidden.
    ImGui::DockBuilderDockWindow("Preview", preview);
    ImGui::DockBuilderDockWindow("Note inspector", inspector);
    ImGui::DockBuilderDockWindow("Scoring", inspector); // Tabbed behind the inspector.
    ImGui::DockBuilderDockWindow("Show", tabs);
    ImGui::DockBuilderDockWindow("Log", tabs); // Tabbed behind the show controls.
    ImGui::DockBuilderDockWindow("Rig", tabs);
    ImGui::DockBuilderDockWindow("Preflight", tabs);
    ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace psyight
