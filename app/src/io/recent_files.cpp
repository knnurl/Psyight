#include "io/recent_files.h"

#include "core/paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <string>

namespace psyight {

namespace {

// The same file however it was written (case and separators differ on Windows).
bool same_file(const std::filesystem::path &a, const std::filesystem::path &b) {
    std::error_code error;
    if (std::filesystem::equivalent(a, b, error)) {
        return true;
    }
    return a.lexically_normal() == b.lexically_normal();
}

} // namespace

RecentFiles::RecentFiles(std::filesystem::path file, std::string key)
    : file_(std::move(file)), key_(std::move(key)) {
    std::ifstream in(file_, std::ios::binary);
    if (!in) {
        return;
    }
    const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
    if (!root.is_object() || !root.contains(key_) || !root[key_].is_array()) {
        return;
    }
    for (const nlohmann::json &t : root[key_]) {
        if (t.is_string() && items_.size() < kMaxRecent) {
            const std::string utf8 = t.get<std::string>();
            items_.push_back(std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size())));
        }
    }
}

void RecentFiles::add(const std::filesystem::path &track) {
    std::erase_if(items_, [&](const std::filesystem::path &p) { return same_file(p, track); });
    items_.insert(items_.begin(), track);
    if (items_.size() > kMaxRecent) {
        items_.resize(kMaxRecent);
    }
    save();
}

void RecentFiles::remove(const std::filesystem::path &track) {
    std::erase_if(items_, [&](const std::filesystem::path &p) { return same_file(p, track); });
    save();
}

void RecentFiles::clear() {
    items_.clear();
    save();
}

void RecentFiles::save() const {
    nlohmann::json list = nlohmann::json::array();
    for (const std::filesystem::path &p : items_) {
        list.push_back(path_to_utf8(p));
    }
    std::ofstream(file_, std::ios::binary) << nlohmann::json{{key_, list}}.dump(2) << "\n";
}

} // namespace psyight
