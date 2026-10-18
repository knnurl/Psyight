// The recent tracks list (io/recent_files.h).

#include "io/recent_files.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace psyight;

TEST_CASE("recent files: newest first, no duplicates, capped, kept across runs") {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "psyight_recent_test.json";
    std::filesystem::remove(file);
    {
        RecentFiles recent(file);
        CHECK(recent.items().empty());
        recent.add("C:/music/a.mp3");
        recent.add("C:/music/b.wav");
        recent.add("C:/music/./a.mp3"); // The same file: moves to the front.
        REQUIRE(recent.items().size() == 2);
        CHECK(recent.items()[0].lexically_normal() == std::filesystem::path("C:/music/a.mp3"));
        CHECK(recent.items()[1] == std::filesystem::path("C:/music/b.wav"));
        for (int i = 0; i < 20; ++i) {
            recent.add("C:/music/t" + std::to_string(i) + ".mp3");
        }
        CHECK(recent.items().size() == RecentFiles::kMaxRecent);
        CHECK(recent.items()[0] == std::filesystem::path("C:/music/t19.mp3"));
        recent.add(std::filesystem::path(u8"C:/music/Kab\u00e1yun - \u00dcber.mp3"));
    }
    {
        const RecentFiles again(file);
        REQUIRE(again.items().size() == RecentFiles::kMaxRecent);
        CHECK(again.items()[0] == std::filesystem::path(u8"C:/music/Kab\u00e1yun - \u00dcber.mp3"));
        CHECK(again.items()[1] == std::filesystem::path("C:/music/t19.mp3"));
    }
    {
        RecentFiles again(file);
        again.remove("C:/music/t19.mp3");
        CHECK(again.items().size() == RecentFiles::kMaxRecent - 1);
        again.clear();
        CHECK(RecentFiles(file).items().empty());
    }
    std::ofstream(file) << "not json";
    CHECK(RecentFiles(file).items().empty()); // A damaged file: an empty list, no crash.
    std::filesystem::remove(file);
}
