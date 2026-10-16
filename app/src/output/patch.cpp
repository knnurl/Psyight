#include "output/patch.h"

#include <algorithm>
#include <array>

namespace psyight {

namespace {

constexpr int kChannelsPerPixel = 3;

// Where R, G and B go within a pixel's three channels.
std::array<int, 3> positions(ColourOrder order) {
    switch (order) {
    case ColourOrder::rgb:
        return {0, 1, 2};
    case ColourOrder::grb:
        return {1, 0, 2};
    case ColourOrder::brg:
        return {1, 2, 0};
    case ColourOrder::rbg:
        return {0, 2, 1};
    case ColourOrder::gbr:
        return {2, 0, 1};
    case ColourOrder::bgr:
        return {2, 1, 0};
    }
    return {0, 1, 2};
}

} // namespace

Patch build_patch(const Rig &rig, int first_universe, int pixels_per_universe,
                  const std::vector<ColourOrder> &strip_orders) {
    Patch patch;
    pixels_per_universe = std::clamp(pixels_per_universe, 1, 512 / kChannelsPerPixel);
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        const auto slot = static_cast<int>(i) / pixels_per_universe;
        if (slot >= static_cast<int>(patch.universes.size())) {
            patch.universes.push_back(first_universe + slot);
        }
        PatchedPixel p;
        p.universe_slot = slot;
        p.channel = (static_cast<int>(i) % pixels_per_universe) * kChannelsPerPixel;
        const auto strip = static_cast<size_t>(rig.pixels[i].strip);
        p.order = strip < strip_orders.size() ? strip_orders[strip] : ColourOrder::grb;
        patch.pixels.push_back(p);
    }
    return patch;
}

std::vector<UniverseData> patch_frame(const Patch &patch, std::span<const std::uint8_t> rgb_bytes) {
    std::vector<UniverseData> out;
    for (const int universe : patch.universes) {
        out.push_back({universe, {}});
    }
    for (size_t i = 0; i < patch.pixels.size() && 3 * i + 2 < rgb_bytes.size(); ++i) {
        const PatchedPixel &p = patch.pixels[i];
        std::vector<std::uint8_t> &channels = out[static_cast<size_t>(p.universe_slot)].channels;
        if (channels.size() < static_cast<size_t>(p.channel + kChannelsPerPixel)) {
            channels.resize(static_cast<size_t>(p.channel + kChannelsPerPixel), 0);
        }
        const std::array<int, 3> at = positions(p.order);
        for (int c = 0; c < 3; ++c) {
            channels[static_cast<size_t>(p.channel + at[c])] = rgb_bytes[3 * i + c];
        }
    }
    return out;
}

std::vector<std::uint8_t> unpatch_frame(const Patch &patch,
                                        std::span<const std::vector<std::uint8_t>> universes) {
    std::vector<std::uint8_t> rgb(3 * patch.pixels.size(), 0);
    for (size_t i = 0; i < patch.pixels.size(); ++i) {
        const PatchedPixel &p = patch.pixels[i];
        const auto slot = static_cast<size_t>(p.universe_slot);
        if (slot >= universes.size() ||
            universes[slot].size() < static_cast<size_t>(p.channel + kChannelsPerPixel)) {
            continue;
        }
        const std::array<int, 3> at = positions(p.order);
        for (int c = 0; c < 3; ++c) {
            rgb[3 * i + c] = universes[slot][static_cast<size_t>(p.channel + at[c])];
        }
    }
    return rgb;
}

} // namespace psyight
