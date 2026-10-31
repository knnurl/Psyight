// The media player's library: tags and lengths from file headers (io/audio_tags.h), the
// scanned index (io/media_library.h) and rekordbox playlists (io/rekordbox.h), on synthetic
// files written here.

#include "core/paths.h"
#include "io/audio_tags.h"
#include "io/media_library.h"
#include "io/rekordbox.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace psyight;

namespace {

using Bytes = std::vector<std::uint8_t>;

void put(Bytes &b, std::string_view s) { b.insert(b.end(), s.begin(), s.end()); }
void be32(Bytes &b, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) {
        b.push_back(static_cast<std::uint8_t>(v >> s));
    }
}
void le32(Bytes &b, std::uint32_t v) {
    for (int s = 0; s <= 24; s += 8) {
        b.push_back(static_cast<std::uint8_t>(v >> s));
    }
}
void le16(Bytes &b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void synchsafe(Bytes &b, std::uint32_t v) {
    for (int s = 21; s >= 0; s -= 7) {
        b.push_back(static_cast<std::uint8_t>((v >> s) & 0x7F));
    }
}

void write(const std::filesystem::path &path, const Bytes &bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

// ID3v2.3 frames: a UTF-16 title (with an e acute), a Latin-1 artist, a BPM.
Bytes id3_tag() {
    Bytes frames;
    const auto frame = [&](std::string_view id, const Bytes &body) {
        put(frames, id);
        be32(frames, static_cast<std::uint32_t>(body.size()));
        frames.push_back(0);
        frames.push_back(0);
        frames.insert(frames.end(), body.begin(), body.end());
    };
    Bytes title = {1, 0xFF, 0xFE}; // UTF-16LE with its mark: "Café".
    for (const char16_t c : std::u16string(u"Caf\u00e9")) {
        le16(title, static_cast<std::uint16_t>(c));
    }
    frame("TIT2", title);
    Bytes artist = {0};
    put(artist, "Artist");
    frame("TPE1", artist);
    Bytes bpm = {0};
    put(bpm, "150");
    frame("TBPM", bpm);
    Bytes tag;
    put(tag, "ID3");
    tag.insert(tag.end(), {3, 0, 0});
    synchsafe(tag, static_cast<std::uint32_t>(frames.size()));
    tag.insert(tag.end(), frames.begin(), frames.end());
    return tag;
}

Bytes mp3_file() {
    Bytes b = id3_tag();
    // MPEG-1 layer III, 128 kbit/s, 44.1 kHz, joint stereo; its Xing frame: 1000 frames.
    const size_t header = b.size();
    b.insert(b.end(), {0xFF, 0xFB, 0x90, 0x64});
    b.resize(header + 36, 0);
    put(b, "Xing");
    be32(b, 1);
    be32(b, 1000);
    b.resize(b.size() + 400, 0);
    return b;
}

Bytes flac_file() {
    Bytes b;
    put(b, "fLaC");
    b.insert(b.end(), {0x00, 0x00, 0x00, 34}); // STREAMINFO, not the last block.
    Bytes info(34, 0);
    const std::uint32_t rate = 48000;
    const std::uint64_t samples = 480000; // 10 s.
    info[10] = static_cast<std::uint8_t>(rate >> 12);
    info[11] = static_cast<std::uint8_t>(rate >> 4);
    info[12] = static_cast<std::uint8_t>(((rate & 0xF) << 4) | (1 << 1));      // Stereo.
    info[13] = static_cast<std::uint8_t>((15 << 4) | ((samples >> 32) & 0xF)); // 16 bits.
    info[14] = static_cast<std::uint8_t>(samples >> 24);
    info[15] = static_cast<std::uint8_t>(samples >> 16);
    info[16] = static_cast<std::uint8_t>(samples >> 8);
    info[17] = static_cast<std::uint8_t>(samples);
    b.insert(b.end(), info.begin(), info.end());
    Bytes comments;
    le32(comments, 0); // Vendor.
    le32(comments, 3);
    for (const std::string_view c : {"TITLE=Flac song", "artist=Someone", "BPM=148"}) {
        le32(comments, static_cast<std::uint32_t>(c.size()));
        put(comments, c);
    }
    b.push_back(0x80 | 4); // VORBIS_COMMENT, the last block.
    b.push_back(0);
    b.push_back(static_cast<std::uint8_t>(comments.size() >> 8));
    b.push_back(static_cast<std::uint8_t>(comments.size()));
    b.insert(b.end(), comments.begin(), comments.end());
    return b;
}

Bytes wav_file(bool with_id3) {
    Bytes chunks;
    put(chunks, "fmt ");
    le32(chunks, 16);
    le16(chunks, 1);     // PCM.
    le16(chunks, 2);     // Stereo.
    le32(chunks, 48000); // Rate.
    le32(chunks, 48000 * 4);
    le16(chunks, 4); // Block align.
    le16(chunks, 16);
    Bytes info;
    put(info, "INFO");
    put(info, "INAM");
    le32(info, 10);
    put(info, std::string_view("Wav title\0", 10));
    put(info, "IART");
    le32(info, 9);
    put(info, std::string_view("W artist\0", 9));
    info.push_back(0); // Pad to even.
    put(chunks, "LIST");
    le32(chunks, static_cast<std::uint32_t>(info.size()));
    chunks.insert(chunks.end(), info.begin(), info.end());
    if (with_id3) {
        const Bytes tag = id3_tag();
        put(chunks, "id3 ");
        le32(chunks, static_cast<std::uint32_t>(tag.size()));
        chunks.insert(chunks.end(), tag.begin(), tag.end());
        if (tag.size() % 2 != 0) {
            chunks.push_back(0);
        }
    }
    put(chunks, "data");
    le32(chunks, 48000 * 4 * 2); // 2 s.
    chunks.resize(chunks.size() + 48000 * 4 * 2, 0);
    Bytes b;
    put(b, "RIFF");
    le32(b, static_cast<std::uint32_t>(4 + chunks.size()));
    put(b, "WAVE");
    b.insert(b.end(), chunks.begin(), chunks.end());
    return b;
}

std::filesystem::path scratch(const char *name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "psyight_tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("audio tags: ID3v2 and the Xing length; FLAC; WAV INFO and id3 chunks") {
    const std::filesystem::path dir = scratch("tags");
    write(dir / "a.mp3", mp3_file());
    write(dir / "b.flac", flac_file());
    write(dir / "c.wav", wav_file(false));
    write(dir / "d.wav", wav_file(true));
    const TrackTags mp3 = read_track_tags(dir / "a.mp3");
    CHECK(mp3.title == "Caf\xC3\xA9");
    CHECK(mp3.artist == "Artist");
    CHECK(mp3.bpm == 150.0);
    CHECK(mp3.duration_s == doctest::Approx(1000.0 * 1152.0 / 44100.0));
    const TrackTags flac = read_track_tags(dir / "b.flac");
    CHECK(flac.title == "Flac song");
    CHECK(flac.artist == "Someone"); // Field names are case-insensitive.
    CHECK(flac.bpm == 148.0);
    CHECK(flac.duration_s == doctest::Approx(10.0));
    const TrackTags wav = read_track_tags(dir / "c.wav");
    CHECK(wav.title == "Wav title");
    CHECK(wav.artist == "W artist");
    CHECK(wav.duration_s == doctest::Approx(2.0));
    const TrackTags tagged = read_track_tags(dir / "d.wav"); // ID3 wins over INFO.
    CHECK(tagged.title == "Caf\xC3\xA9");
    CHECK(tagged.bpm == 150.0);
    CHECK(read_track_tags(dir / "missing.mp3") == TrackTags{});
}

TEST_CASE("media library: scans folders, reuses unchanged tags, round-trips its index") {
    const std::filesystem::path root = scratch("library");
    write(root / "Album" / "a.mp3", mp3_file());
    write(root / "b.flac", flac_file());
    write(root / "notes.txt", {'x'});
    const MediaLibrary first = scan_library({root, root / "Album"}, {});
    REQUIRE(first.tracks.size() == 2); // The text file skipped; a.mp3 once.
    const auto &album =
        first.tracks[0].path.filename() == "a.mp3" ? first.tracks[0] : first.tracks[1];
    CHECK(album.folder == "Album");
    CHECK(display_title(album) == "Caf\xC3\xA9");
    // Unchanged files keep the index's tags (read again only when size or time change).
    MediaLibrary edited = first;
    for (LibraryTrack &t : edited.tracks) {
        t.tags.title = "From the index";
    }
    for (const LibraryTrack &t : scan_library({root}, edited).tracks) {
        CHECK(t.tags.title == "From the index");
    }
    const std::filesystem::path index = root / "library.json";
    REQUIRE(save_library_index(index, first).ok());
    const Result<MediaLibrary> back = load_library_index(index);
    REQUIRE(back.ok());
    CHECK(back.value().tracks == first.tracks);
}

TEST_CASE("rekordbox: the collection, folders and playlists by id or location") {
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<DJ_PLAYLISTS Version="1.0.0">
  <COLLECTION Entries="2">
    <TRACK TrackID="1" Name="One &amp; only" Artist="A" AverageBpm="150.00" TotalTime="420"
           Tonality="8A" Location="file://localhost/C:/Music/a%20b%C3%A9.mp3"/>
    <TRACK TrackID="2" Name="Two" Location="file://localhost/C:/Music/two.flac">
      <TEMPO Inizio="0.0" Bpm="148.00"/>
    </TRACK>
  </COLLECTION>
  <PLAYLISTS>
    <NODE Type="0" Name="ROOT" Count="2">
      <NODE Type="0" Name="Sets" Count="1">
        <NODE Name="Night" Type="1" KeyType="0" Entries="2">
          <TRACK Key="2"/>
          <TRACK Key="1"/>
        </NODE>
      </NODE>
      <NODE Name="By path" Type="1" KeyType="1" Entries="1">
        <TRACK Key="file://localhost/C:/Music/two.flac"/>
      </NODE>
    </NODE>
  </PLAYLISTS>
</DJ_PLAYLISTS>)";
    const Result<RekordboxLibrary> parsed = parse_rekordbox_xml(xml);
    REQUIRE(parsed.ok());
    const RekordboxLibrary &r = parsed.value();
    REQUIRE(r.tracks.size() == 2);
    CHECK(r.tracks[0].tags.title == "One & only");
    CHECK(r.tracks[0].tags.bpm == 150.0);
    CHECK(r.tracks[0].tags.duration_s == 420.0);
    CHECK(r.tracks[0].tags.key == "8A");
    CHECK(r.tracks[0].path == utf8_to_path("C:/Music/a b\xC3\xA9.mp3").lexically_normal());
    REQUIRE(r.playlists.size() == 2);
    CHECK(r.playlists[0].name == "Night");
    CHECK(r.playlists[0].folder == "Sets");
    REQUIRE(r.playlists[0].tracks.size() == 2);
    CHECK(r.playlists[0].tracks[0] == r.tracks[1].path);
    CHECK(r.playlists[1].folder.empty());
    CHECK(r.playlists[1].tracks == std::vector<std::filesystem::path>{r.tracks[1].path});
    CHECK_FALSE(parse_rekordbox_xml("<html></html>").ok());
}
