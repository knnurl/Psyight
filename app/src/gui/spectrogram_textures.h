// A whole-track spectrogram as OpenGL textures, built once per track and drawn into the current
// ImPlot plot. Tiles are at most 4096 frames wide (and never above GL_MAX_TEXTURE_SIZE), so a
// 7-minute track at a 256-sample hop needs about 20. Nothing is uploaded per frame.
// Requires a current OpenGL context on the calling (GUI) thread.
#pragma once

#include "analysis/front_end.h"
#include "gui/spectrogram_image.h"

#include <vector>

namespace psyight {

class SpectrogramTextures {
  public:
    SpectrogramTextures() = default;
    ~SpectrogramTextures();
    SpectrogramTextures(const SpectrogramTextures &) = delete;
    SpectrogramTextures &operator=(const SpectrogramTextures &) = delete;

    // Builds from any frame-major image on the analysis frame clock (spectrogram or pitch
    // salience).
    void build(const LogSpectrogram &image, int hop_samples, int sample_rate_hz, float min_db,
               float max_db, const ColourTable &colours);
    void clear();
    bool built_for(const LogSpectrogram *image) const { return source_ == image; }

    // Draws the tiles overlapping [from_s, to_s], with row r spanning [y0 + r, y0 + r + 1).
    void draw(double from_s, double to_s, double y0 = 0.0) const;

  private:
    struct Tile {
        unsigned int texture = 0;
        int first_frame = 0;
        int frame_count = 0;
    };
    std::vector<Tile> tiles_;
    const LogSpectrogram *source_ = nullptr;
    int rows_ = 0;
    double frame_s_ = 0.0;
};

} // namespace psyight
