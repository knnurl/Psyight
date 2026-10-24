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

namespace {

double distance_m(const Vec3 &a, const Vec3 &b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}

bool closed(const StripSpec &s) {
    return s.polyline.size() > 2 && distance_m(s.polyline.front(), s.polyline.back()) < 1e-6;
}

} // namespace

std::vector<WiringStep> wiring_order(const std::vector<StripSpec> &strips, size_t first,
                                     bool allow_reverse) {
    std::vector<WiringStep> order;
    if (strips.empty()) {
        return order;
    }
    first = std::min(first, strips.size() - 1);
    std::vector<bool> used(strips.size(), false);
    order.push_back({first, false});
    used[first] = true;
    Vec3 end = strips[first].polyline.empty() ? Vec3{} : strips[first].polyline.back();
    while (order.size() < strips.size()) {
        // The current order's next strip, as it runs.
        size_t keep = strips.size();
        for (size_t i = 0; i < strips.size(); ++i) {
            if (!used[i] && !strips[i].polyline.empty()) {
                keep = i;
                break;
            }
        }
        WiringStep best;
        double best_m = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < strips.size(); ++i) {
            const std::vector<Vec3> &p = strips[i].polyline;
            if (used[i] || p.empty()) {
                continue;
            }
            const double forward = distance_m(end, p.front());
            if (forward < best_m - 1e-9) {
                best = {i, false};
                best_m = forward;
            }
            const double backward = distance_m(end, p.back());
            if (allow_reverse && !closed(strips[i]) && backward < best_m - 1e-9) {
                best = {i, true};
                best_m = backward;
            }
        }
        if (!std::isfinite(best_m)) {
            for (size_t i = 0; i < strips.size(); ++i) { // Strips without points, last.
                if (!used[i]) {
                    order.push_back({i, false});
                    used[i] = true;
                }
            }
            break;
        }
        if (keep < strips.size() &&
            distance_m(end, strips[keep].polyline.front()) - best_m < kWiringMinGainM) {
            best = {keep, false}; // Not worth changing.
        }
        used[best.strip] = true;
        order.push_back(best);
        const std::vector<Vec3> &p = strips[best.strip].polyline;
        end = best.reversed ? p.front() : p.back();
    }
    return order;
}

StripSpec reversed_strip(StripSpec strip) {
    std::reverse(strip.polyline.begin(), strip.polyline.end());
    return strip;
}

double jumper_length_m(const std::vector<StripSpec> &strips) {
    double total = 0.0;
    for (size_t i = 1; i < strips.size(); ++i) {
        if (!strips[i - 1].polyline.empty() && !strips[i].polyline.empty()) {
            total += distance_m(strips[i - 1].polyline.back(), strips[i].polyline.front());
        }
    }
    return total;
}

} // namespace psyight
