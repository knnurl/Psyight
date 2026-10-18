// Minimal PNG writer for UI captures (docs/RESTYLE.md, Phase 0): 8-bit RGBA, no filtering, zlib
// "stored" (uncompressed) blocks, so it needs no compression library. Files are about 4 bytes a
// pixel (8 MB at 1920x1080); captures are for comparison, not storage.
#pragma once

#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace psyight {

// rgba: width * height * 4 bytes, rows top to bottom.
std::vector<std::uint8_t> encode_png_rgba(int width, int height,
                                          std::span<const std::uint8_t> rgba);
Result<void> write_png_rgba(const std::filesystem::path &path, int width, int height,
                            std::span<const std::uint8_t> rgba);

} // namespace psyight
