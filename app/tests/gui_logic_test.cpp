// Tests for the GUI's pure logic: grid lines and spectrogram image tiles.

#include "core/timebase.h"
#include "gui/spectrogram_image.h"

#include <doctest/doctest.h>

#include <vector>

using namespace psyight;

TEST_CASE("grid lines: one bar at 160 BPM gives bar, beat and slot levels in order") {
    const Grid grid{160.0, 24000, 48000};
    const std::vector<GridLine> lines = grid.lines(24000, 24000 + 16 * 4500, 1000);
    REQUIRE(lines.size() == 17); // Slots 0 to 16 inclusive.
    CHECK(lines[0].sample == 24000);
    CHECK(lines[0].level == GridLevel::bar);
    CHECK(lines[1].level == GridLevel::slot);
    CHECK(lines[4].level == GridLevel::beat);
    CHECK(lines[4].sample == 24000 + 4 * 4500);
    CHECK(lines[16].level == GridLevel::bar);
}

TEST_CASE("grid lines: before the first downbeat the bar pattern continues backwards") {
    const Grid grid{160.0, 24000, 48000};
    const std::vector<GridLine> lines = grid.lines(0, 24000, 1000);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.back().sample == 24000);
    CHECK(lines.back().level == GridLevel::bar);
    // 24000 samples is 5.33 slots: the slot boundary at -4 slots is a beat.
    CHECK(lines[lines.size() - 5].sample == 24000 - 4 * 4500);
    CHECK(lines[lines.size() - 5].level == GridLevel::beat);
}

TEST_CASE("grid lines: finer levels are dropped when zoomed out, then everything") {
    const Grid grid{160.0, 0, 48000};
    const SampleIndex minute = 60 * 48000; // 640 slots, 160 beats, 40 bars.
    CHECK(grid.lines(0, minute, 1000).size() == 641);
    const std::vector<GridLine> beats = grid.lines(0, minute, 200);
    CHECK(beats.size() == 161);
    CHECK(beats[1].level == GridLevel::beat);
    const std::vector<GridLine> bars = grid.lines(0, minute, 50);
    CHECK(bars.size() == 41);
    CHECK(bars[1].level == GridLevel::bar);
    CHECK(grid.lines(0, minute, 10).empty());
}

TEST_CASE("spectrogram tile: rows map bottom-up and dB clamps to the colour table ends") {
    LogSpectrogram spectrogram;
    spectrogram.rows = 3;
    spectrogram.frames = 4;
    // Frame-major: frame 0 rows 0..2, frame 1 rows 0..2, ...
    spectrogram.db = {-100.0f, -50.0f, 0.0f, -90.0f, -50.0f, -10.0f,
                      -70.0f,  -30.0f, 5.0f, -90.0f, -90.0f, -90.0f};
    ColourTable colours{};
    for (size_t i = 0; i < colours.size(); ++i) {
        colours[i] = static_cast<std::uint32_t>(i);
    }
    const std::vector<std::uint32_t> pixels =
        spectrogram_tile_rgba(spectrogram, 1, 2, -90.0f, -10.0f, colours);
    REQUIRE(pixels.size() == 6);     // 3 rows x 2 frames, row-major.
    CHECK(pixels[0 * 2 + 0] == 0);   // Frame 1, row 0: -90 dB, the bottom of the range.
    CHECK(pixels[1 * 2 + 0] == 128); // Frame 1, row 1: -50 dB, the middle (127.5 rounds up).
    CHECK(pixels[2 * 2 + 0] == 255); // Frame 1, row 2: -10 dB, the top.
    CHECK(pixels[2 * 2 + 1] == 255); // Frame 2, row 2: +5 dB, clamped.
    CHECK(pixels[0 * 2 + 1] == 64);  // Frame 2, row 0: -70 dB, a quarter (63.75).

    // Frames past the end are filled with the lowest colour.
    const std::vector<std::uint32_t> tail =
        spectrogram_tile_rgba(spectrogram, 3, 3, -90.0f, -10.0f, colours);
    CHECK(tail[1] == 0);
    CHECK(tail[2] == 0);
}
