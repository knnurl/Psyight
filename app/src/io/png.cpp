#include "io/png.h"

#include "core/paths.h"

#include <array>
#include <fstream>
#include <string>

namespace psyight {

namespace {

std::uint32_t crc32(const std::uint8_t *data, size_t size, std::uint32_t crc = 0xFFFFFFFFu) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[n] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

void put32(std::vector<std::uint8_t> &out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t> &out, const char type[4],
           const std::vector<std::uint8_t> &data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu);
}

} // namespace

std::vector<std::uint8_t> encode_png_rgba(int width, int height,
                                          std::span<const std::uint8_t> rgba) {
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    put32(header, static_cast<std::uint32_t>(width));
    put32(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 6, 0, 0, 0}); // 8 bits, RGBA, deflate, no filter, no interlace.
    chunk(out, "IHDR", header);

    // Scanlines with filter byte 0, in zlib stored blocks of at most 65535 bytes.
    const size_t row = static_cast<size_t>(width) * 4;
    std::vector<std::uint8_t> raw;
    raw.reserve((row + 1) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto *line = rgba.data() + static_cast<size_t>(y) * row;
        raw.insert(raw.end(), line, line + row);
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1, b = 0; // Adler-32.
    for (size_t at = 0; at < raw.size() || at == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        const bool last = at + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        for (size_t i = at; i < at + n; ++i) {
            a = (a + raw[i]) % 65521u;
            b = (b + a) % 65521u;
        }
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
                 raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        if (last) {
            break;
        }
    }
    put32(z, (b << 16) | a);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

Result<void> write_png_rgba(const std::filesystem::path &path, int width, int height,
                            std::span<const std::uint8_t> rgba) {
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
        return Error{"PNG: the pixel buffer does not match the size"};
    }
    const std::vector<std::uint8_t> png = encode_png_rgba(width, height, rgba);
    std::ofstream file(path, std::ios::binary);
    if (!file.write(reinterpret_cast<const char *>(png.data()),
                    static_cast<std::streamsize>(png.size()))) {
        return Error{"cannot write " + path_to_utf8(path)};
    }
    return {};
}

} // namespace psyight
