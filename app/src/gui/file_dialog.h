// The platform's file-open dialog (File > Open track). Windows: the common item dialog, modal to
// the app window; the GUI waits while it is open (audio and the show keep running).
#pragma once

#include <filesystem>
#include <optional>

namespace psyight {

// owner: the native window handle (HWND on Windows), or null. Empty when cancelled.
std::optional<std::filesystem::path> open_audio_file_dialog(void *owner);

} // namespace psyight
