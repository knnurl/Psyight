#include "gui/spectrogram_textures.h"

#include <imgui.h>
#include <implot.h>

#include <GLFW/glfw3.h> // System OpenGL 1.1 header: texture objects are core since 1.1.

#include <algorithm>
#include <cstdint>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F // OpenGL 1.2; absent from the Windows 1.1 header.
#endif

namespace psyight {

namespace {

constexpr int kMaxTileFrames = 4096;

} // namespace

ColourTable implot_colour_table(int colormap) {
    ColourTable table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = ImGui::ColorConvertFloat4ToU32(
            ImPlot::SampleColormap(static_cast<float>(i) / 255.0f, colormap));
    }
    return table;
}

SpectrogramTextures::~SpectrogramTextures() { clear(); }

void SpectrogramTextures::clear() {
    for (const Tile &tile : tiles_) {
        glDeleteTextures(1, &tile.texture);
    }
    tiles_.clear();
    source_ = nullptr;
}

void SpectrogramTextures::build(const LogSpectrogram &spectrogram, int hop_samples,
                                int sample_rate_hz, float min_db, float max_db,
                                const ColourTable &colours) {
    clear();
    GLint max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    const int tile_frames = std::min(kMaxTileFrames, static_cast<int>(max_texture_size));
    rows_ = spectrogram.rows;
    frame_s_ = static_cast<double>(hop_samples) / sample_rate_hz;
    for (int first = 0; first < spectrogram.frames; first += tile_frames) {
        const int count = std::min(tile_frames, spectrogram.frames - first);
        const std::vector<std::uint32_t> pixels =
            spectrogram_tile_rgba(spectrogram, first, count, min_db, max_db, colours);
        Tile tile{0, first, count};
        glGenTextures(1, &tile.texture);
        glBindTexture(GL_TEXTURE_2D, tile.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, count, rows_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels.data());
        tiles_.push_back(tile);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    source_ = &spectrogram;
}

void SpectrogramTextures::draw(double from_s, double to_s, double y0) const {
    for (const Tile &tile : tiles_) {
        // Frame k is centred on k * hop, so a tile spans half a frame either side of its centres.
        const double start_s = (tile.first_frame - 0.5) * frame_s_;
        const double end_s = (tile.first_frame + tile.frame_count - 0.5) * frame_s_;
        if (end_s < from_s || start_s > to_s) {
            continue;
        }
        // Texel row 0 holds the lowest frequency. ImPlot draws uv0 at the top-left corner of the
        // bounds, so v is flipped to put row 0 at the bottom (low frequencies low).
        ImPlot::PlotImage("##spectrogram", static_cast<ImTextureID>(tile.texture),
                          ImPlotPoint(start_s, y0), ImPlotPoint(end_s, y0 + rows_),
                          ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
}

} // namespace psyight
