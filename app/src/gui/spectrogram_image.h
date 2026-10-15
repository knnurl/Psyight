// Converts part of a log spectrogram into RGBA pixels for a texture (no graphics API here, so it
// is unit-tested). Row 0 of the image is the lowest frequency; dB values are clamped to
// [min_db, max_db] and mapped through a 256-entry colour table.
#pragma once

#include "analysis/front_end.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace psyight {

using ColourTable = std::array<std::uint32_t, 256>; // Packed RGBA, as ImGui's IM_COL32.

// Pixels for frames [first_frame, first_frame + frame_count), row-major: pixel[row * count + i].
std::vector<std::uint32_t> spectrogram_tile_rgba(const LogSpectrogram &spectrogram, int first_frame,
                                                 int frame_count, float min_db, float max_db,
                                                 const ColourTable &colours);

// Display range from the image's own values: the low_q and high_q quantiles of a strided sample.
// Robust to silence and to a few extreme frames. Returns {low, high} with high > low.
std::pair<float, float> display_range(const LogSpectrogram &image, double low_q, double high_q);

} // namespace psyight
