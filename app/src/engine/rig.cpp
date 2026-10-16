#include "engine/rig.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace psyight {

namespace {

double distance(const Vec3 &a, const Vec3 &b) {
    return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) +
                     (b.z - a.z) * (b.z - a.z));
}

Vec3 lerp(const Vec3 &a, const Vec3 &b, double f) {
    return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
}

// The point at arc length s along the polyline, extrapolating past its end.
Vec3 along(const std::vector<Vec3> &line, double s) {
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        const double length = distance(line[i], line[i + 1]);
        if (s <= length || i + 2 == line.size()) {
            return length > 0.0 ? lerp(line[i], line[i + 1], s / length) : line[i];
        }
        s -= length;
    }
    return line.empty() ? Vec3{} : line.front();
}

} // namespace

Rig build_rig(std::vector<StripSpec> strips) {
    Rig rig;
    rig.strips = std::move(strips);
    constexpr double kBig = std::numeric_limits<double>::max();
    rig.min = {kBig, kBig, kBig};
    rig.max = {-kBig, -kBig, -kBig};
    for (size_t s = 0; s < rig.strips.size(); ++s) {
        const StripSpec &strip = rig.strips[s];
        if (strip.polyline.size() < 2 || strip.pixels_per_m <= 0.0) {
            continue;
        }
        const double pitch_m = 1.0 / strip.pixels_per_m;
        for (int i = 0; i < strip.count; ++i) {
            Pixel pixel;
            pixel.position = along(strip.polyline, (i + 0.5) * pitch_m);
            pixel.strip = static_cast<int>(s);
            pixel.index = i;
            rig.pixels.push_back(pixel);
            rig.min = {std::min(rig.min.x, pixel.position.x), std::min(rig.min.y, pixel.position.y),
                       std::min(rig.min.z, pixel.position.z)};
            rig.max = {std::max(rig.max.x, pixel.position.x), std::max(rig.max.y, pixel.position.y),
                       std::max(rig.max.z, pixel.position.z)};
        }
    }
    if (rig.pixels.empty()) {
        rig.min = rig.max = {};
    }
    return rig;
}

Rig default_rig() {
    constexpr double kWidth = 2.5;
    constexpr double kHeight = 1.2;
    constexpr int kSidePixels = 150;
    return build_rig({
        {"bottom", {{0.0, 0.0, 0.0}, {kWidth, 0.0, 0.0}}, 60.0, kSidePixels, {"frame", "bottom"}},
        {"right", {{kWidth, 0.0, 0.0}, {kWidth, kHeight, 0.0}}, 60.0, 72, {"frame", "right"}},
        {"top", {{kWidth, kHeight, 0.0}, {0.0, kHeight, 0.0}}, 60.0, kSidePixels, {"frame", "top"}},
        {"left", {{0.0, kHeight, 0.0}, {0.0, 0.0, 0.0}}, 60.0, 72, {"frame", "left"}},
    });
}

} // namespace psyight
