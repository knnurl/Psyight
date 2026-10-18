// The operator's preferences file (io/user_settings.h).

#include "io/user_settings.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace psyight;

TEST_CASE("user settings: defaults without a file, kept across runs") {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "psyight_settings_test.json";
    std::filesystem::remove(file);
    CHECK(load_user_settings(file) == UserSettings{});

    UserSettings changed;
    changed.theme = "show";
    changed.interface_depth = "engineer";
    changed.spectrogram_colours = "viridis";
    changed.spectrogram_min_db = -75.5f;
    changed.spectrogram_max_db = -5.0f;
    changed.volume_db = -12.0f;
    changed.show_detections = false;
    changed.show_pitch = false;
    changed.snap = false;
    changed.preview_open = false;
    changed.show_mode_theme = false;
    changed.show_mode_hides_editing = false;
    save_user_settings(file, changed);
    CHECK(load_user_settings(file) == changed);
    std::filesystem::remove(file);
}

TEST_CASE("user settings: a damaged file keeps the defaults it cannot say") {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "psyight_settings_bad.json";
    std::ofstream(file, std::ios::binary) << "{\"theme\": \"show\", \"volume_db\": \"loud\", "
                                             "\"snap\": 0}";
    const UserSettings loaded = load_user_settings(file);
    CHECK(loaded.theme == "show");
    CHECK(loaded.volume_db == UserSettings{}.volume_db); // Wrong type: the default.
    CHECK(loaded.snap == UserSettings{}.snap);
    std::ofstream(file, std::ios::binary) << "not json";
    CHECK(load_user_settings(file) == UserSettings{});
    std::filesystem::remove(file);
}
