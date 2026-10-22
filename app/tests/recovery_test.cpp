// Recovery copies (io/recovery.h) and the recent projects list (io/recent_files.h, its own key).

#include "io/recent_files.h"
#include "io/recovery.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace psyight;

namespace {

void write(const std::filesystem::path &file, const char *text) {
    std::ofstream(file, std::ios::binary) << text;
}

} // namespace

TEST_CASE("recovery: a copy beside the file, offered only when newer than it") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "psyight_recovery";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "show.psyproj";
    CHECK(recovery_path(file) == dir / "show.psyproj.autosave");

    // Neither exists, or only the file: nothing to offer.
    CHECK_FALSE(recovery_newer(file));
    CHECK(recovery_age(file).empty());
    write(file, "{}");
    CHECK_FALSE(recovery_newer(file));

    // A copy newer than the file is offered, with its age.
    write(recovery_path(file), "{}");
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(file, now - std::chrono::minutes(10));
    std::filesystem::last_write_time(recovery_path(file), now - std::chrono::minutes(3));
    CHECK(recovery_newer(file));
    CHECK(recovery_age(file) == "3 minutes ago");

    // A copy older than the file (saved since) is not.
    std::filesystem::last_write_time(file, now);
    CHECK_FALSE(recovery_newer(file));

    // A copy without its file (an untitled project, labels never saved) is.
    std::filesystem::remove(file);
    CHECK(recovery_newer(file));
    std::filesystem::last_write_time(recovery_path(file), now - std::chrono::hours(30));
    CHECK(recovery_age(file) == "yesterday");

    remove_recovery(file);
    CHECK_FALSE(std::filesystem::exists(recovery_path(file)));
    CHECK_FALSE(recovery_newer(file));
    remove_recovery(file); // Nothing to remove: fine.
    std::filesystem::remove_all(dir);
}

TEST_CASE("recent files: projects keep their own list under their own key") {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "psyight_recent_projects_test.json";
    std::filesystem::remove(file);
    {
        RecentFiles projects(file, "projects");
        projects.add("D:/shows/forest.psyproj");
        projects.add("D:/shows/club.psyproj");
    }
    const RecentFiles projects(file, "projects");
    REQUIRE(projects.items().size() == 2);
    CHECK(projects.items()[0] == std::filesystem::path("D:/shows/club.psyproj"));
    CHECK(RecentFiles(file, "tracks").items().empty()); // Another key reads nothing.
    std::filesystem::remove(file);
}
