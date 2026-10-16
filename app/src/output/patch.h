// Patch (SPEC sections 7.1 and 8): rig pixels to DMX universes and channels, separate from the
// geometry. Pixels fill universes in rig order, pixels_per_universe each (170 RGB pixels use 510
// of a universe's 512 channels), never split across universes, starting at first_universe.
// Each strip has a colour order (WS2812-class strips usually want GRB); bytes leave in that order.
#pragma once

#include "engine/rig.h"

#include <cstdint>
#include <span>
#include <vector>

namespace psyight {

enum class ColourOrder { rgb, grb, brg, rbg, gbr, bgr };

struct PatchedPixel {
    int universe_slot = 0; // Index into Patch::universes.
    int channel = 0;       // First of three, 0-based within the universe.
    ColourOrder order = ColourOrder::grb;
};

struct Patch {
    std::vector<int> universes;       // sACN universe numbers (1 to 63999), ascending.
    std::vector<PatchedPixel> pixels; // One per rig pixel.
};

struct UniverseData {
    int universe = 0;
    std::vector<std::uint8_t> channels; // Up to 512.
};

// strip_orders: one colour order per strip; strips beyond it use GRB.
Patch build_patch(const Rig &rig, int first_universe = 1, int pixels_per_universe = 170,
                  const std::vector<ColourOrder> &strip_orders = {});

// rgb_bytes: three bytes per rig pixel in R, G, B order (engine/limiters.h to_bytes).
std::vector<UniverseData> patch_frame(const Patch &patch, std::span<const std::uint8_t> rgb_bytes);

// The inverse, as a node sees it: RGB bytes per rig pixel from received universes (indexed like
// Patch::universes). Pixels whose universe is missing or short stay black.
std::vector<std::uint8_t> unpatch_frame(const Patch &patch,
                                        std::span<const std::vector<std::uint8_t>> universes);

} // namespace psyight
