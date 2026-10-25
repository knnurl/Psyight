#include "io/dxf_rig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCurveStepM = 0.005; // As the SVG reader's.
constexpr int kSegmentsPerTurn = 64;
constexpr int kMaxSegments = 1024;

struct Pair {
    int code = 0;
    std::string value;
};

std::string trimmed(std::string_view s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const size_t last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

std::string lower(std::string s) {
    for (char &ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

// The file as group code and value pairs (a code line, then a value line).
std::vector<Pair> read_pairs(std::string_view text) {
    std::vector<Pair> pairs;
    size_t pos = 0;
    const auto next_line = [&](std::string &line) {
        if (pos >= text.size()) {
            return false;
        }
        const size_t end = text.find('\n', pos);
        line = trimmed(
            text.substr(pos, end == std::string_view::npos ? text.size() - pos : end - pos));
        pos = end == std::string_view::npos ? text.size() : end + 1;
        return true;
    };
    std::string code;
    std::string value;
    while (next_line(code) && next_line(value)) {
        char *end = nullptr;
        const long c = std::strtol(code.c_str(), &end, 10);
        if (end == code.c_str()) {
            break; // Not a group code: the rest is not DXF.
        }
        pairs.push_back({static_cast<int>(c), value});
    }
    return pairs;
}

// One entity: its type and its pairs up to the next code 0.
struct Entity {
    std::string type;
    std::vector<Pair> pairs;

    double number(int code, double fallback = 0.0) const {
        for (const Pair &p : pairs) {
            if (p.code == code) {
                return std::strtod(p.value.c_str(), nullptr);
            }
        }
        return fallback;
    }
    std::string text(int code) const {
        for (const Pair &p : pairs) {
            if (p.code == code) {
                return p.value;
            }
        }
        return {};
    }
    std::vector<double> numbers(int code) const {
        std::vector<double> out;
        for (const Pair &p : pairs) {
            if (p.code == code) {
                out.push_back(std::strtod(p.value.c_str(), nullptr));
            }
        }
        return out;
    }
};

double metres_per_unit(int insunits) {
    switch (insunits) {
    case 1:
        return 0.0254; // Inches.
    case 2:
        return 0.3048; // Feet.
    case 5:
        return 0.01; // Centimetres.
    case 6:
        return 1.0; // Metres.
    default:
        return 0.001; // Millimetres, and unitless.
    }
}

int segments_for(double length_m, double turns) {
    const int by_length = static_cast<int>(std::ceil(length_m / kCurveStepM));
    const int by_angle = static_cast<int>(std::ceil(turns * kSegmentsPerTurn));
    return std::clamp(std::max(by_length, by_angle), 1, kMaxSegments);
}

struct Point {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0; // Depth: the drawing's own z (towards the viewer).
};

// Points from a0 to a0 + sweep (radians, anticlockwise when positive) on a circle, the first
// point excluded.
void add_arc(std::vector<Point> &out, Point centre, double radius, double a0, double sweep) {
    const int n = segments_for(std::abs(sweep) * radius, std::abs(sweep) / (2.0 * kPi));
    for (int i = 1; i <= n; ++i) {
        const double a = a0 + sweep * i / n;
        out.push_back({centre.x + radius * std::cos(a), centre.y + radius * std::sin(a), centre.z});
    }
}

// A polyline segment from p0 to p1 with a bulge (the tangent of a quarter of its arc's angle;
// positive: anticlockwise), p0 excluded.
void add_bulge(std::vector<Point> &out, Point p0, Point p1, double bulge) {
    const double dx = p1.x - p0.x;
    const double dy = p1.y - p0.y;
    const double chord = std::hypot(dx, dy);
    if (std::abs(bulge) < 1e-9 || chord < 1e-12) {
        out.push_back(p1);
        return;
    }
    const double sweep = 4.0 * std::atan(bulge);
    const double offset = chord * (1.0 - bulge * bulge) / (4.0 * bulge); // Along the left normal.
    const Point centre{0.5 * (p0.x + p1.x) - dy / chord * offset,
                       0.5 * (p0.y + p1.y) + dx / chord * offset, p0.z};
    const double radius = std::hypot(p0.x - centre.x, p0.y - centre.y);
    add_arc(out, centre, radius, std::atan2(p0.y - centre.y, p0.x - centre.x), sweep);
    out.back() = p1; // Exactly on the vertex.
}

// A non-uniform rational B-spline evaluated at t (de Boor).
Point de_boor(int degree, const std::vector<double> &knots, const std::vector<Point> &control,
              const std::vector<double> &weights, double t) {
    const int n = static_cast<int>(control.size());
    int span = degree;
    while (span < n - 1 && t >= knots[static_cast<size_t>(span + 1)]) {
        ++span;
    }
    std::vector<double> x(static_cast<size_t>(degree + 1));
    std::vector<double> y(x.size());
    std::vector<double> z(x.size());
    std::vector<double> w(x.size());
    for (int j = 0; j <= degree; ++j) {
        const size_t i = static_cast<size_t>(span - degree + j);
        w[static_cast<size_t>(j)] = weights.size() == control.size() ? weights[i] : 1.0;
        x[static_cast<size_t>(j)] = control[i].x * w[static_cast<size_t>(j)];
        y[static_cast<size_t>(j)] = control[i].y * w[static_cast<size_t>(j)];
        z[static_cast<size_t>(j)] = control[i].z * w[static_cast<size_t>(j)];
    }
    for (int r = 1; r <= degree; ++r) {
        for (int j = degree; j >= r; --j) {
            const size_t i = static_cast<size_t>(span - degree + j);
            const double lo = knots[i];
            const double hi = knots[i + static_cast<size_t>(degree - r + 1)];
            const double a = hi > lo ? (t - lo) / (hi - lo) : 0.0;
            const auto k = static_cast<size_t>(j);
            x[k] = (1.0 - a) * x[k - 1] + a * x[k];
            y[k] = (1.0 - a) * y[k - 1] + a * y[k];
            z[k] = (1.0 - a) * z[k - 1] + a * z[k];
            w[k] = (1.0 - a) * w[k - 1] + a * w[k];
        }
    }
    const auto d = static_cast<size_t>(degree);
    return w[d] != 0.0 ? Point{x[d] / w[d], y[d] / w[d], z[d] / w[d]} : Point{x[d], y[d], z[d]};
}

double length_of(const std::vector<Point> &points) {
    double length = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        length += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y,
                             points[i].z - points[i - 1].z);
    }
    return length;
}

// A shape read from the file, in metres. Arcs and circles keep their centre for the direction
// pass; a circle's points are made there.
struct Read {
    std::string type;
    std::string layer;
    std::vector<Point> points;
    bool round = false; // An arc or a circle (stored anticlockwise).
    bool circle = false;
    Point centre;
    double radius = 0.0;
};

} // namespace

Result<SvgRig> rig_from_dxf(std::string_view dxf, const SvgRigOptions &options) {
    if (dxf.substr(0, 18) == "AutoCAD Binary DXF") {
        return Error{"A binary DXF: save it as text (ASCII) DXF."};
    }
    const std::vector<Pair> pairs = read_pairs(dxf);
    double unit = metres_per_unit(4);
    std::set<std::string> hidden_layers;
    std::vector<Entity> entities;
    bool found_entities = false;
    std::string section;
    std::string table_entry;
    std::string layer_name;
    int layer_flags = 0;
    int layer_colour = 0;
    const auto end_layer = [&] {
        if (table_entry == "LAYER" && !layer_name.empty() &&
            ((layer_flags & 1) != 0 || layer_colour < 0)) {
            hidden_layers.insert(layer_name); // Frozen, or off (a negative colour).
        }
        table_entry.clear();
        layer_name.clear();
        layer_flags = 0;
        layer_colour = 0;
    };
    for (size_t i = 0; i < pairs.size(); ++i) {
        const Pair &p = pairs[i];
        if (p.code == 0 && p.value == "SECTION" && i + 1 < pairs.size() && pairs[i + 1].code == 2) {
            section = pairs[++i].value;
            found_entities = found_entities || section == "ENTITIES";
            continue;
        }
        if (p.code == 0 && p.value == "ENDSEC") {
            end_layer();
            section.clear();
            continue;
        }
        if (section == "HEADER") {
            if (p.code == 9 && p.value == "$INSUNITS" && i + 1 < pairs.size()) {
                unit = metres_per_unit(std::atoi(pairs[i + 1].value.c_str()));
            }
        } else if (section == "TABLES") {
            if (p.code == 0) {
                end_layer();
                table_entry = p.value;
            } else if (table_entry == "LAYER") {
                if (p.code == 2) {
                    layer_name = p.value;
                } else if (p.code == 70) {
                    layer_flags = std::atoi(p.value.c_str());
                } else if (p.code == 62) {
                    layer_colour = std::atoi(p.value.c_str());
                }
            }
        } else if (section == "ENTITIES") {
            if (p.code == 0) {
                entities.push_back({p.value, {}});
            } else if (!entities.empty()) {
                entities.back().pairs.push_back(p);
            }
        }
    }
    if (!found_entities) {
        return Error{"Not a DXF drawing (no ENTITIES section)."};
    }

    std::vector<Read> shapes;
    const auto visible = [&](const Entity &e) { return !hidden_layers.contains(e.text(8)); };
    // A point in metres; mirrored (an extrusion along -Z): x and z turn over.
    const auto at = [&](double x, double y, double z, bool mirrored) {
        return Point{(mirrored ? -x : x) * unit, y * unit, (mirrored ? -z : z) * unit};
    };
    for (size_t k = 0; k < entities.size(); ++k) {
        const Entity &e = entities[k];
        // An extrusion pointing away (-Z, a sketch seen from below) mirrors x in the entity's
        // own coordinates (ARC, CIRCLE, LWPOLYLINE, POLYLINE).
        const bool mirrored = e.number(230, 1.0) < 0.0;
        Read shape{e.type, e.text(8), {}};
        if (e.type == "LINE") {
            shape.points = {at(e.number(10), e.number(20), e.number(30), false),
                            at(e.number(11), e.number(21), e.number(31), false)};
        } else if (e.type == "LWPOLYLINE") {
            // Vertices in order: each 10 starts one; its 20 and 42 (bulge) follow.
            std::vector<Point> vertices;
            std::vector<double> bulges;
            for (const Pair &p : e.pairs) {
                if (p.code == 10) {
                    // A lightweight polyline lies in its plane at its elevation (code 38).
                    vertices.push_back({std::strtod(p.value.c_str(), nullptr), 0.0, e.number(38)});
                    bulges.push_back(0.0);
                } else if (p.code == 20 && !vertices.empty()) {
                    vertices.back().y = std::strtod(p.value.c_str(), nullptr);
                } else if (p.code == 42 && !bulges.empty()) {
                    bulges.back() = std::strtod(p.value.c_str(), nullptr);
                }
            }
            const bool closed = (static_cast<int>(e.number(70)) & 1) != 0;
            for (size_t v = 0; v < vertices.size(); ++v) {
                const Point here = at(vertices[v].x, vertices[v].y, vertices[v].z, mirrored);
                if (v == 0) {
                    shape.points.push_back(here);
                }
                const bool last = v + 1 == vertices.size();
                if (last && !closed) {
                    break;
                }
                const Point next = at(vertices[last ? 0 : v + 1].x, vertices[last ? 0 : v + 1].y,
                                      vertices[last ? 0 : v + 1].z, mirrored);
                add_bulge(shape.points, here, next, mirrored ? -bulges[v] : bulges[v]);
            }
        } else if (e.type == "POLYLINE") {
            const int flags = static_cast<int>(e.number(70));
            std::vector<Point> vertices;
            std::vector<double> bulges;
            for (++k; k < entities.size() && entities[k].type == "VERTEX"; ++k) {
                // 3D polylines (flag 8) give each vertex its z; 2D ones lie at the elevation
                // in the polyline's own point.
                vertices.push_back({entities[k].number(10), entities[k].number(20),
                                    (flags & 8) != 0 ? entities[k].number(30) : e.number(30)});
                bulges.push_back(entities[k].number(42));
            }
            if ((flags & (16 | 64)) != 0) {
                continue; // A 3D mesh or polyface.
            }
            const bool closed = (flags & 1) != 0;
            for (size_t v = 0; v < vertices.size(); ++v) {
                const Point here = at(vertices[v].x, vertices[v].y, vertices[v].z, mirrored);
                if (v == 0) {
                    shape.points.push_back(here);
                }
                const bool last = v + 1 == vertices.size();
                if (last && !closed) {
                    break;
                }
                const Point next = at(vertices[last ? 0 : v + 1].x, vertices[last ? 0 : v + 1].y,
                                      vertices[last ? 0 : v + 1].z, mirrored);
                add_bulge(shape.points, here, next, mirrored ? -bulges[v] : bulges[v]);
            }
            if (k < entities.size() && entities[k].type != "SEQEND") {
                --k; // No SEQEND: the next entity is read as usual.
            }
        } else if (e.type == "ARC" || e.type == "CIRCLE") {
            shape.round = true;
            shape.circle = e.type == "CIRCLE";
            shape.centre = at(e.number(10), e.number(20), e.number(30), mirrored);
            shape.radius = e.number(40) * unit;
            if (!shape.circle) {
                double a0 = e.number(50) * kPi / 180.0;
                double a1 = e.number(51) * kPi / 180.0;
                while (a1 <= a0) {
                    a1 += 2.0 * kPi;
                }
                if (mirrored) { // Mirrored in x: the angles turn the other way.
                    a0 = kPi - a0;
                    a1 = kPi - a1;
                }
                shape.points.push_back({shape.centre.x + shape.radius * std::cos(a0),
                                        shape.centre.y + shape.radius * std::sin(a0),
                                        shape.centre.z});
                add_arc(shape.points, shape.centre, shape.radius, a0, a1 - a0);
            }
        } else if (e.type == "ELLIPSE") {
            const Point c = at(e.number(10), e.number(20), e.number(30), false);
            const Point major{e.number(11) * unit, e.number(21) * unit};
            const double ratio = e.number(40, 1.0);
            const double side = e.number(230, 1.0) < 0.0 ? -1.0 : 1.0;
            const Point minor{-major.y * ratio * side, major.x * ratio * side};
            const double t0 = e.number(41, 0.0);
            double t1 = e.number(42, 2.0 * kPi);
            while (t1 <= t0) {
                t1 += 2.0 * kPi;
            }
            const double size =
                std::max(std::hypot(major.x, major.y), std::hypot(minor.x, minor.y));
            const int n = segments_for((t1 - t0) * size, (t1 - t0) / (2.0 * kPi));
            for (int i = 0; i <= n; ++i) {
                const double t = t0 + (t1 - t0) * i / n;
                shape.points.push_back({c.x + major.x * std::cos(t) + minor.x * std::sin(t),
                                        c.y + major.y * std::cos(t) + minor.y * std::sin(t), c.z});
            }
        } else if (e.type == "SPLINE") {
            const int degree = static_cast<int>(e.number(71, 3.0));
            const std::vector<double> knots = e.numbers(40);
            const std::vector<double> weights = e.numbers(41);
            const std::vector<double> cx = e.numbers(10);
            const std::vector<double> cy = e.numbers(20);
            const std::vector<double> cz = e.numbers(30);
            std::vector<Point> control;
            for (size_t i = 0; i < std::min(cx.size(), cy.size()); ++i) {
                control.push_back(at(cx[i], cy[i], i < cz.size() ? cz[i] : 0.0, false));
            }
            const size_t n = control.size();
            if (degree >= 1 && n > static_cast<size_t>(degree) &&
                knots.size() == n + static_cast<size_t>(degree) + 1) {
                const double t0 = knots[static_cast<size_t>(degree)];
                const double t1 = knots[n];
                const int steps = segments_for(length_of(control), 0.0) * 2;
                for (int i = 0; i <= steps; ++i) {
                    shape.points.push_back(
                        de_boor(degree, knots, control, weights, t0 + (t1 - t0) * i / steps));
                }
            } else { // No usable control polygon: a line through the fit points.
                const std::vector<double> fx = e.numbers(11);
                const std::vector<double> fy = e.numbers(21);
                const std::vector<double> fz = e.numbers(31);
                for (size_t i = 0; i < std::min(fx.size(), fy.size()); ++i) {
                    shape.points.push_back(at(fx[i], fy[i], i < fz.size() ? fz[i] : 0.0, false));
                }
            }
        } else {
            continue; // Text, hatches, blocks (INSERT), points, dimensions.
        }
        if (visible(e)) {
            shapes.push_back(std::move(shape));
        }
    }

    // Arcs and circles: anticlockwise as stored on the right of the middle, reversed on the
    // left; a circle starts at its point nearest the middle (a centred one at its bottom).
    double min_x = 0.0;
    double max_x = 0.0;
    bool any = false;
    for (const Read &s : shapes) {
        const auto extend = [&](double x) {
            min_x = any ? std::min(min_x, x) : x;
            max_x = any ? std::max(max_x, x) : x;
            any = true;
        };
        if (s.circle) {
            extend(s.centre.x - s.radius);
            extend(s.centre.x + s.radius);
        }
        for (const Point &p : s.points) {
            extend(p.x);
        }
    }
    const double middle = 0.5 * (min_x + max_x);
    const double tolerance = 1e-6 + 1e-3 * (max_x - min_x);
    for (Read &s : shapes) {
        if (!s.round) {
            continue;
        }
        const bool left = s.centre.x < middle - tolerance;
        const bool right = s.centre.x > middle + tolerance;
        if (s.circle) {
            const double start = left ? 0.0 : right ? kPi : -0.5 * kPi;
            s.points = {{s.centre.x + s.radius * std::cos(start),
                         s.centre.y + s.radius * std::sin(start), s.centre.z}};
            add_arc(s.points, s.centre, s.radius, start, left ? -2.0 * kPi : 2.0 * kPi);
        } else if (left) {
            std::reverse(s.points.begin(), s.points.end());
        }
    }

    std::vector<DrawingShape> drawn;
    std::map<std::string, int> numbers;
    for (Read &s : shapes) {
        DrawingShape d;
        const bool default_layer = s.layer.empty() || s.layer == "0";
        std::string base = default_layer ? lower(s.type) : s.layer;
        base[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(base[0])));
        d.name = base + " " + std::to_string(++numbers[base]);
        if (!default_layer) {
            d.groups.push_back(lower(trimmed(s.layer)));
        }
        for (const Point &p : s.points) {
            d.points.push_back({p.x, p.y, p.z});
        }
        drawn.push_back(std::move(d));
    }
    return rig_from_shapes(std::move(drawn), options);
}

} // namespace psyight
