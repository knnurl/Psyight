// rekordbox playlists for the media player's library (SPEC 9.5), from the collection exported
// as XML (rekordbox: File > Export Collection in xml format). The collection's TRACK elements
// give each track's file (Location, a file:// URL) and tags (Name, Artist, Album, AverageBpm,
// TotalTime, Tonality); PLAYLISTS holds folders (NODE Type="0") and playlists (NODE Type="1")
// whose TRACK Key is a TrackID (KeyType="0") or a Location (KeyType="1"). Our own small XML
// reader: elements and attributes, entities, comments and declarations skipped.
#pragma once

#include "core/result.h"
#include "io/audio_tags.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

struct RekordboxTrack {
    std::filesystem::path path;
    TrackTags tags;
};

struct RekordboxPlaylist {
    std::string name;
    std::string folder; // Its folders in the tree, '/' separated; empty at the top.
    std::vector<std::filesystem::path> tracks; // In playlist order.
};

struct RekordboxLibrary {
    std::vector<RekordboxTrack> tracks; // The collection.
    std::vector<RekordboxPlaylist> playlists;
};

Result<RekordboxLibrary> read_rekordbox_xml(const std::filesystem::path &file);
Result<RekordboxLibrary> parse_rekordbox_xml(std::string_view text);

// "file://localhost/C:/Music/a%20b.mp3" -> C:/Music/a b.mp3 (percent-decoded UTF-8).
std::filesystem::path rekordbox_location(std::string_view url);

} // namespace psyight
