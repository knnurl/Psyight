#include "engine/rig_templates.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
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

RigPreset dj_booth_chevron() {
    // A V of two wings behind the DJ, from the apex outwards, over the booth's front edge.
    RigPreset p{"DJ booth chevron",
                "A V of two 2.4 m wings behind the DJ over a 2 m booth front. Groups: chevron, "
                "wings, booth.",
                {}};
    p.strips.push_back(
        preset_strip("Left wing", {{0.0, 1.3, 0.0}, {-1.7, 3.0, 0.0}}, 60.0, {"chevron", "wings"}));
    p.strips.push_back(
        preset_strip("Right wing", {{0.0, 1.3, 0.0}, {1.7, 3.0, 0.0}}, 60.0, {"chevron", "wings"}));
    p.strips.push_back(preset_strip("Booth front", {{-1.0, 1.0, 0.6}, {1.0, 1.0, 0.6}}, 60.0,
                                    {"chevron", "booth"}));
    return p;
}

RigPreset triple_arch_tunnel() {
    // Three nested arches, 1 m apart in depth; each half runs from the floor to the apex.
    RigPreset p{"Triple arch tunnel",
                "Three nested arches, 1 m apart front to back, each half from the floor to the "
                "apex. Groups: arches, front, middle, back.",
                {}};
    constexpr double kLegM = 1.0;
    const char *const depth[3] = {"front", "middle", "back"};
    for (int k = 0; k < 3; ++k) {
        const double radius_m = 1.8 - 0.35 * k;
        const double z = -1.0 * k;
        const Vec3 centre{0.0, kLegM, z};
        std::vector<Vec3> left{{-radius_m, 0.0, z}};
        for (const Vec3 &v : arc(centre, radius_m, kPi, 0.5 * kPi, 12)) {
            left.push_back(v);
        }
        std::vector<Vec3> right{{radius_m, 0.0, z}};
        for (const Vec3 &v : arc(centre, radius_m, 0.0, 0.5 * kPi, 12)) {
            right.push_back(v);
        }
        const std::string n = std::to_string(k + 1);
        p.strips.push_back(
            preset_strip("Arch " + n + " left", std::move(left), 60.0, {"arches", depth[k]}));
        p.strips.push_back(
            preset_strip("Arch " + n + " right", std::move(right), 60.0, {"arches", depth[k]}));
    }
    return p;
}

RigPreset sunburst() {
    // Twelve rays running outwards from a small core ring, centred 1.8 m up.
    RigPreset p{"Sunburst",
                "Twelve 1.15 m rays running outwards from a core ring, centred 1.8 m up. "
                "Groups: sun, rays, core, upper, lower, even, odd.",
                {}};
    const Vec3 centre{0.0, 1.8, 0.0};
    p.strips.push_back(preset_strip("Core", polygon(centre, 0.25, 24), 60.0, {"sun", "core"}));
    for (int i = 0; i < 12; ++i) {
        const double a = 0.5 * kPi + 2.0 * kPi * i / 12.0; // Ray 1 points straight up.
        const Vec3 inner{centre.x + 0.35 * std::cos(a), centre.y + 0.35 * std::sin(a), 0.0};
        const Vec3 outer{centre.x + 1.5 * std::cos(a), centre.y + 1.5 * std::sin(a), 0.0};
        std::vector<std::string> groups{"sun", "rays", i % 2 == 0 ? "even" : "odd"};
        if (outer.y > centre.y + 1e-9) {
            groups.emplace_back("upper");
        } else if (outer.y < centre.y - 1e-9) {
            groups.emplace_back("lower");
        }
        p.strips.push_back(
            preset_strip("Ray " + std::to_string(i + 1), {inner, outer}, 60.0, std::move(groups)));
    }
    return p;
}

RigPreset hexagon_mandala() {
    // Three concentric hexagons joined by six spokes running outwards.
    RigPreset p{"Hexagon mandala",
                "Three concentric hexagons (1.2, 0.8 and 0.4 m) joined by six spokes, centred "
                "1.7 m up. Groups: hex, outer, middle, inner, spokes.",
                {}};
    const Vec3 centre{0.0, 1.7, 0.0};
    p.strips.push_back(
        preset_strip("Outer hexagon", polygon(centre, 1.2, 6), 60.0, {"hex", "outer"}));
    p.strips.push_back(
        preset_strip("Middle hexagon", polygon(centre, 0.8, 6), 60.0, {"hex", "middle"}));
    p.strips.push_back(
        preset_strip("Inner hexagon", polygon(centre, 0.4, 6), 60.0, {"hex", "inner"}));
    for (int i = 0; i < 6; ++i) {
        const double a = -0.5 * kPi + 2.0 * kPi * i / 6.0;
        const Vec3 from{centre.x + 0.4 * std::cos(a), centre.y + 0.4 * std::sin(a), 0.0};
        const Vec3 to{centre.x + 1.2 * std::cos(a), centre.y + 1.2 * std::sin(a), 0.0};
        p.strips.push_back(
            preset_strip("Spoke " + std::to_string(i + 1), {from, to}, 60.0, {"hex", "spokes"}));
    }
    return p;
}

RigPreset goalpost_truss() {
    // A 6 m wide goalpost truss with a stage lip; beam and lip run from the centre outwards.
    RigPreset p{"Goalpost truss",
                "Two 3.2 m truss legs, a 6 m top beam and a 6 m stage lip; beam and lip run from "
                "the centre outwards. Groups: truss, legs, beam, lip.",
                {}};
    p.strips.push_back(
        preset_strip("Left leg", {{-3.0, 0.0, 0.0}, {-3.0, 3.2, 0.0}}, 60.0, {"truss", "legs"}));
    p.strips.push_back(
        preset_strip("Right leg", {{3.0, 0.0, 0.0}, {3.0, 3.2, 0.0}}, 60.0, {"truss", "legs"}));
    p.strips.push_back(
        preset_strip("Beam left", {{0.0, 3.3, 0.0}, {-3.0, 3.3, 0.0}}, 60.0, {"truss", "beam"}));
    p.strips.push_back(
        preset_strip("Beam right", {{0.0, 3.3, 0.0}, {3.0, 3.3, 0.0}}, 60.0, {"truss", "beam"}));
    p.strips.push_back(
        preset_strip("Lip left", {{0.0, 0.0, 1.0}, {-3.0, 0.0, 1.0}}, 30.0, {"lip"}));
    p.strips.push_back(
        preset_strip("Lip right", {{0.0, 0.0, 1.0}, {3.0, 0.0, 1.0}}, 30.0, {"lip"}));
    return p;
}

RigPreset triangle_row() {
    // Five 1.2 m triangles in a row, pointing up and down in turn.
    RigPreset p{"Triangle row",
                "Five 1.2 m triangles in a row, pointing up and down in turn. Groups: triangles, "
                "up, down, even, odd.",
                {}};
    constexpr double kSideM = 1.2;
    constexpr double kBottomM = 1.0;
    const double top_m = kBottomM + kSideM * std::sqrt(3.0) / 2.0;
    for (int i = 0; i < 5; ++i) {
        const double cx = (i - 2) * 0.7;
        const bool up = i % 2 == 0;
        // Closed, starting at the point (bottom or top) and going anticlockwise.
        std::vector<Vec3> points = up ? std::vector<Vec3>{{cx - 0.5 * kSideM, kBottomM, 0.0},
                                                          {cx + 0.5 * kSideM, kBottomM, 0.0},
                                                          {cx, top_m, 0.0},
                                                          {cx - 0.5 * kSideM, kBottomM, 0.0}}
                                      : std::vector<Vec3>{{cx, kBottomM, 0.0},
                                                          {cx + 0.5 * kSideM, top_m, 0.0},
                                                          {cx - 0.5 * kSideM, top_m, 0.0},
                                                          {cx, kBottomM, 0.0}};
        p.strips.push_back(preset_strip("Triangle " + std::to_string(i + 1), std::move(points),
                                        60.0,
                                        {"triangles", up ? "up" : "down", up ? "even" : "odd"}));
    }
    return p;
}

RigPreset pixel_curtain() {
    PillarsTemplate t;
    t.count = 16;
    t.height_m = 2.4;
    t.base_m = 0.3;
    t.spacing_m = 0.25;
    t.pixels_per_m = 30.0;
    t.serpentine = true;
    return {"Pixel curtain",
            "Sixteen 2.4 m drops, 0.25 m apart, 30 px/m, wired serpentine. Groups: pillars, "
            "even, odd.",
            pillars_strips(t)};
}

RigPreset pixel_wall() {
    MatrixTemplate t;
    t.rows = 20;
    t.width_m = 2.0;
    t.row_spacing_m = 0.1;
    t.base_m = 0.5;
    t.pixels_per_m = 30.0;
    t.serpentine = true;
    std::vector<StripSpec> rows = matrix_strips(t);
    for (StripSpec &s : rows) {
        s.groups.insert(s.groups.end(), {"left", "right"}); // Centred rows span both.
    }
    return {"Pixel wall",
            "A 2 by 2 m low-resolution wall: 20 rows of 60 pixels, zigzag wired. Groups: matrix, "
            "top, bottom, even, odd.",
            std::move(rows)};
}

RigPreset hive_portal() {
    // Three nested lens portals receding in depth, the DJ hidden behind; each half runs from the
    // bottom tip to the top tip.
    RigPreset p{"Hive portal",
                "Three nested lens-shaped portals, 0.8 m apart front to back, the tallest 3.2 m; "
                "each half from the bottom tip to the top. Groups: portal, outer, middle, inner.",
                {}};
    struct Layer {
        const char *name;
        double height_m;
        double width_m;
    };
    const Layer layers[3] = {{"outer", 3.2, 2.2}, {"middle", 2.6, 1.7}, {"inner", 2.0, 1.2}};
    for (int k = 0; k < 3; ++k) {
        const Layer &l = layers[k];
        const double z = -0.8 * k;
        const Vec3 bottom{0.0, 1.8 - 0.5 * l.height_m, z};
        const Vec3 top{0.0, 1.8 + 0.5 * l.height_m, z};
        std::string name = l.name;
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
        p.strips.push_back(preset_strip(name + " left", lens_side(bottom, top, l.width_m), 60.0,
                                        {"portal", l.name}));
        p.strips.push_back(preset_strip(name + " right",
                                        reversed(lens_side(top, bottom, l.width_m)), 60.0,
                                        {"portal", l.name}));
    }
    return p;
}

RigPreset honeycomb() {
    // Nineteen hexagon cells: a core, a ring of 6 and a ring of 12.
    RigPreset p{"Honeycomb",
                "Nineteen 0.7 m hexagon cells in a hive, 30 px/m: a core cell and two rings around "
                "it. Groups: comb, core, ring1, ring2, upper, lower.",
                {}};
    const Vec3 origin{0.0, 1.9, 0.0};
    int n = 0;
    for (int q = -2; q <= 2; ++q) {
        for (int r = -2; r <= 2; ++r) {
            const int ring = std::max({std::abs(q), std::abs(r), std::abs(q + r)});
            if (ring > 2) {
                continue;
            }
            const Vec3 centre = honeycomb_cell(origin, 0.35, q, r);
            std::vector<std::string> groups{"comb",
                                            ring == 0 ? "core" : ring == 1 ? "ring1" : "ring2"};
            if (r > 0) {
                groups.emplace_back("upper");
            } else if (r < 0) {
                groups.emplace_back("lower");
            }
            p.strips.push_back(preset_strip("Cell " + std::to_string(++n), polygon(centre, 0.35, 6),
                                            30.0, std::move(groups)));
        }
    }
    return p;
}

RigPreset lotus() {
    // A seed ring, 8 inner petals and 8 outer petals between them; each petal runs out from its
    // base along one side and back along the other.
    RigPreset p{"Lotus",
                "A seed ring with 8 inner petals (60 px/m) and 8 larger outer petals between them "
                "(30 px/m), 3.2 m across. Groups: lotus, seed, inner, outer, even, odd, upper, "
                "lower.",
                {}};
    const Vec3 centre{0.0, 1.8, 0.0};
    p.strips.push_back(preset_strip("Seed", polygon(centre, 0.18, 16), 60.0, {"lotus", "seed"}));
    const auto petals = [&](const char *layer, double offset, double from_m, double to_m,
                            double width_m, double pixels_per_m) {
        for (int i = 0; i < 8; ++i) {
            const double a = 0.5 * kPi + 2.0 * kPi * (i + offset) / 8.0;
            std::vector<std::string> groups{"lotus", layer, i % 2 == 0 ? "even" : "odd"};
            const double up = std::sin(a);
            if (up > 1e-9) {
                groups.emplace_back("upper");
            } else if (up < -1e-9) {
                groups.emplace_back("lower");
            }
            std::string name = layer;
            name[0] = static_cast<char>(name[0] - 'a' + 'A');
            p.strips.push_back(
                preset_strip(name + " petal " + std::to_string(i + 1),
                             lens_loop(polar(centre, from_m, a), polar(centre, to_m, a), width_m),
                             pixels_per_m, std::move(groups)));
        }
    };
    petals("inner", 0.0, 0.24, 0.9, 0.36, 60.0);
    petals("outer", 0.5, 0.55, 1.6, 0.62, 30.0);
    return p;
}

RigPreset third_eye() {
    // An eye in a pyramid: lids, iris, a slit pupil, lashes, and the pyramid behind.
    RigPreset p{"Third eye",
                "A 3 m eye (lids left to right, iris, slit pupil, 9 lashes) inside a 4.6 m "
                "pyramid. Groups: eye, lids, iris, pupil, lashes, pyramid.",
                {}};
    const Vec3 centre{0.0, 1.9, 0.0};
    const Vec3 left{-1.5, 1.9, 0.0};
    const Vec3 right{1.5, 1.9, 0.0};
    p.strips.push_back(
        preset_strip("Upper lid", lens_side(left, right, 1.3), 60.0, {"eye", "lids"}));
    p.strips.push_back(
        preset_strip("Lower lid", reversed(lens_side(right, left, 1.3)), 60.0, {"eye", "lids"}));
    p.strips.push_back(preset_strip("Iris", polygon(centre, 0.5, 32), 60.0, {"eye", "iris"}));
    p.strips.push_back(preset_strip("Pupil", lens_loop({0.0, 1.5, 0.0}, {0.0, 2.3, 0.0}, 0.22),
                                    60.0, {"eye", "pupil"}));
    for (int i = 0; i < 9; ++i) {
        const double a = (35.0 + 110.0 * i / 8.0) * kPi / 180.0;
        p.strips.push_back(preset_strip("Lash " + std::to_string(i + 1),
                                        {polar(centre, 0.85, a), polar(centre, 1.2, a)}, 60.0,
                                        {"eye", "lashes"}));
    }
    const double apex_m = 0.3 + 4.6 * std::sqrt(3.0) / 2.0;
    p.strips.push_back(preset_strip(
        "Pyramid", {{-2.3, 0.3, 0.0}, {2.3, 0.3, 0.0}, {0.0, apex_m, 0.0}, {-2.3, 0.3, 0.0}}, 30.0,
        {"pyramid"}));
    return p;
}

RigPreset string_art_star() {
    // A twelve-point star {12/5}: each chord runs from point i to point i + 5, so a chase along
    // the strips turns the whole star.
    RigPreset p{"String-art star",
                "Twelve chords of a 3 m circle joining every fifth point (a {12/5} star), inside a "
                "ring, 30 px/m. Groups: string, chords, ring, even, odd.",
                {}};
    const Vec3 centre{0.0, 1.9, 0.0};
    for (int i = 0; i < 12; ++i) {
        const double from = 0.5 * kPi + 2.0 * kPi * i / 12.0;
        const double to = 0.5 * kPi + 2.0 * kPi * (i + 5) / 12.0;
        p.strips.push_back(preset_strip("Chord " + std::to_string(i + 1),
                                        {polar(centre, 1.5, from), polar(centre, 1.5, to)}, 30.0,
                                        {"string", "chords", i % 2 == 0 ? "even" : "odd"}));
    }
    p.strips.push_back(preset_strip("Ring", polygon(centre, 1.6, 48), 30.0, {"string", "ring"}));
    return p;
}

RigPreset alien_ribcage() {
    // A spine with 7 pairs of arched ribs, widest in the middle, each from the spine outwards.
    RigPreset p{"Alien ribcage",
                "A 2.8 m spine with 7 pairs of arched ribs running outwards, widest in the middle. "
                "Groups: ribcage, spine, ribs, upper, lower, even, odd.",
                {}};
    p.strips.push_back(
        preset_strip("Spine", {{0.0, 0.4, 0.0}, {0.0, 3.2, 0.0}}, 60.0, {"ribcage", "spine"}));
    for (int k = 0; k < 7; ++k) {
        const double y = 0.8 + 2.0 * k / 6.0;
        const double reach_m = 0.45 + 1.35 * std::sin(kPi * (k + 0.5) / 7.0);
        for (const double side : {-1.0, 1.0}) {
            std::vector<std::string> groups{"ribcage", "ribs", k % 2 == 0 ? "even" : "odd"};
            if (k > 3) {
                groups.emplace_back("upper");
            } else if (k < 3) {
                groups.emplace_back("lower");
            }
            p.strips.push_back(preset_strip(
                std::string(side < 0.0 ? "Left" : "Right") + " rib " + std::to_string(k + 1),
                bezier({0.0, y, 0.0}, {side * 0.7 * reach_m, y + 0.3, 0.0},
                       {side * reach_m, y - 0.45, 0.0}),
                60.0, std::move(groups)));
        }
    }
    return p;
}

void add_branches(RigPreset &p, Vec3 from, double angle_rad, double length_m, int depth) {
    const Vec3 to{from.x + length_m * std::cos(angle_rad), from.y + length_m * std::sin(angle_rad),
                  from.z};
    const char *level = depth == 0 ? "trunk" : depth <= 2 ? "branches" : "twigs";
    const char *name = depth == 0 ? "Trunk" : depth <= 2 ? "Branch" : "Twig";
    p.strips.push_back(preset_strip(std::string(name) + " " + std::to_string(p.strips.size() + 1),
                                    {from, to}, 60.0, {"tree", level}));
    if (depth < 4) {
        for (const double side : {-1.0, 1.0}) {
            add_branches(p, to, angle_rad + side * 27.0 * kPi / 180.0, 0.72 * length_m, depth + 1);
        }
    }
}

RigPreset fractal_tree() {
    // A trunk splitting in two four times; every strip runs from the root towards the tips.
    RigPreset p{"Fractal tree",
                "A 1.2 m trunk splitting in two four times (31 strips), every strip running from "
                "the root to the tips. Groups: tree, trunk, branches, twigs.",
                {}};
    add_branches(p, {0.0, 0.0, 0.0}, 0.5 * kPi, 1.2, 0);
    return p;
}

RigPreset vortex() {
    // Six logarithmic spiral arms, each from the centre outwards over 0.9 of a turn.
    RigPreset p{"Vortex",
                "Six logarithmic spiral arms, each from the centre outwards over 0.9 of a turn, "
                "3.2 m across. Groups: vortex, arms, even, odd.",
                {}};
    const Vec3 centre{0.0, 1.9, 0.0};
    constexpr double kInnerM = 0.15;
    constexpr double kOuterM = 1.6;
    const double turn_rad = 0.9 * 2.0 * kPi;
    const double growth = std::log(kOuterM / kInnerM) / turn_rad;
    for (int k = 0; k < 6; ++k) {
        const double offset = 0.5 * kPi + 2.0 * kPi * k / 6.0;
        std::vector<Vec3> points;
        for (int i = 0; i <= 48; ++i) {
            const double t = turn_rad * i / 48.0;
            points.push_back(polar(centre, kInnerM * std::exp(growth * t), offset + t));
        }
        p.strips.push_back(preset_strip("Arm " + std::to_string(k + 1), std::move(points), 60.0,
                                        {"vortex", "arms", k % 2 == 0 ? "even" : "odd"}));
    }
    return p;
}

RigPreset hive_temple() {
    // A temple front: a double lens portal, honeycomb wings at its shoulders, rays crowning it.
    RigPreset p{"Hive temple",
                "A temple front: a double lens portal (3 m), a honeycomb wing of 4 cells either "
                "side and 7 rays crowning the top. Groups: temple, portal, outer, inner, comb, "
                "crown.",
                {}};
    for (const auto &[layer, height_m, width_m] :
         {std::tuple{"outer", 3.0, 1.9}, std::tuple{"inner", 2.3, 1.25}}) {
        const Vec3 bottom{0.0, 1.75 - 0.5 * height_m, 0.0};
        const Vec3 top{0.0, 1.75 + 0.5 * height_m, 0.0};
        std::string name = layer;
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
        p.strips.push_back(preset_strip(name + " portal left", lens_side(bottom, top, width_m),
                                        60.0, {"temple", "portal", layer}));
        p.strips.push_back(preset_strip(name + " portal right",
                                        reversed(lens_side(top, bottom, width_m)), 60.0,
                                        {"temple", "portal", layer}));
    }
    const int cells[4][2] = {{0, 0}, {1, 0}, {0, 1}, {-1, 1}};
    for (const double side : {-1.0, 1.0}) {
        for (int i = 0; i < 4; ++i) {
            Vec3 centre = honeycomb_cell({1.6, 1.55, 0.0}, 0.34, cells[i][0], cells[i][1]);
            centre.x *= side;
            p.strips.push_back(preset_strip(std::string(side < 0.0 ? "Left" : "Right") + " cell " +
                                                std::to_string(i + 1),
                                            polygon(centre, 0.34, 6), 30.0, {"temple", "comb"}));
        }
    }
    const Vec3 crown{0.0, 3.25, 0.0};
    for (int i = 0; i < 7; ++i) {
        const double a = (30.0 + 120.0 * i / 6.0) * kPi / 180.0;
        p.strips.push_back(preset_strip("Ray " + std::to_string(i + 1),
                                        {polar(crown, 0.2, a), polar(crown, 0.75, a)}, 60.0,
                                        {"temple", "crown"}));
    }
    return p;
}

} // namespace

std::vector<RigPreset> builtin_rig_presets() {
    return {dj_booth_chevron(),
            triple_arch_tunnel(),
            sunburst(),
            hexagon_mandala(),
            goalpost_truss(),
            triangle_row(),
            pixel_curtain(),
            pixel_wall(),
            hive_temple(),
            hive_portal(),
            honeycomb(),
            lotus(),
            third_eye(),
            string_art_star(),
            alien_ribcage(),
            fractal_tree(),
            vortex()};
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
