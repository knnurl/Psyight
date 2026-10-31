#include "io/rekordbox.h"

#include "core/paths.h"
#include "io/text_file.h"

#include <cstdlib>
#include <map>

namespace psyight {

namespace {

struct Element {
    std::string name;
    std::map<std::string, std::string> attributes;
    bool closing = false;      // </name>
    bool self_closing = false; // <name ... />
};

void append_utf8(std::string &out, unsigned long c) {
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

std::string entities(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const size_t semi = s.find(';', i);
        if (semi == std::string_view::npos) {
            out += s[i];
            continue;
        }
        const std::string_view e = s.substr(i + 1, semi - i - 1);
        if (e == "amp") {
            out += '&';
        } else if (e == "lt") {
            out += '<';
        } else if (e == "gt") {
            out += '>';
        } else if (e == "quot") {
            out += '"';
        } else if (e == "apos") {
            out += '\'';
        } else if (!e.empty() && e[0] == '#') {
            const std::string digits(e.substr(1));
            const unsigned long c = digits.size() > 1 && (digits[0] == 'x' || digits[0] == 'X')
                                        ? std::strtoul(digits.c_str() + 1, nullptr, 16)
                                        : std::strtoul(digits.c_str(), nullptr, 10);
            append_utf8(out, c);
        } else {
            out.append(s.substr(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

// The elements in order (text between them ignored).
std::vector<Element> elements(std::string_view text) {
    std::vector<Element> out;
    size_t at = 0;
    while ((at = text.find('<', at)) != std::string_view::npos) {
        if (text.substr(at, 4) == "<!--") {
            const size_t end = text.find("-->", at);
            at = end == std::string_view::npos ? text.size() : end + 3;
            continue;
        }
        if (at + 1 < text.size() && (text[at + 1] == '?' || text[at + 1] == '!')) {
            const size_t end = text.find('>', at);
            at = end == std::string_view::npos ? text.size() : end + 1;
            continue;
        }
        Element e;
        size_t i = at + 1;
        if (i < text.size() && text[i] == '/') {
            e.closing = true;
            ++i;
        }
        const auto space = [&](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        const size_t name_from = i;
        while (i < text.size() && !space(text[i]) && text[i] != '>' && text[i] != '/') {
            ++i;
        }
        e.name = std::string(text.substr(name_from, i - name_from));
        while (i < text.size() && text[i] != '>') {
            if (space(text[i])) {
                ++i;
                continue;
            }
            if (text[i] == '/') {
                e.self_closing = true;
                ++i;
                continue;
            }
            const size_t key_from = i;
            while (i < text.size() && text[i] != '=' && !space(text[i]) && text[i] != '>') {
                ++i;
            }
            const std::string key(text.substr(key_from, i - key_from));
            while (i < text.size() && (space(text[i]) || text[i] == '=')) {
                ++i;
            }
            if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
                const char quote = text[i];
                const size_t end = text.find(quote, i + 1);
                if (end == std::string_view::npos) {
                    return out;
                }
                e.attributes[key] = entities(text.substr(i + 1, end - i - 1));
                i = end + 1;
            }
        }
        out.push_back(std::move(e));
        at = i;
    }
    return out;
}

std::string attribute(const Element &e, const char *key) {
    const auto it = e.attributes.find(key);
    return it == e.attributes.end() ? std::string() : it->second;
}

double number(const std::string &s) { return s.empty() ? 0.0 : std::strtod(s.c_str(), nullptr); }

} // namespace

std::filesystem::path rekordbox_location(std::string_view url) {
    for (const std::string_view prefix : {"file://localhost/", "file:///", "file://"}) {
        if (url.substr(0, prefix.size()) == prefix) {
            url.remove_prefix(prefix.size());
            break;
        }
    }
    std::string decoded;
    for (size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '%' && i + 2 < url.size()) {
            decoded += static_cast<char>(
                std::strtol(std::string(url.substr(i + 1, 2)).c_str(), nullptr, 16));
            i += 2;
        } else {
            decoded += url[i];
        }
    }
    // A path with no drive letter is absolute on its own (macOS, "/Users/...").
    if (decoded.size() < 2 || decoded[1] != ':') {
        decoded.insert(decoded.begin(), '/');
    }
    return utf8_to_path(decoded).lexically_normal();
}

Result<RekordboxLibrary> parse_rekordbox_xml(std::string_view text) {
    const std::vector<Element> all = elements(text);
    bool seen_root = false;
    for (const Element &e : all) {
        seen_root |= e.name == "DJ_PLAYLISTS";
    }
    if (!seen_root) {
        return Error{"not a rekordbox collection (no DJ_PLAYLISTS)"};
    }
    RekordboxLibrary library;
    std::map<std::string, size_t> by_id; // TrackID -> collection index.
    bool in_collection = false;
    bool in_playlists = false;
    std::vector<std::string> folders;   // Open folder nodes' names.
    std::vector<bool> node_is_playlist; // The open NODE stack.
    RekordboxPlaylist *playlist = nullptr;
    std::string key_type = "0";
    for (const Element &e : all) {
        if (e.name == "COLLECTION") {
            in_collection = !e.closing && !e.self_closing;
        } else if (e.name == "PLAYLISTS") {
            in_playlists = !e.closing && !e.self_closing;
        } else if (e.name == "NODE" && in_playlists) {
            if (e.closing) {
                if (!node_is_playlist.empty()) {
                    if (!node_is_playlist.back() && !folders.empty()) {
                        folders.pop_back();
                    }
                    node_is_playlist.pop_back();
                }
                playlist = nullptr;
                continue;
            }
            const bool is_playlist = attribute(e, "Type") == "1";
            const std::string name = attribute(e, "Name");
            if (is_playlist) {
                RekordboxPlaylist p;
                p.name = name;
                for (size_t k = 1; k < folders.size(); ++k) { // folders[0] is "ROOT".
                    p.folder += (p.folder.empty() ? "" : "/") + folders[k];
                }
                library.playlists.push_back(std::move(p));
                playlist = e.self_closing ? nullptr : &library.playlists.back();
                key_type = attribute(e, "KeyType");
            } else if (!e.self_closing) {
                folders.push_back(name);
            }
            if (!e.self_closing) {
                node_is_playlist.push_back(is_playlist);
            }
        } else if (e.name == "TRACK" && !e.closing) {
            if (in_collection) {
                RekordboxTrack t;
                t.path = rekordbox_location(attribute(e, "Location"));
                t.tags.title = attribute(e, "Name");
                t.tags.artist = attribute(e, "Artist");
                t.tags.album = attribute(e, "Album");
                t.tags.key = attribute(e, "Tonality");
                t.tags.bpm = number(attribute(e, "AverageBpm"));
                t.tags.duration_s = number(attribute(e, "TotalTime"));
                by_id[attribute(e, "TrackID")] = library.tracks.size();
                library.tracks.push_back(std::move(t));
            } else if (playlist != nullptr) {
                const std::string key = attribute(e, "Key");
                if (key_type == "1") {
                    playlist->tracks.push_back(rekordbox_location(key));
                } else if (const auto it = by_id.find(key); it != by_id.end()) {
                    playlist->tracks.push_back(library.tracks[it->second].path);
                }
            }
        }
    }
    return library;
}

Result<RekordboxLibrary> read_rekordbox_xml(const std::filesystem::path &file) {
    Result<std::string> text = read_text_file(file);
    if (!text) {
        return text.error();
    }
    return parse_rekordbox_xml(text.value());
}

} // namespace psyight
