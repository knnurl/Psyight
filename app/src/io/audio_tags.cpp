#include "io/audio_tags.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace psyight {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr size_t kMaxTagBytes = 4 << 20;       // Larger tags (big cover art) are cut here.
constexpr size_t kFrameSearchBytes = 64 << 10; // Where the first MP3 frame must start.

std::uint32_t be32(const std::uint8_t *p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) |
           p[3];
}
std::uint32_t le32(const std::uint8_t *p) {
    return (std::uint32_t{p[3]} << 24) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[1]} << 8) |
           p[0];
}
std::uint16_t le16(const std::uint8_t *p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t synchsafe(const std::uint8_t *p) {
    return (std::uint32_t{p[0] & 0x7Fu} << 21) | (std::uint32_t{p[1] & 0x7Fu} << 14) |
           (std::uint32_t{p[2] & 0x7Fu} << 7) | (p[3] & 0x7Fu);
}

Bytes read_at(std::ifstream &in, std::uint64_t offset, size_t count) {
    Bytes out(count);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(count));
    out.resize(static_cast<size_t>(std::max<std::streamsize>(0, in.gcount())));
    return out;
}

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

bool valid_utf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80         ? 1
                         : (c >> 5) == 6  ? 2
                         : (c >> 4) == 14 ? 3
                         : (c >> 3) == 30 ? 4
                                          : 0;
        if (n == 0 || i + n > s.size()) {
            return false;
        }
        for (size_t k = 1; k < n; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += n;
    }
    return true;
}

std::string latin1(std::string_view s) {
    std::string out;
    for (const char ch : s) {
        append_utf8(out, static_cast<unsigned char>(ch));
    }
    return out;
}

// UTF-8 when it is valid UTF-8, else Latin-1 (RIFF INFO and old tags say neither).
std::string any_text(std::string_view s) {
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) {
        s.remove_suffix(1);
    }
    return valid_utf8(s) ? std::string(s) : latin1(s);
}

std::string utf16(const std::uint8_t *p, size_t n, bool big_endian) {
    std::string out;
    for (size_t i = 0; i + 1 < n; i += 2) {
        std::uint32_t c = big_endian ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
        if (c == 0) {
            break;
        }
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) { // A surrogate pair.
            const std::uint32_t low =
                big_endian ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
            c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
            i += 2;
        }
        append_utf8(out, c);
    }
    return out;
}

// An ID3v2 text frame's value (its first string).
std::string id3_text(const std::uint8_t *p, size_t n) {
    if (n == 0) {
        return {};
    }
    const std::uint8_t encoding = p[0];
    ++p;
    --n;
    switch (encoding) {
    case 1: { // UTF-16 with a byte-order mark.
        if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
            return utf16(p + 2, n - 2, true);
        }
        if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
            return utf16(p + 2, n - 2, false);
        }
        return utf16(p, n, false);
    }
    case 2:
        return utf16(p, n, true);
    case 3: {
        const auto *end = std::find(p, p + n, std::uint8_t{0});
        return std::string(reinterpret_cast<const char *>(p), static_cast<size_t>(end - p));
    }
    default: {
        const auto *end = std::find(p, p + n, std::uint8_t{0});
        return latin1(
            std::string_view(reinterpret_cast<const char *>(p), static_cast<size_t>(end - p)));
    }
    }
}

double number(const std::string &s) {
    return s.empty() ? 0.0 : std::max(0.0, std::strtod(s.c_str(), nullptr));
}

// An ID3v2 tag at data[0] ("ID3"). Returns the tag's length in bytes (0: none); TLEN goes to
// *length_ms.
size_t parse_id3(const Bytes &data, TrackTags &tags, double *length_ms) {
    if (data.size() < 10 || data[0] != 'I' || data[1] != 'D' || data[2] != '3') {
        return 0;
    }
    const int version = data[3];
    const std::uint8_t flags = data[5];
    const size_t size = synchsafe(&data[6]);
    const size_t total = 10 + size + ((flags & 0x10) != 0 ? 10 : 0);
    const size_t end = std::min(data.size(), 10 + size);
    size_t at = 10;
    if ((flags & 0x40) != 0 && at + 4 <= end) { // An extended header.
        at += version >= 4 ? synchsafe(&data[at]) : be32(&data[at]) + 4;
    }
    const size_t id_len = version <= 2 ? 3 : 4;
    const size_t header_len = version <= 2 ? 6 : 10;
    while (at + header_len <= end && data[at] != 0) {
        const std::string id(reinterpret_cast<const char *>(&data[at]), id_len);
        size_t frame = 0;
        if (version <= 2) {
            frame = (data[at + 3] << 16) | (data[at + 4] << 8) | data[at + 5];
        } else if (version >= 4) {
            frame = synchsafe(&data[at + 4]);
        } else {
            frame = be32(&data[at + 4]);
        }
        const size_t body = at + header_len;
        if (frame == 0 || body + frame > end) {
            break;
        }
        const auto text = [&] { return id3_text(&data[body], frame); };
        if (id == "TIT2" || id == "TT2") {
            tags.title = text();
        } else if (id == "TPE1" || id == "TP1") {
            tags.artist = text();
        } else if (id == "TALB" || id == "TAL") {
            tags.album = text();
        } else if (id == "TBPM" || id == "TBP") {
            tags.bpm = number(text());
        } else if (id == "TKEY" || id == "TKE") {
            tags.key = text();
        } else if ((id == "TLEN" || id == "TLE") && length_ms != nullptr) {
            *length_ms = number(text());
        }
        at = body + frame;
    }
    return total;
}

// The first MPEG layer III frame at or after `from`: the length from Xing/Info/VBRI, else from
// the bit rate over the bytes left.
double mp3_length(std::ifstream &in, std::uint64_t from, std::uint64_t file_size) {
    const Bytes d = read_at(in, from, kFrameSearchBytes);
    static constexpr std::array<int, 15> kRates1 = {0,   32,  40,  48,  56,  64,  80, 96,
                                                    112, 128, 160, 192, 224, 256, 320};
    static constexpr std::array<int, 15> kRates2 = {0,  8,  16, 24,  32,  40,  48, 56,
                                                    64, 80, 96, 112, 128, 144, 160};
    for (size_t i = 0; i + 4 <= d.size(); ++i) {
        if (d[i] != 0xFF || (d[i + 1] & 0xE0) != 0xE0) {
            continue;
        }
        const int version = (d[i + 1] >> 3) & 3; // 0: 2.5, 2: 2, 3: 1.
        const int layer = (d[i + 1] >> 1) & 3;   // 1: layer III.
        const int rate_index = d[i + 2] >> 4;
        const int sr_index = (d[i + 2] >> 2) & 3;
        if (version == 1 || layer != 1 || rate_index == 0 || rate_index == 15 || sr_index == 3) {
            continue;
        }
        static constexpr std::array<int, 3> kSampleRates = {44100, 48000, 32000};
        const int sample_rate = kSampleRates[static_cast<size_t>(sr_index)] >> (version == 3   ? 0
                                                                                : version == 2 ? 1
                                                                                               : 2);
        const int samples_per_frame = version == 3 ? 1152 : 576;
        const bool mono = (d[i + 3] >> 6) == 3;
        const size_t side = version == 3 ? (mono ? 17 : 32) : (mono ? 9 : 17);
        const size_t xing = i + 4 + side;
        if (xing + 12 <= d.size() &&
            (std::string_view(reinterpret_cast<const char *>(&d[xing]), 4) == "Xing" ||
             std::string_view(reinterpret_cast<const char *>(&d[xing]), 4) == "Info")) {
            if ((be32(&d[xing + 4]) & 1) != 0) {
                return static_cast<double>(be32(&d[xing + 8])) * samples_per_frame / sample_rate;
            }
        }
        const size_t vbri = i + 36;
        if (vbri + 18 <= d.size() &&
            std::string_view(reinterpret_cast<const char *>(&d[vbri]), 4) == "VBRI") {
            return static_cast<double>(be32(&d[vbri + 14])) * samples_per_frame / sample_rate;
        }
        const int kbps = (version == 3 ? kRates1 : kRates2)[static_cast<size_t>(rate_index)];
        const std::uint64_t audio = file_size > from + i ? file_size - (from + i) : 0;
        return static_cast<double>(audio) * 8.0 / (kbps * 1000.0);
    }
    return 0.0;
}

void read_mp3(std::ifstream &in, std::uint64_t file_size, TrackTags &tags) {
    Bytes head = read_at(in, 0, 10);
    std::uint64_t audio_from = 0;
    double length_ms = 0.0;
    if (head.size() == 10 && head[0] == 'I' && head[1] == 'D' && head[2] == '3') {
        const size_t size = std::min<size_t>(10 + synchsafe(&head[6]), kMaxTagBytes);
        audio_from = parse_id3(read_at(in, 0, size), tags, &length_ms);
    }
    tags.duration_s = length_ms > 0.0 ? length_ms / 1000.0 : mp3_length(in, audio_from, file_size);
}

void vorbis_comments(const Bytes &d, TrackTags &tags) {
    if (d.size() < 8) {
        return;
    }
    size_t at = 4 + le32(&d[0]); // The vendor string.
    if (at + 4 > d.size()) {
        return;
    }
    const std::uint32_t count = le32(&d[at]);
    at += 4;
    for (std::uint32_t k = 0; k < count && at + 4 <= d.size(); ++k) {
        const size_t len = le32(&d[at]);
        at += 4;
        if (at + len > d.size()) {
            break;
        }
        const std::string entry(reinterpret_cast<const char *>(&d[at]), len);
        at += len;
        const size_t eq = entry.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = entry.substr(0, eq);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const std::string value = entry.substr(eq + 1);
        if (key == "TITLE") {
            tags.title = value;
        } else if (key == "ARTIST") {
            tags.artist = value;
        } else if (key == "ALBUM") {
            tags.album = value;
        } else if (key == "BPM" || key == "TEMPO") {
            tags.bpm = number(value);
        } else if (key == "INITIALKEY" || key == "KEY") {
            tags.key = value;
        }
    }
}

void read_flac(std::ifstream &in, TrackTags &tags) {
    std::uint64_t at = 0;
    Bytes head = read_at(in, 0, 10);
    if (head.size() == 10 && head[0] == 'I' && head[1] == 'D' && head[2] == '3') {
        at = 10 + synchsafe(&head[6]); // A stray ID3 tag before the stream.
    }
    if (read_at(in, at, 4) != Bytes{'f', 'L', 'a', 'C'}) {
        return;
    }
    at += 4;
    for (int blocks = 0; blocks < 64; ++blocks) {
        const Bytes h = read_at(in, at, 4);
        if (h.size() < 4) {
            return;
        }
        const bool last = (h[0] & 0x80) != 0;
        const int type = h[0] & 0x7F;
        const size_t len = (h[1] << 16) | (h[2] << 8) | h[3];
        if (type == 0 && len >= 18) { // STREAMINFO.
            const Bytes s = read_at(in, at + 4, 18);
            if (s.size() == 18) {
                const std::uint32_t rate =
                    (std::uint32_t{s[10]} << 12) | (s[11] << 4) | (s[12] >> 4);
                const std::uint64_t samples = (std::uint64_t{s[13] & 0x0Fu} << 32) | be32(&s[14]);
                if (rate > 0) {
                    tags.duration_s = static_cast<double>(samples) / rate;
                }
            }
        } else if (type == 4) { // VORBIS_COMMENT.
            vorbis_comments(read_at(in, at + 4, std::min(len, kMaxTagBytes)), tags);
        }
        at += 4 + len;
        if (last) {
            return;
        }
    }
}

void read_wav(std::ifstream &in, std::uint64_t file_size, TrackTags &tags) {
    const Bytes riff = read_at(in, 0, 12);
    if (riff.size() < 12 ||
        std::string_view(reinterpret_cast<const char *>(riff.data()), 4) != "RIFF" ||
        std::string_view(reinterpret_cast<const char *>(&riff[8]), 4) != "WAVE") {
        return;
    }
    std::uint32_t block_align = 0;
    std::uint32_t sample_rate = 0;
    std::uint64_t at = 12;
    while (at + 8 <= file_size) {
        const Bytes h = read_at(in, at, 8);
        if (h.size() < 8) {
            break;
        }
        const std::string id(reinterpret_cast<const char *>(h.data()), 4);
        const std::uint64_t len = le32(&h[4]);
        const std::uint64_t body = at + 8;
        if (id == "fmt " && len >= 16) {
            const Bytes f = read_at(in, body, 16);
            if (f.size() == 16) {
                sample_rate = le32(&f[4]);
                block_align = le16(&f[12]);
            }
        } else if (id == "data") {
            if (block_align > 0 && sample_rate > 0) {
                const std::uint64_t bytes = std::min(len, file_size - body);
                tags.duration_s = static_cast<double>(bytes / block_align) / sample_rate;
            }
        } else if (id == "LIST" && len >= 4) {
            const Bytes l =
                read_at(in, body, static_cast<size_t>(std::min<std::uint64_t>(len, kMaxTagBytes)));
            if (l.size() >= 4 &&
                std::string_view(reinterpret_cast<const char *>(l.data()), 4) == "INFO") {
                for (size_t k = 4; k + 8 <= l.size();) {
                    const std::string sub(reinterpret_cast<const char *>(&l[k]), 4);
                    const size_t n = le32(&l[k + 4]);
                    if (k + 8 + n > l.size()) {
                        break;
                    }
                    const std::string_view value(reinterpret_cast<const char *>(&l[k + 8]), n);
                    if (sub == "INAM" && tags.title.empty()) {
                        tags.title = any_text(value);
                    } else if (sub == "IART" && tags.artist.empty()) {
                        tags.artist = any_text(value);
                    } else if (sub == "IPRD" && tags.album.empty()) {
                        tags.album = any_text(value);
                    }
                    k += 8 + n + (n & 1);
                }
            }
        } else if (id == "id3 " || id == "ID3 ") { // ID3v2 wins over INFO.
            TrackTags id3;
            parse_id3(
                read_at(in, body, static_cast<size_t>(std::min<std::uint64_t>(len, kMaxTagBytes))),
                id3, nullptr);
            using Field = std::pair<std::string *, const std::string *>;
            for (const auto &[to, from] :
                 {Field{&tags.title, &id3.title}, Field{&tags.artist, &id3.artist},
                  Field{&tags.album, &id3.album}, Field{&tags.key, &id3.key}}) {
                if (!from->empty()) {
                    *to = *from;
                }
            }
            if (id3.bpm > 0.0) {
                tags.bpm = id3.bpm;
            }
        }
        at = body + len + (len & 1);
    }
}

} // namespace

TrackTags read_track_tags(const std::filesystem::path &path) {
    TrackTags tags;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return tags;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return tags;
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".mp3") {
        read_mp3(in, size, tags);
    } else if (ext == ".flac") {
        read_flac(in, tags);
    } else if (ext == ".wav") {
        read_wav(in, size, tags);
    }
    return tags;
}

} // namespace psyight
