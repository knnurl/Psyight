// The operator's preferences that outlive a session (theme, interface depth, spectrogram colours,
// volume, bass roll display), kept in a small JSON file (psyight.settings.json in the working
// directory by default). Not the project (rig, scenes: io/project.h) and not the window layout
// (imgui.ini). A missing file gives the defaults; a field that is missing or has the wrong type
// keeps its default, so an older or damaged file loses only what it cannot say.
#pragma once

#include <filesystem>
#include <string>

namespace psyight {

struct UserSettings {
    std::string theme = "desk";                // desk, show.
    std::string interface_depth = "tune";      // operate, tune, engineer.
    std::string spectrogram_colours = "magma"; // magma, inferno, viridis.
    float spectrogram_min_db = -90.0f;
    float spectrogram_max_db = -10.0f;
    float volume_db = 0.0f;
    bool show_detections = true; // Bass roll.
    bool show_pitch = true;      // Bass roll pitch background.
    bool snap = true;            // Bass roll: snap to 1/16.

    bool operator==(const UserSettings &) const = default;
};

UserSettings load_user_settings(const std::filesystem::path &file);
void save_user_settings(const std::filesystem::path &file, const UserSettings &settings);

} // namespace psyight
