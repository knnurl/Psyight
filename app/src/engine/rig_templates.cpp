#include "engine/rig_templates.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

int pixels_for(double length_m, double pixels_per_m) {
    return std::max(0, static_cast<int>(std::floor(length_m * pixels_per_m + 1e-9)));
}

} // namespace

std::vector<StripSpec> pillars_strips(const PillarsTemplate &t) {
    const int count = std::clamp(t.count, 1, kMaxTemplateStrips);
    const double height_m = std::max(0.05, t.height_m);
    std::vector<StripSpec> strips;
    for (int i = 0; i < count; ++i) {
        double x = 0.0;
        double z = 0.0;
        if (t.circle) {
            const double angle = 2.0 * kPi * i / count;
            x = t.radius_m * std::sin(angle);
            z = -t.radius_m * std::cos(angle);
        } else {
            x = (i - 0.5 * (count - 1)) * t.spacing_m;
        }
        StripSpec s;
        s.name = "Pillar " + std::to_string(i + 1);
        Vec3 bottom{x, t.base_m, z};
        Vec3 top{x, t.base_m + height_m, z};
        s.polyline = t.serpentine && i % 2 == 1 ? std::vector<Vec3>{top, bottom}
                                                : std::vector<Vec3>{bottom, top};
        s.pixels_per_m = t.pixels_per_m;
        s.count = pixels_for(height_m, t.pixels_per_m);
        s.groups = {"pillars"};
        if (x <= 1e-9) {
            s.groups.emplace_back("left");
        }
        if (x >= -1e-9) {
            s.groups.emplace_back("right");
        }
        s.groups.emplace_back(i % 2 == 0 ? "even" : "odd");
        strips.push_back(std::move(s));
    }
    return strips;
}

std::vector<StripSpec> matrix_strips(const MatrixTemplate &t) {
    const int rows = std::clamp(t.rows, 1, kMaxTemplateStrips);
    const double width_m = std::max(0.05, t.width_m);
    std::vector<StripSpec> strips;
    for (int r = 0; r < rows; ++r) {
        const double y = t.base_m + r * t.row_spacing_m;
        StripSpec s;
        s.name = "Row " + std::to_string(r + 1);
        const Vec3 left{-0.5 * width_m, y, 0.0};
        const Vec3 right{0.5 * width_m, y, 0.0};
        s.polyline = t.serpentine && r % 2 == 1 ? std::vector<Vec3>{right, left}
                                                : std::vector<Vec3>{left, right};
        s.pixels_per_m = t.pixels_per_m;
        s.count = pixels_for(width_m, t.pixels_per_m);
        s.groups = {"matrix", 2 * r < rows ? "bottom" : "top", r % 2 == 0 ? "even" : "odd"};
        strips.push_back(std::move(s));
    }
    return strips;
}

} // namespace psyight
