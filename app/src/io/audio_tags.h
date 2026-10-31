// Track tags and length for the media player's library (SPEC 9.5), read from the file's headers
// without decoding the audio:
// - MP3: an ID3v2 tag (2.2, 2.3, 2.4; text frames title, artist, album, BPM, key, length), and
//   the length from TLEN, else the first frame's Xing, Info or VBRI frame count, else the size
//   over the first frame's bit rate (exact for constant bit rate);
// - FLAC: STREAMINFO's sample count and rate, and the Vorbis comments (TITLE, ARTIST, ALBUM, BPM,
//   INITIALKEY or KEY);
// - WAV: the fmt and data chunks for the length, a LIST INFO chunk (INAM, IART, IPRD) and an
//   "id3 " chunk (ID3v2, as rekordbox and taggers write them).
// Missing fields stay empty (bpm and duration 0). Our own reader: tag libraries are LGPL.
#pragma once

#include <filesystem>
#include <string>

namespace psyight {

struct TrackTags {
    std::string title; // UTF-8; empty when untagged (show the file name).
    std::string artist;
    std::string album;
    std::string key; // As tagged ("8A", "Am"...).
    double bpm = 0.0;
    double duration_s = 0.0;

    bool operator==(const TrackTags &) const = default;
};

// Never throws; an unreadable or unknown file gives empty tags.
TrackTags read_track_tags(const std::filesystem::path &path);

} // namespace psyight
