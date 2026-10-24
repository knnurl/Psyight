// The platform's file dialogs (File > Open track, the project's Open and Save as). Windows: the
// common item dialogs, modal to the app window; the GUI waits while one is open (audio and the
// show keep running). The dialogs need a single-threaded COM apartment, but miniaudio makes the
// GUI thread multithreaded when it opens the audio device (and its WASAPI code needs that), so a
// dialog shown after a track loaded runs on a thread of its own while the GUI thread keeps
// handling its window's messages; shown from the GUI thread itself, it would hang the app.
#pragma once

#include <filesystem>
#include <optional>

namespace psyight {

// owner: the native window handle (HWND on Windows), or null. Empty when cancelled.
std::optional<std::filesystem::path> open_audio_file_dialog(void *owner);
std::optional<std::filesystem::path> open_project_file_dialog(void *owner);
// An SVG or DXF drawing (Rig > New rig > From drawing).
std::optional<std::filesystem::path> open_svg_file_dialog(void *owner);
// Where to save the rig as a drawing: an SVG or (dxf) a DXF; the extension is added when missing.
std::optional<std::filesystem::path>
save_drawing_file_dialog(void *owner, const std::filesystem::path &suggested, bool dxf);
// A folder (the labelling mode's music folders).
std::optional<std::filesystem::path> pick_folder_dialog(void *owner);
// suggested: the file name (and folder, when it has one) to start from. The chosen path always
// ends in .psyproj; the dialog asks before replacing an existing file.
std::optional<std::filesystem::path>
save_project_file_dialog(void *owner, const std::filesystem::path &suggested);

} // namespace psyight
