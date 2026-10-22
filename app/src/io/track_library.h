// The labelling mode's track list (gui/labelling_view.h): the audio files in a set of folders
// (and their subfolders), each with its section-labelling status from its labels file.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

// none: no labelled sections or phrases yet; started: some, not marked done; done: the owner
// marked the track's sections done (Labels::sections_done).
enum class LabelStatus { none, started, done };
const char *label_status_name(LabelStatus status); // "Not started", "In progress", "Done".

struct TrackEntry {
    std::filesystem::path path;
    std::string name;   // File name, UTF-8.
    std::string folder; // The parent folder's name, UTF-8 (tracks sit in album folders).
    LabelStatus status = LabelStatus::none;
    int sections = 0; // Labelled sections.
};

// Reads the track's labels file (when it exists). An unreadable file counts as started, so it
// is never offered as untouched.
LabelStatus label_status(const std::filesystem::path &audio, int *sections = nullptr);

// WAV, FLAC and MP3 files under each folder (subfolders included, at most `max_depth` deep),
// sorted by path; a file found through two folders is listed once. Unreadable folders are skipped.
std::vector<TrackEntry> scan_track_folders(const std::vector<std::filesystem::path> &folders,
                                           int max_depth = 4);

// The next track after `from` (wrapping, `from` itself last) whose status is not done; with
// `from` past the end, from the start. nullopt when every track is done.
std::optional<size_t> next_unfinished(const std::vector<TrackEntry> &tracks, size_t from);

} // namespace psyight
