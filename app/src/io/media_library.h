// The media player's library (SPEC 9.5): the WAV, FLAC and MP3 files under the folders the
// operator adds, with their tags (io/audio_tags.h), kept in an index file
// (psyight.library.json in the working directory) so the app starts with it and a rescan reads
// only files whose size or time changed. Scanning runs on a worker thread (the GUI's).
#pragma once

#include "core/result.h"
#include "io/audio_tags.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace psyight {

struct LibraryTrack {
    std::filesystem::path path;
    std::string root;   // The library folder it was found under (UTF-8).
    std::string folder; // Its folder under that root, '/' separated; empty at the root itself.
    TrackTags tags;
    std::uintmax_t size = 0;
    std::int64_t modified = 0; // The file's write time (ticks), to spot a changed file.

    bool operator==(const LibraryTrack &) const = default;
};

struct MediaLibrary {
    std::vector<LibraryTrack> tracks; // Sorted by path.
};

// The title to show: the tag, else the file name without its extension.
std::string display_title(const LibraryTrack &track);

// Every WAV, FLAC and MP3 under the roots (all depths; unreadable folders skipped; a file under
// two roots listed once), reusing `previous`'s tags for files of the same size and time. Counts
// files into *scanned as it goes; stops early (returning what it has) when *cancel is set.
MediaLibrary scan_library(const std::vector<std::filesystem::path> &roots,
                          const MediaLibrary &previous, std::atomic<int> *scanned = nullptr,
                          const std::atomic<bool> *cancel = nullptr);

Result<MediaLibrary> load_library_index(const std::filesystem::path &file);
Result<void> save_library_index(const std::filesystem::path &file, const MediaLibrary &library);

} // namespace psyight
