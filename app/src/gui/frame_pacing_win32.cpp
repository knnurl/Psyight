#include "gui/frame_pacing.h"

#include <windows.h>

#include <dwmapi.h>

namespace psyight {

namespace {

constexpr DWORD kFallbackFrameMs = 16; // About 60 Hz if the compositor cannot be waited on.

} // namespace

void wait_for_display_refresh() {
    if (FAILED(DwmFlush())) {
        Sleep(kFallbackFrameMs);
    }
}

} // namespace psyight
