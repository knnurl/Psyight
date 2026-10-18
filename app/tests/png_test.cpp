// The capture PNG writer (io/png.h): structure, checksums and the pixels read back from the
// stored deflate blocks.

#include "io/png.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using namespace psyight;

namespace {

std::uint32_t be32(const std::vector<std::uint8_t> &b, size_t at) {
    return (std::uint32_t{b[at]} << 24) | (std::uint32_t{b[at + 1]} << 16) |
           (std::uint32_t{b[at + 2]} << 8) | std::uint32_t{b[at + 3]};
}

std::uint32_t crc32(const std::uint8_t *p, size_t n) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
    }
    return c ^ 0xFFFFFFFFu;
}

} // namespace

TEST_CASE("png: chunks, checksums and pixels round-trip through stored blocks") {
    const int w = 3, h = 2;
    std::vector<std::uint8_t> rgba;
    for (int i = 0; i < w * h * 4; ++i) {
        rgba.push_back(static_cast<std::uint8_t>(i * 7));
    }
    const std::vector<std::uint8_t> png = encode_png_rgba(w, h, rgba);
    REQUIRE(png.size() > 8);
    CHECK(png[0] == 0x89);
    CHECK(png[1] == 'P');
    std::vector<std::uint8_t> idat;
    size_t at = 8;
    int chunks = 0;
    while (at + 12 <= png.size()) {
        const std::uint32_t length = be32(png, at);
        const std::string type(png.begin() + static_cast<long>(at) + 4,
                               png.begin() + static_cast<long>(at) + 8);
        CHECK(be32(png, at + 8 + length) == crc32(png.data() + at + 4, length + 4));
        if (type == "IHDR") {
            CHECK(be32(png, at + 8) == 3u);
            CHECK(be32(png, at + 12) == 2u);
            CHECK(png[at + 16] == 8); // Bit depth.
            CHECK(png[at + 17] == 6); // RGBA.
        } else if (type == "IDAT") {
            idat.assign(png.begin() + static_cast<long>(at) + 8,
                        png.begin() + static_cast<long>(at) + 8 + length);
        }
        at += 12 + length;
        ++chunks;
    }
    CHECK(chunks == 3);
    CHECK(at == png.size());
    // zlib header, one final stored block, scanlines with filter 0.
    REQUIRE(idat.size() == 2 + 5 + static_cast<size_t>(h) * (1 + w * 4) + 4);
    CHECK(idat[2] == 1);
    for (int y = 0; y < h; ++y) {
        const size_t line = 7 + static_cast<size_t>(y) * (1 + w * 4);
        CHECK(idat[line] == 0);
        for (int i = 0; i < w * 4; ++i) {
            CHECK(idat[line + 1 + static_cast<size_t>(i)] ==
                  rgba[static_cast<size_t>(y * w * 4 + i)]);
        }
    }
}

TEST_CASE("png: a buffer of the wrong size is refused") {
    CHECK_FALSE(write_png_rgba("never-written.png", 4, 4, std::vector<std::uint8_t>(10)).ok());
}
