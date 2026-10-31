// The operator's preferences that outlive a session (theme, interface depth, spectrogram colours,
// the analysis plots' heights, volume, bass roll display, rig preview, show mode), kept in a small
// JSON file (psyight.settings.json in the working directory by default). Not the project (rig,
// scenes: io/project.h) and not the window layout (imgui.ini). A missing file gives the defaults;
// a field that is missing or has the wrong type keeps its default, so an older or damaged file
// loses only what it cannot say.
#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace psyight {

struct UserSettings {
    std::string theme = "desk";                // desk, show.
    std::string interface_depth = "tune";      // operate, tune, engineer.
    std::string spectrogram_colours = "magma"; // magma, inferno, viridis.
    float spectrogram_min_db = -90.0f;
    float spectrogram_max_db = -10.0f;
    // Shares of the analysis view's height: spectrogram, band energy, flux, residual (dragged
    // with the splitters between the plots). Kept only as four positive numbers.
    std::array<float, 4> analysis_plot_shares{0.55f, 0.25f, 0.20f, 0.30f};
    float volume_db = 0.0f;
    bool show_detections = true; // Bass roll.
    bool show_pitch = true;      // Bass roll pitch background.
    bool snap = true;            // Bass roll: snap to 1/16.
    bool preview_open = true;    // The rig preview (folded when false).
    // The rig preview at full brightness: the brightness cap, Ambient's half and the power
    // budget undone on screen, while the lights stay dimmed.
    bool preview_full_brightness = false;
    // Show mode (React and Show on the top bar): the Show theme, and the editing panels hidden.
    bool show_mode_theme = true;
    bool show_mode_hides_editing = true;
    // The labelling mode (gui/labelling_view.h): its music folders (UTF-8 paths) and which
    // tracks its list shows (all, not started, in progress, done).
    std::vector<std::string> label_folders;
    std::string label_filter = "all";
    // Detailed section names (SPEC 6.6.1: phases, roles, pre-drops, breaks and fills) in the
    // labelling mode, or the previous six names.
    bool detailed_sections = true;
    // The media player (gui/player.h): its library folders and rekordbox XML export (UTF-8
    // paths), and the queue with the index of its current track (-1: none).
    std::vector<std::string> library_folders;
    std::string rekordbox_xml;
    std::vector<std::string> playlist_files; // M3U, M3U8 or rekordbox TXT exports.
    std::vector<std::string> queue;
    int queue_index = -1;

    bool operator==(const UserSettings &) const = default;
};

UserSettings load_user_settings(const std::filesystem::path &file);
void save_user_settings(const std::filesystem::path &file, const UserSettings &settings);

} // namespace psyight
