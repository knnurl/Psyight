// Recently opened tracks (File > Open recent): newest first, no duplicates, at most kMaxRecent,
// kept in a small JSON file (psyight.recent.json in the working directory by default). A file
// that is missing or does not parse gives an empty list; it is rewritten on every change.
#pragma once

#include <filesystem>
#include <vector>

namespace psyight {

class RecentFiles {
  public:
    static constexpr size_t kMaxRecent = 10;

    explicit RecentFiles(std::filesystem::path file = "psyight.recent.json");

    void add(const std::filesystem::path &track); // Moves it to the front.
    void remove(const std::filesystem::path &track);
    void clear();
    const std::vector<std::filesystem::path> &items() const { return items_; }

  private:
    void save() const;

    std::filesystem::path file_;
    std::vector<std::filesystem::path> items_;
};

} // namespace psyight
