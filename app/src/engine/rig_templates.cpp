#include "engine/rig_templates.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

int pixels_for(double length_m, double pixels_per_m) {
    return std::max(0, static_cast<int>(std::floor(length_m * pixels_per_m + 1e-9)));
}

double polyline_length_m(const std::vector<Vec3> &points) {
    double length_m = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        const double dx = points[i].x - points[i - 1].x;
        const double dy = points[i].y - points[i - 1].y;
        const double dz = points[i].z - points[i - 1].z;
        length_m += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return length_m;
}

// A strip filling its polyline, tagged left and/or right by its bounding box's centre.
StripSpec preset_strip(std::string name, std::vector<Vec3> polyline, double pixels_per_m,
                       std::vector<std::string> groups) {
    double min_x = polyline.front().x;
    double max_x = min_x;
    for (const Vec3 &p : polyline) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
    }
    const double centre_x = 0.5 * (min_x + max_x);
    if (centre_x <= 1e-9) {
        groups.emplace_back("left");
    }
    if (centre_x >= -1e-9) {
        groups.emplace_back("right");
    }
    StripSpec s;
    s.name = std::move(name);
    s.pixels_per_m = pixels_per_m;
    s.count = pixels_for(polyline_length_m(polyline), pixels_per_m);
    s.polyline = std::move(polyline);
    s.groups = std::move(groups);
    return s;
}

// Points along a circular arc in the x-y plane, angles in radians from +x, anticlockwise.
std::vector<Vec3> arc(Vec3 centre, double radius_m, double from_rad, double to_rad, int segments) {
    std::vector<Vec3> points;
    for (int i = 0; i <= segments; ++i) {
        const double a = from_rad + (to_rad - from_rad) * i / segments;
        points.push_back(
            {centre.x + radius_m * std::cos(a), centre.y + radius_m * std::sin(a), centre.z});
    }
    return points;
}

// A closed regular polygon in the x-y plane, starting (and ending) at its lowest vertex.
std::vector<Vec3> polygon(Vec3 centre, double radius_m, int sides) {
    return arc(centre, radius_m, -0.5 * kPi, 1.5 * kPi, sides);
}

// The point radius_m from centre at angle_rad (from +x, anticlockwise), in the x-y plane.
Vec3 polar(Vec3 centre, double radius_m, double angle_rad) {
    return {centre.x + radius_m * std::cos(angle_rad), centre.y + radius_m * std::sin(angle_rad),
            centre.z};
}

std::vector<Vec3> reversed(std::vector<Vec3> points) {
    std::reverse(points.begin(), points.end());
    return points;
}

// A circular arc from a to b in the x-y plane, bulging width_m / 2 to the left of the direction a
// to b at its middle: one side of a lens (vesica) shape.
std::vector<Vec3> lens_side(Vec3 a, Vec3 b, double width_m, int segments = 16) {
    const double chord_m = std::hypot(b.x - a.x, b.y - a.y);
    const double half_m = 0.5 * chord_m;
    const double sagitta_m = 0.5 * width_m;
    const double ux = (b.x - a.x) / chord_m;
    const double uy = (b.y - a.y) / chord_m;
    const double nx = -uy; // Left of a to b.
    const double ny = ux;
    const double radius_m = (half_m * half_m + sagitta_m * sagitta_m) / (2.0 * sagitta_m);
    const double cx = 0.5 * (a.x + b.x) - nx * (radius_m - sagitta_m);
    const double cy = 0.5 * (a.y + b.y) - ny * (radius_m - sagitta_m);
    const double half_angle = std::atan2(half_m, radius_m - sagitta_m);
    std::vector<Vec3> points;
    for (int i = 0; i <= segments; ++i) {
        const double t = -half_angle + 2.0 * half_angle * i / segments;
        points.push_back({cx + radius_m * (nx * std::cos(t) + ux * std::sin(t)),
                          cy + radius_m * (ny * std::cos(t) + uy * std::sin(t)), a.z});
    }
    return points;
}

// A closed lens from a to b and back, width_m across its middle: a petal or a slit pupil.
std::vector<Vec3> lens_loop(Vec3 a, Vec3 b, double width_m) {
    std::vector<Vec3> points = lens_side(a, b, width_m);
    const std::vector<Vec3> back = lens_side(b, a, width_m);
    points.insert(points.end(), back.begin() + 1, back.end());
    return points;
}

// A quadratic Bezier curve from p0 to p2, pulled towards p1.
std::vector<Vec3> bezier(Vec3 p0, Vec3 p1, Vec3 p2, int segments = 16) {
    std::vector<Vec3> points;
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / segments;
        const double a = (1.0 - t) * (1.0 - t);
        const double b = 2.0 * (1.0 - t) * t;
        const double c = t * t;
        points.push_back({a * p0.x + b * p1.x + c * p2.x, a * p0.y + b * p1.y + c * p2.y,
                          a * p0.z + b * p1.z + c * p2.z});
    }
    return points;
}

// The centre of a pointy-top hexagon cell at axial coordinates (q, r) of a honeycomb whose cells
// have circumradius radius_m, with a gap of 8% of the cell width between neighbours.
Vec3 honeycomb_cell(Vec3 origin, double radius_m, int q, int r) {
    const double pitch_m = radius_m * std::sqrt(3.0) * 1.08;
    return {origin.x + pitch_m * (q + 0.5 * r), origin.y + pitch_m * std::sqrt(3.0) / 2.0 * r,
            origin.z};
}

using Strips = std::vector<StripSpec>;
using Builder = std::function<Strips(const PresetValues &)>;

RigPresetParam count_param(const char *id, const char *label, int value, int min, int max) {
    return {id,
            label,
            "",
            static_cast<double>(value),
            static_cast<double>(min),
            static_cast<double>(max),
            true,
            false};
}

RigPresetParam metres_param(const char *id, const char *label, double value, double min,
                            double max) {
    return {id, label, "m", value, min, max, false, false};
}

RigPresetParam toggle_param(const char *id, const char *label, bool on) {
    return {id, label, "", on ? 1.0 : 0.0, 0.0, 1.0, true, true};
}

// A preset from its settings and builder: every preset also gets Scale (the whole shape, about
// the floor's centre) and, unless it sets pixels per metre itself, Density (a multiplier on
// every strip's pixels per metre). `strips` holds the default build.
RigPreset make_preset(const char *name, const char *description, std::vector<RigPresetParam> params,
                      Builder build) {
    RigPreset p{name, description, {}, ""};
    p.params = std::move(params);
    p.params.push_back({"scale", "Scale", "x", 1.0, 0.25, 4.0, false, false});
    const bool own_density =
        std::any_of(p.params.begin(), p.params.end(),
                    [](const RigPresetParam &q) { return q.id == "pixels_per_m"; });
    if (!own_density) {
        p.params.push_back({"density", "Density", "x", 1.0, 0.25, 4.0, false, false});
    }
    p.build = std::move(build);
    p.strips = build_rig_preset(p, {});
    return p;
}

// Names: the k-th of n layers is "outer" (first), "inner" (last, when there are two or more) or
// "middle".
const char *layer_name(int k, int n) { return k == 0 ? "outer" : k == n - 1 ? "inner" : "middle"; }

std::string capitalised(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') {
        s[0] = static_cast<char>(s[0] - 'a' + 'A');
    }
    return s;
}

RigPreset dj_booth_chevron() {
    // A V of two wings behind the DJ, from the apex outwards, over the booth's front edge.
    return make_preset(
        "DJ booth chevron",
        "A V of two wings (2.4 m) behind the DJ over a booth front (2 m). Groups: chevron, wings, "
        "booth.",
        {metres_param("wing_m", "Wing length", 1.7 * std::sqrt(2.0), 0.3, 8.0),
         metres_param("booth_m", "Booth front (0: none)", 2.0, 0.0, 8.0)},
        [](const PresetValues &v) {
            Strips s;
            const double d = v["wing_m"] / std::sqrt(2.0); // At 45 degrees.
            s.push_back(preset_strip("Left wing", {{0.0, 1.3, 0.0}, {-d, 1.3 + d, 0.0}}, 60.0,
                                     {"chevron", "wings"}));
            s.push_back(preset_strip("Right wing", {{0.0, 1.3, 0.0}, {d, 1.3 + d, 0.0}}, 60.0,
                                     {"chevron", "wings"}));
            if (const double b = v["booth_m"]; b > 0.0) {
                s.push_back(preset_strip("Booth front", {{-0.5 * b, 1.0, 0.6}, {0.5 * b, 1.0, 0.6}},
                                         60.0, {"chevron", "booth"}));
            }
            return s;
        });
}

RigPreset arch_tunnel() {
    // Nested arches receding in depth; each half runs from the floor to the apex.
    return make_preset(
        "Arch tunnel",
        "Nested arches (3), 1 m apart front to back, each half from the floor to the apex. "
        "Groups: arches, front, middle, back.",
        {count_param("arches", "Arches", 3, 1, 8),
         metres_param("radius_m", "Front arch radius", 1.8, 0.4, 5.0),
         metres_param("step_m", "Narrower by, each", 0.35, 0.0, 1.0),
         metres_param("depth_m", "Apart, front to back", 1.0, 0.0, 4.0)},
        [](const PresetValues &v) {
            Strips s;
            constexpr double kLegM = 1.0;
            const int n = v.count("arches");
            for (int k = 0; k < n; ++k) {
                const double radius_m = std::max(0.2, v["radius_m"] - v["step_m"] * k);
                const double z = -v["depth_m"] * k;
                const Vec3 centre{0.0, kLegM, z};
                std::vector<Vec3> left{{-radius_m, 0.0, z}};
                for (const Vec3 &p : arc(centre, radius_m, kPi, 0.5 * kPi, 12)) {
                    left.push_back(p);
                }
                std::vector<Vec3> right{{radius_m, 0.0, z}};
                for (const Vec3 &p : arc(centre, radius_m, 0.0, 0.5 * kPi, 12)) {
                    right.push_back(p);
                }
                const char *depth = k == 0 ? "front" : k == n - 1 ? "back" : "middle";
                const std::string name = "Arch " + std::to_string(k + 1);
                s.push_back(preset_strip(name + " left", std::move(left), 60.0, {"arches", depth}));
                s.push_back(
                    preset_strip(name + " right", std::move(right), 60.0, {"arches", depth}));
            }
            return s;
        });
}

RigPreset sunburst() {
    // Rays running outwards from a small core ring, centred 1.8 m up.
    return make_preset(
        "Sunburst",
        "Rays (12, 1.15 m) running outwards from a core ring, centred 1.8 m up. Groups: sun, "
        "rays, core, upper, lower, even, odd.",
        {count_param("rays", "Rays", 12, 3, 36),
         metres_param("ray_m", "Ray length", 1.15, 0.2, 4.0),
         metres_param("core_m", "Core radius", 0.25, 0.05, 1.0)},
        [](const PresetValues &v) {
            Strips s;
            const Vec3 centre{0.0, 1.8, 0.0};
            const double core = v["core_m"];
            s.push_back(preset_strip("Core", polygon(centre, core, 24), 60.0, {"sun", "core"}));
            const int n = v.count("rays");
            const double inner_m = core + 0.1;
            for (int i = 0; i < n; ++i) {
                const double a = 0.5 * kPi + 2.0 * kPi * i / n; // Ray 1 points straight up.
                const Vec3 inner = polar(centre, inner_m, a);
                const Vec3 outer = polar(centre, inner_m + v["ray_m"], a);
                std::vector<std::string> groups{"sun", "rays", i % 2 == 0 ? "even" : "odd"};
                if (outer.y > centre.y + 1e-9) {
                    groups.emplace_back("upper");
                } else if (outer.y < centre.y - 1e-9) {
                    groups.emplace_back("lower");
                }
                s.push_back(preset_strip("Ray " + std::to_string(i + 1), {inner, outer}, 60.0,
                                         std::move(groups)));
            }
            return s;
        });
}

RigPreset hexagon_mandala() {
    // Concentric hexagons joined by six spokes running outwards.
    return make_preset(
        "Hexagon mandala",
        "Concentric hexagons (3, the outer 1.2 m) joined by six spokes, centred 1.7 m up. Centre "
        "Y adds three spokes inside the inner hexagon, from the middle to alternate corners: lit, "
        "the rings read as nested cubes seen corner-on. Groups: hex, outer, middle, inner, "
        "spokes; the centre Y only centre_y (not hex), so layers on hex leave it dark.",
        {count_param("hexagons", "Hexagons", 3, 1, 6),
         metres_param("radius_m", "Outer radius", 1.2, 0.3, 4.0),
         toggle_param("spokes", "Spokes", true), toggle_param("centre_y", "Centre Y", false)},
        [](const PresetValues &v) {
            Strips s;
            const Vec3 centre{0.0, 1.7, 0.0};
            const int n = v.count("hexagons");
            const double r = v["radius_m"];
            for (int k = 0; k < n; ++k) {
                const char *layer = layer_name(k, n);
                const std::string name =
                    n <= 3 ? capitalised(layer) + " hexagon" : "Hexagon " + std::to_string(k + 1);
                s.push_back(
                    preset_strip(name, polygon(centre, r * (n - k) / n, 6), 60.0, {"hex", layer}));
            }
            if (v.on("spokes") && n > 1) {
                for (int i = 0; i < 6; ++i) {
                    const double a = -0.5 * kPi + 2.0 * kPi * i / 6.0;
                    s.push_back(preset_strip("Spoke " + std::to_string(i + 1),
                                             {polar(centre, r / n, a), polar(centre, r, a)}, 60.0,
                                             {"hex", "spokes"}));
                }
            }
            if (v.on("centre_y") && n > 1) {
                // From the middle out to the inner hexagon's bottom, upper right and upper left
                // corners, in line with spokes 1, 3 and 5.
                for (int i = 0; i < 3; ++i) {
                    const double a = -0.5 * kPi + 2.0 * kPi * i / 3.0;
                    s.push_back(preset_strip("Centre spoke " + std::to_string(i + 1),
                                             {centre, polar(centre, r / n, a)}, 60.0,
                                             {"centre_y"}));
                }
            }
            return s;
        });
}

RigPreset goalpost_truss() {
    // A goalpost truss with a stage lip; beam and lip run from the centre outwards.
    return make_preset(
        "Goalpost truss",
        "Two truss legs (3.2 m), a top beam and a stage lip (6 m); beam and lip run from the "
        "centre outwards. Groups: truss, legs, beam, lip.",
        {metres_param("width_m", "Width", 6.0, 1.0, 20.0),
         metres_param("height_m", "Leg height", 3.2, 0.5, 10.0),
         toggle_param("lip", "Stage lip", true)},
        [](const PresetValues &v) {
            Strips s;
            const double x = 0.5 * v["width_m"];
            const double h = v["height_m"];
            s.push_back(
                preset_strip("Left leg", {{-x, 0.0, 0.0}, {-x, h, 0.0}}, 60.0, {"truss", "legs"}));
            s.push_back(
                preset_strip("Right leg", {{x, 0.0, 0.0}, {x, h, 0.0}}, 60.0, {"truss", "legs"}));
            s.push_back(preset_strip("Beam left", {{0.0, h + 0.1, 0.0}, {-x, h + 0.1, 0.0}}, 60.0,
                                     {"truss", "beam"}));
            s.push_back(preset_strip("Beam right", {{0.0, h + 0.1, 0.0}, {x, h + 0.1, 0.0}}, 60.0,
                                     {"truss", "beam"}));
            if (v.on("lip")) {
                s.push_back(
                    preset_strip("Lip left", {{0.0, 0.0, 1.0}, {-x, 0.0, 1.0}}, 30.0, {"lip"}));
                s.push_back(
                    preset_strip("Lip right", {{0.0, 0.0, 1.0}, {x, 0.0, 1.0}}, 30.0, {"lip"}));
            }
            return s;
        });
}

RigPreset triangle_row() {
    // Triangles in a row, pointing up and down in turn.
    return make_preset(
        "Triangle row",
        "Triangles (5, 1.2 m sides) in a row, pointing up and down in turn. Groups: triangles, "
        "up, down, even, odd.",
        {count_param("triangles", "Triangles", 5, 1, 15),
         metres_param("side_m", "Side", 1.2, 0.2, 4.0)},
        [](const PresetValues &v) {
            Strips s;
            const double side = v["side_m"];
            constexpr double kBottomM = 1.0;
            const double top_m = kBottomM + side * std::sqrt(3.0) / 2.0;
            const int n = v.count("triangles");
            const double spacing = 0.7 * side / 1.2;
            for (int i = 0; i < n; ++i) {
                const double cx = (i - 0.5 * (n - 1)) * spacing;
                const bool up = i % 2 == 0;
                // Closed, starting at the point (bottom or top) and going anticlockwise.
                std::vector<Vec3> points = up ? std::vector<Vec3>{{cx - 0.5 * side, kBottomM, 0.0},
                                                                  {cx + 0.5 * side, kBottomM, 0.0},
                                                                  {cx, top_m, 0.0},
                                                                  {cx - 0.5 * side, kBottomM, 0.0}}
                                              : std::vector<Vec3>{{cx, kBottomM, 0.0},
                                                                  {cx + 0.5 * side, top_m, 0.0},
                                                                  {cx - 0.5 * side, top_m, 0.0},
                                                                  {cx, kBottomM, 0.0}};
                s.push_back(preset_strip("Triangle " + std::to_string(i + 1), std::move(points),
                                         60.0,
                                         {"triangles", up ? "up" : "down", up ? "even" : "odd"}));
            }
            return s;
        });
}

RigPreset pixel_curtain() {
    return make_preset(
        "Pixel curtain",
        "Drops (16, 2.4 m, 0.25 m apart, 30 px/m), wired serpentine. Groups: pillars, even, odd.",
        {count_param("drops", "Drops", 16, 1, kMaxTemplateStrips),
         metres_param("height_m", "Drop length", 2.4, 0.2, 10.0),
         metres_param("spacing_m", "Spacing", 0.25, 0.02, 5.0),
         {"pixels_per_m", "Density", "px/m", 30.0, 1.0, 144.0, false, false},
         toggle_param("serpentine", "Serpentine (alternate drops run top to bottom)", true)},
        [](const PresetValues &v) {
            PillarsTemplate t;
            t.count = v.count("drops");
            t.height_m = v["height_m"];
            t.base_m = 0.3;
            t.spacing_m = v["spacing_m"];
            t.pixels_per_m = v["pixels_per_m"];
            t.serpentine = v.on("serpentine");
            return pillars_strips(t);
        });
}

RigPreset pixel_wall() {
    return make_preset(
        "Pixel wall",
        "A low-resolution wall: rows (20) of 2 m at 30 px/m, 0.1 m apart, zigzag wired. Groups: "
        "matrix, top, bottom, even, odd.",
        {count_param("rows", "Rows", 20, 1, kMaxTemplateStrips),
         metres_param("width_m", "Width", 2.0, 0.1, 20.0),
         metres_param("row_spacing_m", "Row spacing", 0.1, 0.005, 2.0),
         {"pixels_per_m", "Density", "px/m", 30.0, 1.0, 144.0, false, false},
         toggle_param("serpentine", "Zigzag (alternate rows run right to left)", true)},
        [](const PresetValues &v) {
            MatrixTemplate t;
            t.rows = v.count("rows");
            t.width_m = v["width_m"];
            t.row_spacing_m = v["row_spacing_m"];
            t.base_m = 0.5;
            t.pixels_per_m = v["pixels_per_m"];
            t.serpentine = v.on("serpentine");
            Strips rows = matrix_strips(t);
            for (StripSpec &s : rows) {
                s.groups.insert(s.groups.end(), {"left", "right"}); // Centred rows span both.
            }
            return rows;
        });
}

RigPreset hive_portal() {
    // Nested lens portals receding in depth, the DJ hidden behind; each half runs from the bottom
    // tip to the top tip.
    return make_preset(
        "Hive portal",
        "Nested lens-shaped portals (3), 0.8 m apart front to back, the tallest 3.2 m; each half "
        "from the bottom tip to the top. Groups: portal, outer, middle, inner.",
        {count_param("portals", "Portals", 3, 1, 5),
         metres_param("height_m", "Front portal height", 3.2, 1.0, 8.0),
         metres_param("depth_m", "Apart, front to back", 0.8, 0.0, 3.0)},
        [](const PresetValues &v) {
            Strips s;
            const int n = v.count("portals");
            const double f = v["height_m"] / 3.2; // The default's proportions, scaled.
            for (int k = 0; k < n; ++k) {
                const double height_m = (3.2 - 0.6 * k) * f;
                const double width_m = std::max(0.2, (2.2 - 0.5 * k) * f);
                const double z = -v["depth_m"] * k;
                const Vec3 bottom{0.0, 1.8 - 0.5 * height_m, z};
                const Vec3 top{0.0, 1.8 + 0.5 * height_m, z};
                const char *layer = layer_name(k, n);
                std::string name = capitalised(layer);
                if (n > 3 && k > 0 && k < n - 1) {
                    name += " " + std::to_string(k);
                }
                s.push_back(preset_strip(name + " left", lens_side(bottom, top, width_m), 60.0,
                                         {"portal", layer}));
                s.push_back(preset_strip(name + " right", reversed(lens_side(top, bottom, width_m)),
                                         60.0, {"portal", layer}));
            }
            return s;
        });
}

RigPreset honeycomb() {
    // Hexagon cells in rings around a core cell.
    return make_preset(
        "Honeycomb",
        "Hexagon cells (0.7 m) in a hive, 30 px/m: a core cell and rings around it (2 rings: 19 "
        "cells). Groups: comb, core, ring1, ring2 and so on, upper, lower.",
        {count_param("rings", "Rings", 2, 0, 4),
         metres_param("cell_m", "Cell width", 0.7, 0.2, 2.0)},
        [](const PresetValues &v) {
            Strips s;
            const int rings = v.count("rings");
            const double radius_m = 0.5 * v["cell_m"];
            const double pitch_m = radius_m * std::sqrt(3.0) * 1.08;
            // High enough that the lowest cell clears the floor.
            const double origin_y =
                std::max(1.9, 0.3 + rings * pitch_m * std::sqrt(3.0) / 2.0 + radius_m);
            const Vec3 origin{0.0, origin_y, 0.0};
            int n = 0;
            for (int q = -rings; q <= rings; ++q) {
                for (int r = -rings; r <= rings; ++r) {
                    const int ring = std::max({std::abs(q), std::abs(r), std::abs(q + r)});
                    if (ring > rings) {
                        continue;
                    }
                    const Vec3 centre = honeycomb_cell(origin, radius_m, q, r);
                    std::vector<std::string> groups{
                        "comb", ring == 0 ? std::string("core") : "ring" + std::to_string(ring)};
                    if (r > 0) {
                        groups.emplace_back("upper");
                    } else if (r < 0) {
                        groups.emplace_back("lower");
                    }
                    s.push_back(preset_strip("Cell " + std::to_string(++n),
                                             polygon(centre, radius_m, 6), 30.0,
                                             std::move(groups)));
                }
            }
            return s;
        });
}

RigPreset lotus() {
    // A seed ring, inner petals and outer petals between them; each petal runs out from its base
    // along one side and back along the other.
    return make_preset(
        "Lotus",
        "A seed ring with inner petals (8, 60 px/m) and larger outer petals between them (30 "
        "px/m), 3.2 m across. Groups: lotus, seed, inner, outer, even, odd, upper, lower.",
        {count_param("petals", "Petals per layer", 8, 3, 16),
         toggle_param("outer", "Outer petals", true)},
        [](const PresetValues &v) {
            Strips s;
            const Vec3 centre{0.0, 1.8, 0.0};
            s.push_back(preset_strip("Seed", polygon(centre, 0.18, 16), 60.0, {"lotus", "seed"}));
            const int n = v.count("petals");
            const auto petals = [&](const char *layer, double offset, double from_m, double to_m,
                                    double width_m, double pixels_per_m) {
                for (int i = 0; i < n; ++i) {
                    const double a = 0.5 * kPi + 2.0 * kPi * (i + offset) / n;
                    std::vector<std::string> groups{"lotus", layer, i % 2 == 0 ? "even" : "odd"};
                    const double up = std::sin(a);
                    if (up > 1e-9) {
                        groups.emplace_back("upper");
                    } else if (up < -1e-9) {
                        groups.emplace_back("lower");
                    }
                    s.push_back(preset_strip(
                        capitalised(layer) + " petal " + std::to_string(i + 1),
                        lens_loop(polar(centre, from_m, a), polar(centre, to_m, a), width_m),
                        pixels_per_m, std::move(groups)));
                }
            };
            petals("inner", 0.0, 0.24, 0.9, 0.36, 60.0);
            if (v.on("outer")) {
                petals("outer", 0.5, 0.55, 1.6, 0.62, 30.0);
            }
            return s;
        });
}

RigPreset third_eye() {
    // An eye in a pyramid: lids, iris, a slit pupil, lashes, and the pyramid behind.
    return make_preset(
        "Third eye",
        "A 3 m eye (lids left to right, iris, slit pupil, lashes) inside a 4.6 m pyramid. "
        "Groups: eye, lids, iris, pupil, lashes, pyramid.",
        {count_param("lashes", "Lashes", 9, 0, 21), toggle_param("pyramid", "Pyramid", true)},
        [](const PresetValues &v) {
            Strips s;
            const Vec3 centre{0.0, 1.9, 0.0};
            const Vec3 left{-1.5, 1.9, 0.0};
            const Vec3 right{1.5, 1.9, 0.0};
            s.push_back(
                preset_strip("Upper lid", lens_side(left, right, 1.3), 60.0, {"eye", "lids"}));
            s.push_back(preset_strip("Lower lid", reversed(lens_side(right, left, 1.3)), 60.0,
                                     {"eye", "lids"}));
            s.push_back(preset_strip("Iris", polygon(centre, 0.5, 32), 60.0, {"eye", "iris"}));
            s.push_back(preset_strip("Pupil", lens_loop({0.0, 1.5, 0.0}, {0.0, 2.3, 0.0}, 0.22),
                                     60.0, {"eye", "pupil"}));
            const int n = v.count("lashes");
            for (int i = 0; i < n; ++i) {
                const double deg = n == 1 ? 90.0 : 35.0 + 110.0 * i / (n - 1);
                const double a = deg * kPi / 180.0;
                s.push_back(preset_strip("Lash " + std::to_string(i + 1),
                                         {polar(centre, 0.85, a), polar(centre, 1.2, a)}, 60.0,
                                         {"eye", "lashes"}));
            }
            if (v.on("pyramid")) {
                const double apex_m = 0.3 + 4.6 * std::sqrt(3.0) / 2.0;
                s.push_back(preset_strip(
                    "Pyramid",
                    {{-2.3, 0.3, 0.0}, {2.3, 0.3, 0.0}, {0.0, apex_m, 0.0}, {-2.3, 0.3, 0.0}}, 30.0,
                    {"pyramid"}));
            }
            return s;
        });
}

RigPreset string_art_star() {
    // A star {points/skip}: each chord runs from point i to point i + skip, so a chase along the
    // strips turns the whole star.
    return make_preset(
        "String-art star",
        "Chords of a 3 m circle joining every fifth of 12 points (a {12/5} star), inside a ring, "
        "30 px/m. Groups: string, chords, ring, even, odd.",
        {count_param("points", "Points", 12, 5, 36), count_param("skip", "Join every", 5, 1, 17),
         metres_param("radius_m", "Radius", 1.5, 0.5, 4.0), toggle_param("ring", "Ring", true)},
        [](const PresetValues &v) {
            Strips s;
            const Vec3 centre{0.0, 1.9, 0.0};
            const int n = v.count("points");
            const int skip = std::clamp(v.count("skip"), 1, std::max(1, n / 2));
            const double r = v["radius_m"];
            for (int i = 0; i < n; ++i) {
                const double from = 0.5 * kPi + 2.0 * kPi * i / n;
                const double to = 0.5 * kPi + 2.0 * kPi * (i + skip) / n;
                s.push_back(preset_strip("Chord " + std::to_string(i + 1),
                                         {polar(centre, r, from), polar(centre, r, to)}, 30.0,
                                         {"string", "chords", i % 2 == 0 ? "even" : "odd"}));
            }
            if (v.on("ring")) {
                s.push_back(
                    preset_strip("Ring", polygon(centre, r + 0.1, 48), 30.0, {"string", "ring"}));
            }
            return s;
        });
}

RigPreset alien_ribcage() {
    // A spine with pairs of arched ribs, widest in the middle, each from the spine outwards.
    return make_preset(
        "Alien ribcage",
        "A 2.8 m spine with pairs of arched ribs (7) running outwards, widest in the middle. "
        "Groups: ribcage, spine, ribs, upper, lower, even, odd.",
        {count_param("ribs", "Rib pairs", 7, 1, 12),
         metres_param("spine_m", "Spine length", 2.8, 1.0, 6.0)},
        [](const PresetValues &v) {
            Strips s;
            const double spine = v["spine_m"];
            s.push_back(preset_strip("Spine", {{0.0, 0.4, 0.0}, {0.0, 0.4 + spine, 0.0}}, 60.0,
                                     {"ribcage", "spine"}));
            const int n = v.count("ribs");
            const double middle = 0.5 * (n - 1);
            for (int k = 0; k < n; ++k) {
                const double along = n == 1 ? 0.5 : static_cast<double>(k) / (n - 1);
                const double y = 0.4 + spine * (0.4 + 2.0 * along) / 2.8;
                const double reach_m = 0.45 + 1.35 * std::sin(kPi * (k + 0.5) / n);
                for (const double side : {-1.0, 1.0}) {
                    std::vector<std::string> groups{"ribcage", "ribs", k % 2 == 0 ? "even" : "odd"};
                    if (k > middle + 1e-9) {
                        groups.emplace_back("upper");
                    } else if (k < middle - 1e-9) {
                        groups.emplace_back("lower");
                    }
                    s.push_back(
                        preset_strip(std::string(side < 0.0 ? "Left" : "Right") + " rib " +
                                         std::to_string(k + 1),
                                     bezier({0.0, y, 0.0}, {side * 0.7 * reach_m, y + 0.3, 0.0},
                                            {side * reach_m, y - 0.45, 0.0}),
                                     60.0, std::move(groups)));
                }
            }
            return s;
        });
}

void add_branches(Strips &s, Vec3 from, double angle_rad, double length_m, int depth, int levels,
                  double spread_rad, double ratio) {
    const Vec3 to{from.x + length_m * std::cos(angle_rad), from.y + length_m * std::sin(angle_rad),
                  from.z};
    const char *level = depth == 0 ? "trunk" : depth <= 2 ? "branches" : "twigs";
    const char *name = depth == 0 ? "Trunk" : depth <= 2 ? "Branch" : "Twig";
    s.push_back(preset_strip(std::string(name) + " " + std::to_string(s.size() + 1), {from, to},
                             60.0, {"tree", level}));
    if (depth < levels) {
        for (const double side : {-1.0, 1.0}) {
            add_branches(s, to, angle_rad + side * spread_rad, ratio * length_m, depth + 1, levels,
                         spread_rad, ratio);
        }
    }
}

RigPreset fractal_tree() {
    // A trunk splitting in two, again and again; every strip runs from the root towards the tips.
    return make_preset(
        "Fractal tree",
        "A 1.2 m trunk splitting in two four times (31 strips), every strip running from the root "
        "to the tips. Groups: tree, trunk, branches, twigs.",
        {count_param("splits", "Splits", 4, 0, 6),
         metres_param("trunk_m", "Trunk length", 1.2, 0.3, 3.0),
         {"angle_deg", "Branch angle", "deg", 27.0, 5.0, 60.0, false, false},
         {"ratio", "Each branch, of the last", "x", 0.72, 0.4, 0.9, false, false}},
        [](const PresetValues &v) {
            Strips s;
            add_branches(s, {0.0, 0.0, 0.0}, 0.5 * kPi, v["trunk_m"], 0, v.count("splits"),
                         v["angle_deg"] * kPi / 180.0, v["ratio"]);
            return s;
        });
}

RigPreset vortex() {
    // Logarithmic spiral arms, each from the centre outwards.
    return make_preset(
        "Vortex",
        "Logarithmic spiral arms (6), each from the centre outwards over 0.9 of a turn, 3.2 m "
        "across. Groups: vortex, arms, even, odd.",
        {count_param("arms", "Arms", 6, 1, 16),
         {"turns", "Turns", "", 0.9, 0.2, 3.0, false, false},
         metres_param("radius_m", "Radius", 1.6, 0.4, 4.0)},
        [](const PresetValues &v) {
            Strips s;
            const Vec3 centre{0.0, 1.9, 0.0};
            constexpr double kInnerM = 0.15;
            const double outer_m = std::max(kInnerM + 0.05, v["radius_m"]);
            const double turn_rad = v["turns"] * 2.0 * kPi;
            const double growth = std::log(outer_m / kInnerM) / turn_rad;
            const int n = v.count("arms");
            for (int k = 0; k < n; ++k) {
                const double offset = 0.5 * kPi + 2.0 * kPi * k / n;
                std::vector<Vec3> points;
                for (int i = 0; i <= 48; ++i) {
                    const double t = turn_rad * i / 48.0;
                    points.push_back(polar(centre, kInnerM * std::exp(growth * t), offset + t));
                }
                s.push_back(preset_strip("Arm " + std::to_string(k + 1), std::move(points), 60.0,
                                         {"vortex", "arms", k % 2 == 0 ? "even" : "odd"}));
            }
            return s;
        });
}

RigPreset hive_temple() {
    // A temple front: a double lens portal, honeycomb wings at its shoulders, rays crowning it.
    return make_preset(
        "Hive temple",
        "A temple front: a double lens portal (3 m), honeycomb wings (4 cells either side) and "
        "rays crowning the top (7). Groups: temple, portal, outer, inner, comb, crown.",
        {count_param("cells", "Cells per wing", 4, 0, 4),
         count_param("rays", "Crown rays", 7, 0, 15)},
        [](const PresetValues &v) {
            Strips s;
            for (const auto &[layer, height_m, width_m] :
                 {std::tuple{"outer", 3.0, 1.9}, std::tuple{"inner", 2.3, 1.25}}) {
                const Vec3 bottom{0.0, 1.75 - 0.5 * height_m, 0.0};
                const Vec3 top{0.0, 1.75 + 0.5 * height_m, 0.0};
                const std::string name = capitalised(layer);
                s.push_back(preset_strip(name + " portal left", lens_side(bottom, top, width_m),
                                         60.0, {"temple", "portal", layer}));
                s.push_back(preset_strip(name + " portal right",
                                         reversed(lens_side(top, bottom, width_m)), 60.0,
                                         {"temple", "portal", layer}));
            }
            const int cells[4][2] = {{0, 0}, {1, 0}, {0, 1}, {-1, 1}};
            const int n_cells = v.count("cells");
            for (const double side : {-1.0, 1.0}) {
                for (int i = 0; i < n_cells; ++i) {
                    Vec3 centre = honeycomb_cell({1.6, 1.55, 0.0}, 0.34, cells[i][0], cells[i][1]);
                    centre.x *= side;
                    s.push_back(preset_strip(std::string(side < 0.0 ? "Left" : "Right") + " cell " +
                                                 std::to_string(i + 1),
                                             polygon(centre, 0.34, 6), 30.0, {"temple", "comb"}));
                }
            }
            const Vec3 crown{0.0, 3.25, 0.0};
            const int n_rays = v.count("rays");
            for (int i = 0; i < n_rays; ++i) {
                const double deg = n_rays == 1 ? 90.0 : 30.0 + 120.0 * i / (n_rays - 1);
                const double a = deg * kPi / 180.0;
                s.push_back(preset_strip("Ray " + std::to_string(i + 1),
                                         {polar(crown, 0.2, a), polar(crown, 0.75, a)}, 60.0,
                                         {"temple", "crown"}));
            }
            return s;
        });
}

// A quadratic Bezier from p0 to p2 (pulled towards p1) with `teeth` triangular spikes of
// height_m standing out to its left (sign 1) or right (sign -1): one edge of a carved silhouette.
std::vector<Vec3> spiked_edge(Vec3 p0, Vec3 p1, Vec3 p2, int teeth, double height_m, double sign) {
    if (teeth <= 0) {
        return bezier(p0, p1, p2);
    }
    const std::vector<Vec3> curve = bezier(p0, p1, p2, 2 * teeth);
    std::vector<Vec3> points;
    for (size_t i = 0; i < curve.size(); ++i) {
        Vec3 p = curve[i];
        if (i % 2 == 1) { // The tooth's tip, off the curve's tangent.
            const double dx = curve[i + 1].x - curve[i - 1].x;
            const double dy = curve[i + 1].y - curve[i - 1].y;
            const double length = std::hypot(dx, dy);
            p.x += sign * height_m * -dy / length;
            p.y += sign * height_m * dx / length;
        }
        points.push_back(p);
    }
    return points;
}

RigPreset hive_stage() {
    // A carved stage front in the round, 1.4 by 0.75 m (a wall frame at home): a mandala with an
    // eye in the middle, a tunnel of rings either side, a spiked silhouette, gems around the
    // mandala and a halo behind it all. Mirrored halves run from the centre outwards.
    return make_preset(
        "Hive stage",
        "A carved stage front, 1.4 by 0.75 m: a mandala (core, ring, rays), a tunnel of rings "
        "either side, a spiked contour, a hexagon round the mandala, gems (single pixels) and a "
        "halo 4 cm behind, along the contour. Groups: stage, mandala, core, ring, rays, tunnels, "
        "outer, inner, contour, hexagon, gems, halo.",
        {count_param("rays", "Mandala rays", 8, 0, 24),
         count_param("tunnel_rings", "Rings per tunnel", 2, 0, 4),
         count_param("spikes", "Spikes per edge", 4, 0, 12), count_param("gems", "Gems", 8, 0, 24),
         toggle_param("halo", "Halo behind", true), toggle_param("hexagon", "Hexagon", true),
         metres_param("hexagon_m", "Hexagon radius", 0.275, 0.1, 0.4)},
        [](const PresetValues &v) {
            Strips s;
            const Vec3 centre{0.0, 1.4, 0.0};
            const auto at = [&](double x, double y) { return Vec3{x, centre.y + y, 0.0}; };
            s.push_back(preset_strip("Core", polygon(centre, 0.08, 24), 60.0,
                                     {"stage", "mandala", "core"}));
            s.push_back(preset_strip("Mandala ring", polygon(centre, 0.22, 48), 60.0,
                                     {"stage", "mandala", "ring"}));
            const int n_rays = v.count("rays");
            for (int i = 0; i < n_rays; ++i) {
                const double a = 0.5 * kPi + 2.0 * kPi * i / n_rays;
                s.push_back(preset_strip("Ray " + std::to_string(i + 1),
                                         {polar(centre, 0.11, a), polar(centre, 0.205, a)}, 60.0,
                                         {"stage", "mandala", "rays"}));
            }
            // Tunnels: rings around a bore 0.43 m out, each from its point nearest the centre,
            // the right one turning the other way (mirrored).
            const int n_rings = v.count("tunnel_rings");
            for (int k = 0; k < n_rings; ++k) {
                const double radius_m = n_rings == 1 ? 0.12 : 0.12 - 0.06 * k / (n_rings - 1);
                const char *layer = layer_name(k, n_rings);
                const std::string ring = n_rings == 1 ? "ring" : capitalised(layer) + " ring";
                s.push_back(preset_strip("Left tunnel " + ring,
                                         arc(at(-0.43, 0.0), radius_m, 0.0, 2.0 * kPi, 32), 60.0,
                                         {"stage", "tunnels", layer}));
                s.push_back(preset_strip("Right tunnel " + ring,
                                         arc(at(0.43, 0.0), radius_m, kPi, -kPi, 32), 60.0,
                                         {"stage", "tunnels", layer}));
            }
            // Contour: upper and lower edges, each from the middle out to the side tip.
            const int teeth = v.count("spikes");
            for (const double side : {-1.0, 1.0}) {
                const std::string name = side < 0.0 ? "Left" : "Right";
                // Outwards is to the left of the upper edge's direction on the right side.
                s.push_back(preset_strip(name + " upper contour",
                                         spiked_edge(at(0.0, 0.33), at(side * 0.45, 0.33),
                                                     at(side * 0.68, 0.0), teeth, 0.04, side),
                                         60.0, {"stage", "contour"}));
                s.push_back(preset_strip(name + " lower contour",
                                         spiked_edge(at(0.0, -0.33), at(side * 0.45, -0.33),
                                                     at(side * 0.68, 0.0), teeth, 0.04, -side),
                                         60.0, {"stage", "contour"}));
            }
            // Gems: one pixel each, around the mandala from the bottom; the strip's pitch is the
            // gems' spacing, its polyline starting half a pitch before the first.
            if (const int n_gems = v.count("gems"); n_gems > 0) {
                const double half_step = kPi / n_gems;
                std::vector<Vec3> ring =
                    arc(centre, 0.28, -0.5 * kPi - half_step, 1.5 * kPi - half_step, 8 * n_gems);
                const double per_m = n_gems / polyline_length_m(ring);
                s.push_back(preset_strip("Gems", std::move(ring), per_m, {"stage", "gems"}));
            }
            // Hexagon around the mandala: points left and right, flat top and bottom; each half
            // from the top edge's middle round its side point to the bottom edge's middle.
            if (v.on("hexagon")) {
                const double r = v["hexagon_m"];
                for (const double side : {-1.0, 1.0}) {
                    std::vector<Vec3> half{at(0.0, r * std::sqrt(3.0) / 2.0)};
                    for (const double deg : {60.0, 0.0, -60.0}) {
                        const double a = deg * kPi / 180.0;
                        half.push_back(at(side * r * std::cos(a), r * std::sin(a)));
                    }
                    half.push_back(at(0.0, -r * std::sqrt(3.0) / 2.0));
                    s.push_back(preset_strip(side < 0.0 ? "Left hexagon" : "Right hexagon",
                                             std::move(half), 60.0, {"stage", "hexagon"}));
                }
            }
            // Halo: the contour's shape, smooth and 0.95 of its size, 4 cm behind (lighting the
            // back panel along the silhouette, clear of the tunnels); each half from the top
            // centre round its side tip to the bottom.
            if (v.on("halo")) {
                for (const double side : {-1.0, 1.0}) {
                    const auto behind = [&](double x, double y) {
                        return Vec3{0.95 * x, centre.y + 0.95 * y, -0.04};
                    };
                    std::vector<Vec3> halo = bezier(behind(0.0, 0.33), behind(side * 0.45, 0.33),
                                                    behind(side * 0.68, 0.0));
                    const std::vector<Vec3> lower = bezier(
                        behind(side * 0.68, 0.0), behind(side * 0.45, -0.33), behind(0.0, -0.33));
                    halo.insert(halo.end(), lower.begin() + 1, lower.end());
                    s.push_back(preset_strip(side < 0.0 ? "Left halo" : "Right halo",
                                             std::move(halo), 60.0, {"stage", "halo"}));
                }
            }
            return s;
        });
}

} // namespace

double PresetValues::operator[](std::string_view id) const {
    for (size_t i = 0; i < params_.size(); ++i) {
        if (params_[i].id == id) {
            const double value = i < values_.size() ? values_[i] : params_[i].value;
            return std::clamp(value, params_[i].min, params_[i].max);
        }
    }
    return 0.0;
}

int PresetValues::count(std::string_view id) const {
    return static_cast<int>(std::lround((*this)[id]));
}

bool PresetValues::on(std::string_view id) const { return (*this)[id] >= 0.5; }

std::vector<StripSpec> build_rig_preset(const RigPreset &preset,
                                        const std::vector<double> &values) {
    if (!preset.build) {
        return preset.strips; // A fixed rig (the user's own).
    }
    const PresetValues v(preset.params, values);
    std::vector<StripSpec> strips = preset.build(v);
    const bool has_density = std::any_of(preset.params.begin(), preset.params.end(),
                                         [](const RigPresetParam &q) { return q.id == "density"; });
    const double scale = v["scale"];
    const double density = has_density ? v["density"] : 1.0;
    for (StripSpec &s : strips) {
        for (Vec3 &p : s.polyline) {
            p.x *= scale;
            p.y *= scale;
            p.z *= scale;
        }
        s.pixels_per_m *= density;
    }
    fit_counts(strips); // Every strip fills its pieces, joints taken out.
    return strips;
}

std::vector<RigPreset> builtin_rig_presets() {
    std::vector<RigPreset> presets;
    const auto add = [&](const char *category, std::initializer_list<RigPreset> rigs) {
        for (RigPreset rig : rigs) {
            rig.category = category;
            presets.push_back(std::move(rig));
        }
    };
    add("Stage builds", {dj_booth_chevron(), arch_tunnel(), goalpost_truss(), triangle_row(),
                         pixel_curtain(), pixel_wall()});
    add("Hive", {hive_stage(), hive_temple(), hive_portal(), honeycomb()});
    add("Sacred geometry",
        {sunburst(), hexagon_mandala(), string_art_star(), vortex(), third_eye()});
    add("Organic", {lotus(), alien_ribcage(), fractal_tree()});
    return presets;
}

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
