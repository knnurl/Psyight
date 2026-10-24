#include "engine/strip_edit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace psyight {

namespace {

double length_m(const std::vector<Vec3> &p) {
    double length = 0.0;
    for (size_t i = 1; i < p.size(); ++i) {
        length += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    }
    return length;
}

int fill_of(const StripSpec &s) {
    return std::max(0, static_cast<int>(std::floor(length_m(s.polyline) * s.pixels_per_m + 1e-9)));
}

} // namespace

PolylineHit nearest_on_polyline(const std::vector<Vec3> &polyline, const Vec3 &p) {
    PolylineHit best;
    best.distance_m = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i + 1 < polyline.size(); ++i) {
        const Vec3 &a = polyline[i];
        const Vec3 &b = polyline[i + 1];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        const double t =
            len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
        const Vec3 q{a.x + t * dx, a.y + t * dy, a.z + t * (b.z - a.z)};
        const double d = std::hypot(q.x - p.x, q.y - p.y);
        if (d < best.distance_m) {
            best = {i, q, d};
        }
    }
    return best;
}

bool is_closed(const StripSpec &strip) {
    const std::vector<Vec3> &p = strip.polyline;
    return p.size() > 2 && std::hypot(p.front().x - p.back().x, p.front().y - p.back().y) < 1e-6;
}

int refilled_count(const StripSpec &before, const StripSpec &after) {
    return std::abs(before.count - fill_of(before)) <= 1 ? fill_of(after) : before.count;
}

StripSpec with_point_moved(StripSpec strip, size_t index, const Vec3 &to) {
    const StripSpec before = strip;
    if (index >= strip.polyline.size()) {
        return strip;
    }
    const bool ring = is_closed(strip);
    strip.polyline[index] = to;
    if (ring && index == 0) {
        strip.polyline.back() = to;
    } else if (ring && index + 1 == strip.polyline.size()) {
        strip.polyline.front() = to;
    }
    strip.count = refilled_count(before, strip);
    return strip;
}

StripSpec with_point_inserted(StripSpec strip, size_t segment, const Vec3 &at) {
    if (segment + 1 >= strip.polyline.size()) {
        return strip;
    }
    strip.polyline.insert(strip.polyline.begin() + static_cast<std::ptrdiff_t>(segment + 1), at);
    return strip; // The path is the same: the count stands.
}

StripSpec with_point_removed(StripSpec strip, size_t index) {
    const StripSpec before = strip;
    const bool ring = is_closed(strip);
    const size_t minimum = ring ? 4 : 2; // A ring: three corners and the closing point.
    if (index >= strip.polyline.size() || strip.polyline.size() <= minimum) {
        return strip;
    }
    if (ring && (index == 0 || index + 1 == strip.polyline.size())) {
        strip.polyline.erase(strip.polyline.begin()); // The start moves to the next corner.
        strip.polyline.back() = strip.polyline.front();
    } else {
        strip.polyline.erase(strip.polyline.begin() + static_cast<std::ptrdiff_t>(index));
    }
    strip.count = refilled_count(before, strip);
    return strip;
}

std::vector<StripSpec> split_strip(const StripSpec &strip, size_t segment, const Vec3 &at) {
    const std::vector<Vec3> &p = strip.polyline;
    if (segment + 1 >= p.size()) {
        return {strip};
    }
    if (is_closed(strip)) {
        // The ring restarted at the cut: from it round back to it.
        StripSpec opened = strip;
        opened.polyline = {at};
        for (size_t i = segment + 1; i + 1 < p.size(); ++i) {
            opened.polyline.push_back(p[i]);
        }
        for (size_t i = 0; i <= segment; ++i) {
            opened.polyline.push_back(p[i]);
        }
        opened.polyline.push_back(at);
        return {opened};
    }
    StripSpec a = strip;
    StripSpec b = strip;
    a.name = strip.name + " a";
    b.name = strip.name + " b";
    a.polyline.assign(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(segment + 1));
    a.polyline.push_back(at);
    b.polyline = {at};
    b.polyline.insert(b.polyline.end(), p.begin() + static_cast<std::ptrdiff_t>(segment + 1),
                      p.end());
    const double total = length_m(p);
    const double share = total > 0.0 ? length_m(a.polyline) / total : 0.5;
    a.count = static_cast<int>(std::lround(strip.count * share));
    b.count = strip.count - a.count;
    return {a, b};
}

StripSpec moved(StripSpec strip, double dx_m, double dy_m) {
    for (Vec3 &v : strip.polyline) {
        v.x += dx_m;
        v.y += dy_m;
    }
    return strip;
}

StripSpec rotated(StripSpec strip, const Vec3 &centre, double angle_rad) {
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    for (Vec3 &v : strip.polyline) {
        const double x = v.x - centre.x;
        const double y = v.y - centre.y;
        v.x = centre.x + c * x - s * y;
        v.y = centre.y + s * x + c * y;
    }
    return strip;
}

Vec3 snapped(const Vec3 &p, double step_m) {
    if (step_m <= 0.0) {
        return p;
    }
    return {std::round(p.x / step_m) * step_m, std::round(p.y / step_m) * step_m, p.z};
}

} // namespace psyight
