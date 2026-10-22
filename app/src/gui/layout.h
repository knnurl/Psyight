// Default docked layout (RESTYLE Phase 4, the owner's choice of three). Left column: a compact
// transport strip on top (controls beside the waveform overview), then analysis and the bass
// roll. Right column: the rig preview pinned on top (it folds away), the note inspector with
// scoring as a tab, then show controls with the log, rig and preflight as tabs. In show mode the
// large rig view (Stage) takes the bass roll's place. The bottom right
// node is the dock space's central node, so it stays when panels are hidden (show mode, a folded
// preview) and the others give their space to their neighbours. Applied on first run (no saved
// layout for the main dock space) and from View > Reset layout; captures always use it.
#pragma once

#include <imgui.h>

namespace psyight {

void apply_default_layout(ImGuiID dockspace_id);

// Thin lines (theme pane_divider) over the splitters between panels docked side by side, so the
// columns read apart; call after the dock space each frame. Drawn in the dock space's host window,
// under the panels and popups.
void draw_pane_dividers(ImGuiID dockspace_id);

// Window classes for the pinned panels: set with ImGui::SetNextWindowClass before Begin.
// The transport strip: no tab bar, not undockable, nothing docks over it.
const ImGuiWindowClass &transport_window_class();
// The rig preview: not undockable, nothing docks over it.
const ImGuiWindowClass &preview_window_class();

} // namespace psyight
