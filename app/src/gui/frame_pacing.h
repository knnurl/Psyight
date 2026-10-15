// Paces the GUI thread to the display refresh without busy-waiting.
//
// Assumes windowed OpenGL presentation through the desktop compositor. Vsync through the swap
// interval made the Intel OpenGL driver (32.0.101.6913) spin one CPU core at 60 fps, so the swap
// interval is 0 and the thread blocks on the compositor instead (measured: 104% to 5% of a core).
#pragma once

namespace psyight {

// Swap interval to set on the GUI's OpenGL context when using this pacing.
constexpr int kGuiSwapInterval = 0;

// Blocks until the compositor presents its next frame. Call once per frame after swapping buffers.
void wait_for_display_refresh();

} // namespace psyight
