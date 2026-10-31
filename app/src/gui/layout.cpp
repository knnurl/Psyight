#include "gui/layout.h"

#include "gui/theme.h"

#include <imgui_internal.h> // DockBuilder API, private dock node flags, dock nodes.

#include <algorithm>

namespace psyight {

namespace {

constexpr float kLeftColumnWidthFraction = 0.76f;
constexpr float kTransportHeightEm = 5.6f;       // Two rows of controls, in font heights.
constexpr float kPreviewHeightFraction = 0.30f;  // Of the right column.
constexpr float kInspectorHeightFraction = 0.4f; // Of the right column under the preview: the
                                                 // inspector and scoring, under the tabs.
constexpr float kAnalysisHeightFraction = 0.5f;  // Of the left column under the transport.

ImGuiWindowClass pinned_class(ImGuiDockNodeFlags extra) {
    ImGuiWindowClass c;
    c.DockNodeFlagsOverrideSet =
        ImGuiDockNodeFlags_NoUndocking |
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoDockingOverMe) | extra;
    return c;
}

// Docks a tab at a given place in its group. ImGui sorts a new tab bar by each window's saved dock
// order, which an older layout in imgui.ini may still hold, so the order is set here too.
void dock_tab(const char *name, ImGuiID node, short order) {
    ImGui::DockBuilderDockWindow(name, node);
    if (ImGuiWindow *window = ImGui::FindWindowByName(name)) {
        window->DockOrder = order;
    }
    if (ImGuiWindowSettings *settings = ImGui::FindWindowSettingsByID(ImHashStr(name))) {
        settings->DockOrder = order;
    }
}

// Fills the splitter gap of every visible left-right split under node, over the split's height,
// fading towards its top and bottom.
void draw_vertical_dividers(const ImGuiDockNode *node, ImDrawList &draw_list,
                            const ImVec4 &colour) {
    if (node == nullptr || !node->IsSplitNode()) {
        return;
    }
    const ImGuiDockNode *first = node->ChildNodes[0];
    const ImGuiDockNode *second = node->ChildNodes[1];
    if (node->SplitAxis == ImGuiAxis_X && first->IsVisible && second->IsVisible) {
        const float left = first->Pos.x + first->Size.x;
        const float right = std::max(second->Pos.x, left + 1.0f);
        fill_faded(draw_list, ImVec2(left, node->Pos.y), ImVec2(right, node->Pos.y + node->Size.y),
                   colour);
    }
    draw_vertical_dividers(first, draw_list, colour);
    draw_vertical_dividers(second, draw_list, colour);
}

} // namespace

void draw_pane_dividers(ImGuiID dockspace_id) {
    const ImGuiDockNode *root = ImGui::DockBuilderGetNode(dockspace_id);
    if (root == nullptr || root->HostWindow == nullptr) {
        return;
    }
    ImDrawList &draw_list = *root->HostWindow->DrawList;
    draw_list.PushClipRectFullScreen();
    draw_vertical_dividers(root, draw_list, theme().pane_divider);
    draw_list.PopClipRect();
}

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
    ImGui::DockBuilderSplitNode(right_rest, ImGuiDir_Down, kInspectorHeightFraction, &inspector,
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
    // Tabs in this order: Log, Preflight, Rig, Show.
    dock_tab("Log", tabs, 0);
    dock_tab("Preflight", tabs, 1);
    dock_tab("Rig", tabs, 2);
    dock_tab("Show", tabs, 3);
    dock_tab("Library", tabs, 4);
    ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace psyight
