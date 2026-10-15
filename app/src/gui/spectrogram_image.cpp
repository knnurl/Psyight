#include "gui/spectrogram_image.h"

#include <algorithm>
#include <cmath>

namespace psyight {

std::vector<std::uint32_t> spectrogram_tile_rgba(const LogSpectrogram &spectrogram, int first_frame,
                                                 int frame_count, float min_db, float max_db,
                                                 const ColourTable &colours) {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(frame_count) * spectrogram.rows,
                                      colours[0]);
    const float scale = 255.0f / (max_db - min_db);
    const int end_frame = std::min(spectrogram.frames, first_frame + frame_count);
    for (int frame = first_frame; frame < end_frame; ++frame) {
        const float *column = spectrogram.db.data() + static_cast<size_t>(frame) * spectrogram.rows;
        for (int row = 0; row < spectrogram.rows; ++row) {
            const float level = std::clamp((column[row] - min_db) * scale, 0.0f, 255.0f);
            pixels[static_cast<size_t>(row) * frame_count + (frame - first_frame)] =
                colours[static_cast<size_t>(std::lround(level))];
        }
    }
    return pixels;
}

} // namespace psyight
