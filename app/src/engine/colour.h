// Colour for the engine (SPEC section 7.5): linear RGB in 0 to 1 per channel (limiters and gamma
// come after the scene), blend modes for layers, HSV hues, and palettes.
#pragma once

#include <string>
#include <vector>

namespace psyight {

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    bool operator==(const Rgb &) const = default;
};

using Frame = std::vector<Rgb>; // One colour per rig pixel.

enum class BlendMode { add, max, alpha, multiply, screen };

// src over dst with the given opacity (0 to 1), clamped to 0 to 1.
Rgb blend(Rgb dst, Rgb src, BlendMode mode, float opacity);

// Hue in degrees (any value, wrapped), saturation and value 0 to 1.
Rgb hsv(double hue_deg, double saturation, double value);

Rgb scaled(Rgb c, double factor);

struct Palette {
    std::string name;
    std::vector<Rgb> colours; // Cyclic: the last blends back into the first.

    // Position 0 to 1 around the palette (wrapped), linearly interpolated.
    Rgb at(double position) const;
};

// UV-friendly sets first (violet and magenta that do not wash out UV decor), then brighter ones.
// White is kept out of them: it is reserved for kicks and drops.
std::vector<Palette> default_palettes();

} // namespace psyight
