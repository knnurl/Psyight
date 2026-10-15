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

    void build(const FrontEndResult &analysis, float min_db, float max_db,
               const ColourTable &colours);
    void clear();
    bool built_for(const FrontEndResult *analysis) const { return source_ == analysis; }

    // Draws the tiles overlapping [from_s, to_s]. The y axis is in spectrogram rows: row r spans
    // [r, r + 1).
    void draw(double from_s, double to_s) const;

  private:
    struct Tile {
        unsigned int texture = 0;
        int first_frame = 0;
        int frame_count = 0;
    };
    std::vector<Tile> tiles_;
    const FrontEndResult *source_ = nullptr;
    int rows_ = 0;
    double frame_s_ = 0.0;
};

} // namespace psyight
