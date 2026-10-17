// Default docked layout: transport across the top, analysis and bass roll below, the note
// inspector (scoring as a tab) and the preview (with the rig, preflight and log panels as tabs) on
// the right. Applied on
// first run (no saved layout for the main dock space) and from View > Reset layout.
#pragma once

#include <imgui.h>

namespace psyight {

void apply_default_layout(ImGuiID dockspace_id);

} // namespace psyight
