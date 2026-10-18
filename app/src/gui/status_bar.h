// The status bar (SPEC 9.3 status chips and a single fault banner), along the bottom of the
// window: the top bar is full with the menus, the state ladder and the safety controls. The
// banner on the left names the worst preflight failure in red (or counts the warnings in amber);
// clicking it opens the Preflight tab. Chips follow: short facts, each with a tooltip.
// Submit it before the dock space, so the docked panels make room for it.
#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace psyight {

enum class ChipTone { plain, dim, good, warn, bad, accent };

struct StatusChip {
    std::string text;
    std::string tooltip;
    ChipTone tone = ChipTone::plain;
};

struct StatusInfo {
    std::string fault; // The worst failure's message; empty when none fails.
    int failures = 0;
    int warnings = 0;
    std::vector<StatusChip> chips;
};

// Returns true when the banner was clicked.
bool draw_status_bar(const StatusInfo &info);

} // namespace psyight
