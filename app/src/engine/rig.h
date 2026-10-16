// Pixel model (SPEC section 7.1): LED strips laid along polylines, each pixel with a position in
// metres, its strip and its index along the strip. Effects work on positions, not strip indices,
// so a layout change needs no effect change. The patch (pixels to universes) is separate (M3,
// output).
#pragma once

#include <string>
#include <vector>

namespace psyight {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct StripSpec {
    std::string name;
    std::vector<Vec3> polyline; // At least two points, in metres.
    double pixels_per_m = 60.0;
    int count = 0;                   // Pixels; they start half a pitch in from the first point.
    std::vector<std::string> groups; // Tags for masks and routing, for example "left".
};

struct Pixel {
    Vec3 position;
    int strip = 0;
    int index = 0; // Along its strip, from the polyline's first point.
};

struct Rig {
    std::vector<StripSpec> strips;
    std::vector<Pixel> pixels; // Strip by strip.
    Vec3 min;                  // Bounding box of the pixels.
    Vec3 max;
};

// Places every strip's pixels along its polyline. A strip longer than its polyline continues
// along the last segment's direction.
Rig build_rig(std::vector<StripSpec> strips);

// A home-test rig: a 2.5 m wide, 1.2 m high frame of four strips at 60 pixels per metre (150
// along the bottom and top, 72 up each side: 444 pixels, under the SPEC's 1,500).
Rig default_rig();

} // namespace psyight
