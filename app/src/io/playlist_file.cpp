#include "io/playlist_file.h"

#include "core/paths.h"
#include "io/rekordbox.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>

namespace psyight {

namespace {

void append_utf8(std::string &out, std::uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t from = 0;
    while (from < s.size() && (s[from] == ' ' || s[from] == '\t')) {
        ++from;
    }
    return s.substr(from);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split(const std::string &line, char by) {
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, by)) {
        out.push_back(trim(field));
    }
    if (!line.empty() && line.back() == by) {
        out.emplace_back();
    }
    return out;
}

std::filesystem::path resolve(const std::string &where, const std::filesystem::path &folder) {
    if (where.rfind("file:", 0) == 0) {
        return rekordbox_location(where);
    }
    std::filesystem::path p = utf8_to_path(where);
    if (p.is_relative()) {
        p = folder / p;
    }
    return p.lexically_normal();
}

} // namespace

std::string text_to_utf8(std::string_view b) {
    const auto byte = [&](size_t i) { return static_cast<unsigned char>(b[i]); };
    if (b.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF) {
        return std::string(b.substr(3));
    }
    if (b.size() < 2 ||
        !((byte(0) == 0xFF && byte(1) == 0xFE) || (byte(0) == 0xFE && byte(1) == 0xFF))) {
        return std::string(b);
    }
    const bool big = byte(0) == 0xFE;
    std::string out;
    for (size_t i = 2; i + 1 < b.size(); i += 2) {
        std::uint32_t c = big ? (byte(i) << 8 | byte(i + 1)) : (byte(i + 1) << 8 | byte(i));
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < b.size()) {
            const std::uint32_t low =
                big ? (byte(i + 2) << 8 | byte(i + 3)) : (byte(i + 3) << 8 | byte(i + 2));
            c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
            i += 2;
        }
        append_utf8(out, c);
    }
    return out;
}

PlaylistFile parse_playlist_text(std::string_view text, const std::filesystem::path &folder,
                                 bool m3u) {
    PlaylistFile playlist;
    std::vector<std::string> lines;
    {
        std::istringstream in{std::string(text)};
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(trim(line));
        }
    }
    if (m3u || (!lines.empty() && lines.front().rfind("#EXTM3U", 0) == 0)) {
        PlaylistEntry pending;
        for (const std::string &line : lines) {
            if (line.empty()) {
                continue;
            }
            if (line.rfind("#EXTINF:", 0) == 0) { // "#EXTINF:seconds,Artist - Title"
                const size_t comma = line.find(',');
                const std::string name = comma == std::string::npos ? "" : line.substr(comma + 1);
                const size_t dash = name.find(" - ");
                pending.artist = dash == std::string::npos ? "" : trim(name.substr(0, dash));
                pending.title = trim(dash == std::string::npos ? name : name.substr(dash + 3));
                continue;
            }
            if (line[0] == '#') {
                continue;
            }
            pending.path = resolve(line, folder);
            playlist.entries.push_back(pending);
            pending = {};
        }
        return playlist;
    }
    // rekordbox's text export: a header row, then a tab-separated row a track.
    if (lines.empty()) {
        return playlist;
    }
    const std::vector<std::string> header = split(lines.front(), '\t');
    int title = -1;
    int artist = -1;
    int location = -1;
    for (size_t c = 0; c < header.size(); ++c) {
        const std::string h = lower(header[c]);
        if (h == "track title" || h == "title" || h == "name") {
            title = static_cast<int>(c);
        } else if (h == "artist") {
            artist = static_cast<int>(c);
        } else if (h == "location" || h == "file name" || h == "filename" || h == "path") {
            location = static_cast<int>(c);
        }
    }
    for (size_t r = 1; r < lines.size(); ++r) {
        if (lines[r].empty()) {
            continue;
        }
        const std::vector<std::string> row = split(lines[r], '\t');
        const auto field = [&](int c) {
            return c >= 0 && static_cast<size_t>(c) < row.size() ? row[static_cast<size_t>(c)]
                                                                 : std::string();
        };
        PlaylistEntry e;
        e.title = field(title);
        e.artist = field(artist);
        if (const std::string where = field(location); !where.empty()) {
            e.path = resolve(where, folder);
        }
        if (!e.title.empty() || !e.path.empty()) {
            playlist.entries.push_back(std::move(e));
        }
    }
    return playlist;
}

Result<PlaylistFile> read_playlist_file(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return Error{"cannot read " + path_to_utf8(file)};
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string ext = lower(file.extension().string());
    PlaylistFile playlist = parse_playlist_text(text_to_utf8(bytes), file.parent_path(),
                                                ext == ".m3u" || ext == ".m3u8");
    playlist.name = path_to_utf8(file.stem());
    if (playlist.entries.empty()) {
        return Error{"no tracks in " + path_to_utf8(file.filename())};
    }
    return playlist;
}

} // namespace psyight
