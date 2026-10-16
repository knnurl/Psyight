#include "engine/colour.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

float blend_channel(float dst, float src, BlendMode mode, float opacity) {
    float mixed = src;
    switch (mode) {
    case BlendMode::add:
        return clamp01(dst + src * opacity);
    case BlendMode::max:
        mixed = std::max(dst, src);
        break;
    case BlendMode::alpha:
        mixed = src;
        break;
    case BlendMode::multiply:
        mixed = dst * src;
        break;
    case BlendMode::screen:
        mixed = 1.0f - (1.0f - dst) * (1.0f - src);
        break;
    }
    return clamp01(dst + (mixed - dst) * opacity);
}

} // namespace

Rgb blend(Rgb dst, Rgb src, BlendMode mode, float opacity) {
    opacity = clamp01(opacity);
    return {blend_channel(dst.r, src.r, mode, opacity), blend_channel(dst.g, src.g, mode, opacity),
            blend_channel(dst.b, src.b, mode, opacity)};
}

Rgb hsv(double hue_deg, double saturation, double value) {
    const double h = std::fmod(std::fmod(hue_deg, 360.0) + 360.0, 360.0) / 60.0;
    const double c = value * saturation;
    const double x = c * (1.0 - std::abs(std::fmod(h, 2.0) - 1.0));
    const double m = value - c;
    double r = 0.0, g = 0.0, b = 0.0;
    switch (static_cast<int>(h)) {
    case 0:
        r = c, g = x;
        break;
    case 1:
        r = x, g = c;
        break;
    case 2:
        g = c, b = x;
        break;
    case 3:
        g = x, b = c;
        break;
    case 4:
        r = x, b = c;
        break;
    default:
        r = c, b = x;
        break;
    }
    return {static_cast<float>(r + m), static_cast<float>(g + m), static_cast<float>(b + m)};
}

Rgb scaled(Rgb c, double factor) {
    const auto f = static_cast<float>(factor);
    return {clamp01(c.r * f), clamp01(c.g * f), clamp01(c.b * f)};
}

Rgb Palette::at(double position) const {
    if (colours.empty()) {
        return {};
    }
    const double wrapped = position - std::floor(position);
    const double scaled_position = wrapped * static_cast<double>(colours.size());
    const auto i = static_cast<size_t>(scaled_position) % colours.size();
    const auto f = static_cast<float>(scaled_position - std::floor(scaled_position));
    const Rgb &a = colours[i];
    const Rgb &b = colours[(i + 1) % colours.size()];
    return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f};
}

std::vector<Palette> default_palettes() {
    return {
        {"UV violet", {{0.35f, 0.0f, 1.0f}, {0.8f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.6f}}},
        {"Acid", {{0.2f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.6f}, {0.6f, 0.0f, 1.0f}}},
        {"Deep sea", {{0.0f, 0.2f, 1.0f}, {0.0f, 0.8f, 1.0f}, {0.4f, 0.0f, 1.0f}}},
        {"Fire", {{1.0f, 0.1f, 0.0f}, {1.0f, 0.5f, 0.0f}, {0.8f, 0.0f, 0.3f}}},
    };
}

} // namespace psyight
