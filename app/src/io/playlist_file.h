// Playlist files for the media player's library (SPEC 9.5), besides rekordbox's collection XML:
// - M3U and M3U8: one path a line (relative to the playlist's folder, or file:// URLs); #EXTINF
//   lines give a title ("Artist - Title") for the line after.
// - rekordbox's playlist export as text (Export a playlist to a file > TXT): a tab-separated
//   table, usually UTF-16 with a byte-order mark, its first row the column names. A Location (or
//   File Name, Path) column gives the file; otherwise the rows carry Track Title and Artist, which
//   the player matches against the library.
// UTF-8 and UTF-16 (either byte order, with a mark) are read.
#pragma once

#include "core/result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

struct PlaylistEntry {
    std::filesystem::path path; // Empty when the file only names the track.
    std::string title;
    std::string artist;
};

struct PlaylistFile {
    std::string name; // The file's name without its extension.
    std::vector<PlaylistEntry> entries;
};

Result<PlaylistFile> read_playlist_file(const std::filesystem::path &file);
// The same from its text (UTF-8); `folder` resolves relative paths.
PlaylistFile parse_playlist_text(std::string_view text, const std::filesystem::path &folder,
                                 bool m3u);

// Bytes to UTF-8: a UTF-16 byte-order mark (FF FE or FE FF) decodes, a UTF-8 one is dropped.
std::string text_to_utf8(std::string_view bytes);

} // namespace psyight
