#include "io/media_library.h"

#include "core/paths.h"
#include "io/text_file.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>

namespace psyight {

namespace {

using json = nlohmann::json;

bool audio_file(const std::filesystem::path &path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".wav" || ext == ".flac" || ext == ".mp3";
}

} // namespace

std::string display_title(const LibraryTrack &track) {
    return track.tags.title.empty() ? path_to_utf8(track.path.stem()) : track.tags.title;
}

MediaLibrary scan_library(const std::vector<std::filesystem::path> &roots,
                          const MediaLibrary &previous, std::atomic<int> *scanned,
                          const std::atomic<bool> *cancel) {
    std::map<std::filesystem::path, const LibraryTrack *> known;
    for (const LibraryTrack &t : previous.tracks) {
        known[t.path] = &t;
    }
    std::map<std::filesystem::path, LibraryTrack> found; // By path: sorted, once each.
    for (const std::filesystem::path &root : roots) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        for (; !error && it != std::filesystem::recursive_directory_iterator();
             it.increment(error)) {
            if (cancel != nullptr && cancel->load()) {
                break;
            }
            std::error_code e;
            if (!it->is_regular_file(e) || !audio_file(it->path())) {
                continue;
            }
            const std::filesystem::path path = it->path().lexically_normal();
            if (found.contains(path)) {
                continue;
            }
            LibraryTrack track;
            track.path = path;
            track.root = path_to_utf8(root);
            const std::filesystem::path parent = path.parent_path().lexically_relative(root);
            track.folder = parent == "." ? std::string() : path_to_utf8(parent.generic_u8string());
            track.size = it->file_size(e);
            track.modified = it->last_write_time(e).time_since_epoch().count();
            const auto old = known.find(path);
            track.tags = old != known.end() && old->second->size == track.size &&
                                 old->second->modified == track.modified
                             ? old->second->tags
                             : read_track_tags(path);
            found.emplace(path, std::move(track));
            if (scanned != nullptr) {
                scanned->fetch_add(1);
            }
        }
    }
    MediaLibrary library;
    library.tracks.reserve(found.size());
    for (auto &[path, track] : found) {
        library.tracks.push_back(std::move(track));
    }
    return library;
}

Result<MediaLibrary> load_library_index(const std::filesystem::path &file) {
    Result<std::string> text = read_text_file(file);
    if (!text) {
        return text.error();
    }
    const json root = json::parse(text.value(), nullptr, false);
    if (!root.is_object() || !root.contains("tracks") || !root["tracks"].is_array()) {
        return Error{"not a library index: " + path_to_utf8(file)};
    }
    MediaLibrary library;
    for (const json &t : root["tracks"]) {
        if (!t.is_object() || !t.contains("path") || !t["path"].is_string()) {
            continue;
        }
        LibraryTrack track;
        track.path = utf8_to_path(t["path"].get<std::string>());
        track.root = t.value("root", std::string());
        track.folder = t.value("folder", std::string());
        track.size = t.value("size", std::uintmax_t{0});
        track.modified = t.value("modified", std::int64_t{0});
        track.tags.title = t.value("title", std::string());
        track.tags.artist = t.value("artist", std::string());
        track.tags.album = t.value("album", std::string());
        track.tags.key = t.value("key", std::string());
        track.tags.bpm = t.value("bpm", 0.0);
        track.tags.duration_s = t.value("duration_s", 0.0);
        library.tracks.push_back(std::move(track));
    }
    std::sort(library.tracks.begin(), library.tracks.end(),
              [](const LibraryTrack &a, const LibraryTrack &b) { return a.path < b.path; });
    return library;
}

Result<void> save_library_index(const std::filesystem::path &file, const MediaLibrary &library) {
    json tracks = json::array();
    for (const LibraryTrack &t : library.tracks) {
        tracks.push_back({{"path", path_to_utf8(t.path)},
                          {"root", t.root},
                          {"folder", t.folder},
                          {"size", t.size},
                          {"modified", t.modified},
                          {"title", t.tags.title},
                          {"artist", t.tags.artist},
                          {"album", t.tags.album},
                          {"key", t.tags.key},
                          {"bpm", t.tags.bpm},
                          {"duration_s", t.tags.duration_s}});
    }
    // Tags are not always valid UTF-8 in the wild: replace rather than fail.
    return write_text_file_atomic(
        file, json{{"tracks", tracks}}.dump(1, ' ', false, json::error_handler_t::replace) + "\n");
}

} // namespace psyight
