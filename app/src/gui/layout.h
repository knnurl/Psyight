// Default docked layout: transport across the top, analysis below. Applied on first run (no saved
// layout for the main dock space) and from View > Reset layout.
#pragma once

#include <imgui.h>

namespace psyight {

void apply_default_layout(ImGuiID dockspace_id);

} // namespace psyight
