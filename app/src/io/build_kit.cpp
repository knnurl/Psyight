#include "io/build_kit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <utility>

namespace psyight {

namespace {

constexpr double kFineCellMm = 0.2;
constexpr int kMaxCells = 4000;        // Across, at most: coarser cells for big rigs.
constexpr double kSimplifyCells = 1.5; // Traced outlines straightened to within this.
constexpr double kBoltWoodMm = 3.0;    // Wood around a bolt hole, at least.
constexpr double kSecondBoltSpanMm = 120.0;
constexpr double kNumberMaxMm = 8.0;
constexpr double kMarginExtraMm = 5.0; // Grid beyond the panel.

struct P {
    double x = 0.0;
    double y = 0.0;
};

using Loop = std::vector<P>;

// A boolean grid over the drawing, cells of `cell` mm from (x0, y0).
struct Grid {
    double x0 = 0.0;
    double y0 = 0.0;
    double cell = 1.0;
    int nx = 0;
    int ny = 0;
    std::vector<std::uint8_t> v;

    std::uint8_t at(int i, int j) const {
        return i >= 0 && j >= 0 && i < nx && j < ny ? v[static_cast<size_t>(j) * nx + i] : 0;
    }
    std::uint8_t &ref(int i, int j) { return v[static_cast<size_t>(j) * nx + i]; }
    P centre(int i, int j) const { return {x0 + (i + 0.5) * cell, y0 + (j + 0.5) * cell}; }
};

Grid empty_like(const Grid &g) {
    Grid out = g;
    std::fill(out.v.begin(), out.v.end(), std::uint8_t{0});
    return out;
}

// Every cell whose centre lies within r of a segment of the lines (round ends and corners).
Grid capsules(const Grid &shape, const std::vector<std::vector<P>> &lines, double r) {
    Grid g = empty_like(shape);
    for (const std::vector<P> &line : lines) {
        for (size_t k = 0; k + 1 < line.size(); ++k) {
            const P a = line[k];
            const P b = line[k + 1];
            const int i0 =
                std::max(0, static_cast<int>((std::min(a.x, b.x) - r - g.x0) / g.cell) - 1);
            const int i1 =
                std::min(g.nx - 1, static_cast<int>((std::max(a.x, b.x) + r - g.x0) / g.cell) + 1);
            const int j0 =
                std::max(0, static_cast<int>((std::min(a.y, b.y) - r - g.y0) / g.cell) - 1);
            const int j1 =
                std::min(g.ny - 1, static_cast<int>((std::max(a.y, b.y) + r - g.y0) / g.cell) + 1);
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double len2 = dx * dx + dy * dy;
            for (int j = j0; j <= j1; ++j) {
                for (int i = i0; i <= i1; ++i) {
                    const P c = g.centre(i, j);
                    const double t =
                        len2 > 0.0
                            ? std::clamp(((c.x - a.x) * dx + (c.y - a.y) * dy) / len2, 0.0, 1.0)
                            : 0.0;
                    const double ex = a.x + t * dx - c.x;
                    const double ey = a.y + t * dy - c.y;
                    if (ex * ex + ey * ey <= r * r) {
                        g.ref(i, j) = 1;
                    }
                }
            }
        }
    }
    return g;
}

// The shape with its holes filled: everything not reachable from the border outside it.
Grid filled(const Grid &g) {
    std::vector<std::uint8_t> outside(g.v.size(), 0);
    std::vector<std::pair<int, int>> stack;
    const auto push = [&](int i, int j) {
        if (i < 0 || j < 0 || i >= g.nx || j >= g.ny) {
            return;
        }
        const size_t k = static_cast<size_t>(j) * g.nx + i;
        if (!g.v[k] && !outside[k]) {
            outside[k] = 1;
            stack.emplace_back(i, j);
        }
    };
    for (int i = 0; i < g.nx; ++i) {
        push(i, 0);
        push(i, g.ny - 1);
    }
    for (int j = 0; j < g.ny; ++j) {
        push(0, j);
        push(g.nx - 1, j);
    }
    while (!stack.empty()) {
        const auto [i, j] = stack.back();
        stack.pop_back();
        push(i + 1, j);
        push(i - 1, j);
        push(i, j + 1);
        push(i, j - 1);
    }
    Grid out = g;
    for (size_t k = 0; k < out.v.size(); ++k) {
        out.v[k] = outside[k] ? 0 : 1;
    }
    return out;
}

Grid minus(Grid a, const Grid &b) {
    for (size_t k = 0; k < a.v.size(); ++k) {
        a.v[k] = a.v[k] && !b.v[k] ? 1 : 0;
    }
    return a;
}

// Components (4-connected): a label per cell (-1 outside), and the count.
std::vector<int> components(const Grid &g, int &count) {
    std::vector<int> label(g.v.size(), -1);
    count = 0;
    std::vector<std::pair<int, int>> stack;
    for (int j = 0; j < g.ny; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            const size_t k = static_cast<size_t>(j) * g.nx + i;
            if (!g.v[k] || label[k] >= 0) {
                continue;
            }
            label[k] = count;
            stack.emplace_back(i, j);
            while (!stack.empty()) {
                const auto [ci, cj] = stack.back();
                stack.pop_back();
                const std::array<std::pair<int, int>, 4> next = {
                    {{ci + 1, cj}, {ci - 1, cj}, {ci, cj + 1}, {ci, cj - 1}}};
                for (const auto [ni, nj] : next) {
                    if (ni < 0 || nj < 0 || ni >= g.nx || nj >= g.ny) {
                        continue;
                    }
                    const size_t nk = static_cast<size_t>(nj) * g.nx + ni;
                    if (g.v[nk] && label[nk] < 0) {
                        label[nk] = count;
                        stack.emplace_back(ni, nj);
                    }
                }
            }
            ++count;
        }
    }
    return label;
}

// Distance (mm, chamfer 3-4) from each cell of the shape to the nearest cell outside it.
std::vector<double> distances(const Grid &g) {
    const int big = 1 << 28;
    std::vector<int> d(g.v.size());
    for (size_t k = 0; k < d.size(); ++k) {
        d[k] = g.v[k] ? big : 0;
    }
    const auto at = [&](int i, int j) {
        return i < 0 || j < 0 || i >= g.nx || j >= g.ny ? 0 : d[static_cast<size_t>(j) * g.nx + i];
    };
    for (int j = 0; j < g.ny; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            int &v = d[static_cast<size_t>(j) * g.nx + i];
            if (v == 0) {
                continue;
            }
            v = std::min({v, at(i - 1, j) + 3, at(i, j - 1) + 3, at(i - 1, j - 1) + 4,
                          at(i + 1, j - 1) + 4});
        }
    }
    for (int j = g.ny - 1; j >= 0; --j) {
        for (int i = g.nx - 1; i >= 0; --i) {
            int &v = d[static_cast<size_t>(j) * g.nx + i];
            if (v == 0) {
                continue;
            }
            v = std::min({v, at(i + 1, j) + 3, at(i, j + 1) + 3, at(i + 1, j + 1) + 4,
                          at(i - 1, j + 1) + 4});
        }
    }
    std::vector<double> mm(d.size());
    for (size_t k = 0; k < d.size(); ++k) {
        mm[k] = d[k] / 3.0 * g.cell;
    }
    return mm;
}

// The shape's outlines (outer edges and holes) by marching squares on the cell centres.
std::vector<Loop> traced(const Grid &g) {
    // Edge midpoints keyed on a doubled grid: x key 2i (on corner column i) or 2i + 1 (between).
    using Key = std::pair<int, int>;
    std::vector<std::pair<Key, Key>> segments;
    for (int j = -1; j < g.ny; ++j) {
        for (int i = -1; i < g.nx; ++i) {
            const int bl = g.at(i, j);
            const int br = g.at(i + 1, j);
            const int tr = g.at(i + 1, j + 1);
            const int tl = g.at(i, j + 1);
            const int code = bl | br << 1 | tr << 2 | tl << 3;
            const Key B{2 * i + 1, 2 * j};
            const Key R{2 * i + 2, 2 * j + 1};
            const Key T{2 * i + 1, 2 * j + 2};
            const Key L{2 * i, 2 * j + 1};
            switch (code) {
            case 1:
            case 14:
                segments.push_back({L, B});
                break;
            case 2:
            case 13:
                segments.push_back({B, R});
                break;
            case 3:
            case 12:
                segments.push_back({L, R});
                break;
            case 4:
            case 11:
                segments.push_back({R, T});
                break;
            case 6:
            case 9:
                segments.push_back({B, T});
                break;
            case 7:
            case 8:
                segments.push_back({L, T});
                break;
            case 5:
                segments.push_back({L, B});
                segments.push_back({R, T});
                break;
            case 10:
                segments.push_back({B, R});
                segments.push_back({T, L});
                break;
            default:
                break;
            }
        }
    }
    std::map<Key, std::vector<size_t>> ends;
    for (size_t s = 0; s < segments.size(); ++s) {
        ends[segments[s].first].push_back(s);
        ends[segments[s].second].push_back(s);
    }
    std::vector<bool> used(segments.size(), false);
    const auto point = [&](const Key &k) {
        return P{g.x0 + (k.first + 1) * 0.5 * g.cell, g.y0 + (k.second + 1) * 0.5 * g.cell};
    };
    std::vector<Loop> loops;
    for (size_t s = 0; s < segments.size(); ++s) {
        if (used[s]) {
            continue;
        }
        used[s] = true;
        Loop loop{point(segments[s].first)};
        Key at = segments[s].second;
        const Key start = segments[s].first;
        while (at != start) {
            loop.push_back(point(at));
            size_t next = segments.size();
            for (const size_t c : ends[at]) {
                if (!used[c]) {
                    next = c;
                    break;
                }
            }
            if (next == segments.size()) {
                break; // Open (should not happen on a padded grid).
            }
            used[next] = true;
            at = segments[next].first == at ? segments[next].second : segments[next].first;
        }
        loop.push_back(loop.front());
        loops.push_back(std::move(loop));
    }
    return loops;
}

double distance_to_segment(const P &p, const P &a, const P &b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    const double t =
        len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(a.x + t * dx - p.x, a.y + t * dy - p.y);
}

void douglas_peucker(const Loop &in, size_t a, size_t b, double tol, std::vector<bool> &keep) {
    double worst = -1.0;
    size_t at = a;
    for (size_t k = a + 1; k < b; ++k) {
        const double d = distance_to_segment(in[k], in[a], in[b]);
        if (d > worst) {
            worst = d;
            at = k;
        }
    }
    if (worst > tol) {
        keep[at] = true;
        douglas_peucker(in, a, at, tol, keep);
        douglas_peucker(in, at, b, tol, keep);
    }
}

// A closed loop straightened: split at its farthest point from the start, each half simplified.
Loop simplified(const Loop &loop, double tol) {
    if (loop.size() < 5) {
        return loop;
    }
    size_t far = 0;
    double far_d = -1.0;
    for (size_t k = 0; k < loop.size(); ++k) {
        const double d = std::hypot(loop[k].x - loop[0].x, loop[k].y - loop[0].y);
        if (d > far_d) {
            far_d = d;
            far = k;
        }
    }
    std::vector<bool> keep(loop.size(), false);
    keep[0] = keep[far] = keep[loop.size() - 1] = true;
    douglas_peucker(loop, 0, far, tol, keep);
    douglas_peucker(loop, far, loop.size() - 1, tol, keep);
    Loop out;
    for (size_t k = 0; k < loop.size(); ++k) {
        if (keep[k]) {
            out.push_back(loop[k]);
        }
    }
    return out;
}

std::vector<Loop> outlines(const Grid &g) {
    std::vector<Loop> out;
    for (const Loop &loop : traced(g)) {
        if (loop.size() >= 4) {
            out.push_back(simplified(loop, kSimplifyCells * g.cell));
        }
    }
    return out;
}

struct Circle {
    P centre;
    double radius = 0.0;
};

// The grid with every cell whose centre lies within a circle set.
Grid with_discs(Grid g, const std::vector<Circle> &discs) {
    for (const Circle &d : discs) {
        const int i0 = std::max(0, static_cast<int>((d.centre.x - d.radius - g.x0) / g.cell) - 1);
        const int i1 =
            std::min(g.nx - 1, static_cast<int>((d.centre.x + d.radius - g.x0) / g.cell) + 1);
        const int j0 = std::max(0, static_cast<int>((d.centre.y - d.radius - g.y0) / g.cell) - 1);
        const int j1 =
            std::min(g.ny - 1, static_cast<int>((d.centre.y + d.radius - g.y0) / g.cell) + 1);
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const P c = g.centre(i, j);
                if (std::hypot(c.x - d.centre.x, c.y - d.centre.y) <= d.radius) {
                    g.ref(i, j) = 1;
                }
            }
        }
    }
    return g;
}

// The distance from p to the nearest of the lines, and that nearest point.
double nearest_on_lines(const P &p, const std::vector<std::vector<P>> &lines, P &at) {
    double best = std::numeric_limits<double>::infinity();
    for (const std::vector<P> &line : lines) {
        for (size_t k = 0; k + 1 < line.size(); ++k) {
            const P a = line[k];
            const P b = line[k + 1];
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double len2 = dx * dx + dy * dy;
            const double t =
                len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0)
                           : 0.0;
            const P q{a.x + t * dx, a.y + t * dy};
            const double d = std::hypot(q.x - p.x, q.y - p.y);
            if (d < best) {
                best = d;
                at = q;
            }
        }
    }
    return best;
}

struct Drawing {
    std::vector<Loop> cut;
    std::vector<Circle> holes;
    std::vector<std::vector<P>> engrave; // Open or closed polylines.
};

void add_number(std::vector<std::vector<P>> &out, int number, P centre, double height) {
    const std::string text = std::to_string(number);
    const double w = 0.55 * height;
    const double advance = w + 0.3 * height;
    const double total = text.size() * advance - 0.3 * height;
    for (size_t k = 0; k < text.size(); ++k) {
        const double left = centre.x - 0.5 * total + k * advance;
        const double bottom = centre.y - 0.5 * height;
        for (const std::vector<Vec3> &stroke : digit_strokes(text[k] - '0')) {
            std::vector<P> line;
            for (const Vec3 &q : stroke) {
                line.push_back({left + q.x * w, bottom + q.y * height});
            }
            out.push_back(std::move(line));
        }
    }
}

std::string num(double v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", std::abs(v) < 5e-4 ? 0.0 : v);
    return text;
}

constexpr std::array<const char *, 7> kSvgColours = {"#000000", "#ff0000", "#00ff00", "#0000ff",
                                                     "#ffff00", "#00ffff", "#ff00ff"};
constexpr std::array<int, 7> kDxfColours = {7, 1, 3, 5, 2, 4, 6};

// An AutoCAD R12 text DXF in millimetres: layers with colours, then a part's outlines (closed
// 2D polylines and circles) and engraving at an elevation, the outlines extruded by a thickness
// (DXF entity thickness: 3D viewers show a sheet as a slab).
class DxfWriter {
  public:
    explicit DxfWriter(const std::vector<std::pair<std::string, ExportColour>> &layers) {
        for (const auto &[c, v] : std::vector<std::pair<int, std::string>>{
                 {0, "SECTION"},   {2, "HEADER"},     {9, "$ACADVER"}, {1, "AC1009"},
                 {9, "$INSUNITS"}, {70, "4"},         {0, "ENDSEC"},   {0, "SECTION"},
                 {2, "TABLES"},    {0, "TABLE"},      {2, "LTYPE"},    {70, "1"},
                 {0, "LTYPE"},     {2, "CONTINUOUS"}, {70, "0"},       {3, "Solid line"},
                 {72, "65"},       {73, "0"},         {40, "0.0"},     {0, "ENDTAB"},
                 {0, "TABLE"},     {2, "LAYER"}}) {
            pair(c, v);
        }
        pair(70, std::to_string(layers.size()));
        for (const auto &[name, colour] : layers) {
            pair(0, "LAYER");
            pair(2, name);
            pair(70, "0");
            pair(62, std::to_string(kDxfColours[static_cast<size_t>(colour)]));
            pair(6, "CONTINUOUS");
        }
        pair(0, "ENDTAB");
        pair(0, "ENDSEC");
        pair(0, "SECTION");
        pair(2, "ENTITIES");
    }

    // The drawing centred on `centre`: outlines and holes on cut_layer at elevation z_mm,
    // extruded by thickness_mm; engraving on engrave_layer at engrave_z_mm.
    void add(const Drawing &d, P centre, const std::string &cut_layer,
             const std::string &engrave_layer, double z_mm, double thickness_mm,
             double engrave_z_mm) {
        for (const Loop &l : d.cut) {
            polyline(cut_layer, l, true, centre, z_mm, thickness_mm);
        }
        for (const Circle &c : d.holes) {
            pair(0, "CIRCLE");
            pair(8, cut_layer);
            if (thickness_mm > 0.0) {
                pair(39, num(thickness_mm));
            }
            pair(10, num(c.centre.x - centre.x));
            pair(20, num(c.centre.y - centre.y));
            pair(30, num(z_mm));
            pair(40, num(c.radius));
        }
        for (const std::vector<P> &l : d.engrave) {
            polyline(engrave_layer, l, false, centre, engrave_z_mm, 0.0);
        }
    }

    // The rig export's shapes (already centred, in mm) on layers 0_<layer id>, at z_mm.
    void add_shapes(const std::vector<ExportShape> &shapes, double z_mm) {
        const P origin{0.0, 0.0};
        for (const ExportShape &s : shapes) {
            const std::string layer = std::string("0_") + kExportLayerIds[s.layer];
            if (s.kind == ExportShape::Kind::polyline) {
                std::vector<P> line;
                for (const Vec3 &v : s.points) {
                    line.push_back({v.x, v.y});
                }
                if (s.closed && !line.empty()) {
                    line.push_back(line.front()); // polyline() drops a closed one's last point.
                }
                polyline(layer, line, s.closed, origin, z_mm, 0.0);
            } else if (s.kind == ExportShape::Kind::circle) {
                pair(0, "CIRCLE");
                pair(8, layer);
                pair(10, num(s.points[0].x));
                pair(20, num(s.points[0].y));
                pair(30, num(z_mm));
                pair(40, num(s.radius_mm));
            } else {
                pair(0, "TEXT");
                pair(8, layer);
                pair(10, num(s.points[0].x));
                pair(20, num(s.points[0].y));
                pair(30, num(z_mm));
                pair(40, num(4.0));
                pair(1, s.label);
            }
        }
    }

    std::string finish() {
        pair(0, "ENDSEC");
        pair(0, "EOF");
        return std::move(text_);
    }

  private:
    void pair(int code, const std::string &value) {
        text_ += std::to_string(code) + "\n" + value + "\n";
    }
    // A 2D polyline in its plane at elevation z (the header point's z), its vertices in x and y.
    void polyline(const std::string &layer, const std::vector<P> &l, bool closed, P centre,
                  double z_mm, double thickness_mm) {
        pair(0, "POLYLINE");
        pair(8, layer);
        if (thickness_mm > 0.0) {
            pair(39, num(thickness_mm));
        }
        pair(66, "1");
        pair(10, "0.0");
        pair(20, "0.0");
        pair(30, num(z_mm));
        pair(70, closed ? "1" : "0");
        for (size_t k = 0; k < l.size(); ++k) {
            if (closed && k + 1 == l.size()) {
                break; // A closed polyline does not repeat its first point.
            }
            pair(0, "VERTEX");
            pair(8, layer);
            pair(10, num(l[k].x - centre.x));
            pair(20, num(l[k].y - centre.y));
            pair(30, num(z_mm));
        }
        pair(0, "SEQEND");
        pair(8, layer);
    }

    std::string text_;
};

// Both files for a part, centred on `centre`: Cut (outlines and holes) and Engrave layers.
BuildKitPart write_part(const std::string &name, const Drawing &d, P centre,
                        const BuildKitOptions &o) {
    BuildKitPart part{name, {}, {}};
    double half_x = 0.0;
    double half_y = 0.0;
    const auto extend = [&](const P &p, double r) {
        half_x = std::max(half_x, std::abs(p.x - centre.x) + r);
        half_y = std::max(half_y, std::abs(p.y - centre.y) + r);
    };
    for (const Loop &l : d.cut) {
        for (const P &p : l) {
            extend(p, 0.0);
        }
    }
    for (const Circle &c : d.holes) {
        extend(c.centre, c.radius);
    }
    for (const std::vector<P> &l : d.engrave) {
        for (const P &p : l) {
            extend(p, 0.0);
        }
    }
    const double margin = 10.0;
    const double w = 2.0 * (half_x + margin);
    const double h = 2.0 * (half_y + margin);
    const auto X = [&](double x) { return num(x - centre.x + 0.5 * w); };
    const auto Y = [&](double y) { return num(0.5 * h - (y - centre.y)); };
    const std::string cut = kSvgColours[static_cast<size_t>(o.cut_colour)];
    const std::string engrave = kSvgColours[static_cast<size_t>(o.engrave_colour)];
    std::string svg =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- Psyight build kit: " + name +
        ", millimetres. -->\n<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" width=\"" +
        num(w) + "mm\" height=\"" + num(h) + "mm\" viewBox=\"0 0 " + num(w) + " " + num(h) +
        "\">\n  <g inkscape:groupmode=\"layer\" inkscape:label=\"Cut\" "
        "fill=\"none\" stroke=\"" +
        cut + "\" stroke-width=\"0.1\">\n";
    const auto path = [&](const std::vector<P> &l, bool closed) {
        std::string s = "    <path d=\"M";
        for (const P &p : l) {
            s += " " + X(p.x) + " " + Y(p.y);
        }
        return s + (closed ? " Z\"/>\n" : "\"/>\n");
    };
    for (const Loop &l : d.cut) {
        svg += path(l, true);
    }
    for (const Circle &c : d.holes) {
        svg += "    <circle cx=\"" + X(c.centre.x) + "\" cy=\"" + Y(c.centre.y) + "\" r=\"" +
               num(c.radius) + "\"/>\n";
    }
    svg += "  </g>\n";
    if (!d.engrave.empty()) {
        svg += "  <g inkscape:groupmode=\"layer\" inkscape:label=\"Engrave\" fill=\"none\" "
               "stroke=\"" +
               engrave + "\" stroke-width=\"0.1\">\n";
        for (const std::vector<P> &l : d.engrave) {
            svg += path(l, false);
        }
        svg += "  </g>\n";
    }
    svg += "</svg>\n";
    part.svg = std::move(svg);

    DxfWriter dxf({{"Cut", o.cut_colour}, {"Engrave", o.engrave_colour}});
    dxf.add(d, centre, "Cut", "Engrave", 0.0, 0.0, 0.0);
    part.dxf = dxf.finish();
    return part;
}

} // namespace

BuildKit build_kit(const std::vector<StripSpec> &strips, const BuildKitOptions &options,
                   const RigExportOptions *rig, const std::vector<std::string> &notes) {
    BuildKit kit;
    std::vector<std::vector<P>> lines;
    double lo_x = 1e18, hi_x = -1e18, lo_y = 1e18, hi_y = -1e18;
    for (const StripSpec &s : strips) {
        std::vector<P> line;
        for (const Vec3 &v : s.polyline) {
            line.push_back({v.x * 1000.0, v.y * 1000.0});
            lo_x = std::min(lo_x, line.back().x);
            hi_x = std::max(hi_x, line.back().x);
            lo_y = std::min(lo_y, line.back().y);
            hi_y = std::max(hi_y, line.back().y);
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    if (lines.empty()) {
        return kit;
    }
    const bool lattice = options.style == BuildKitStyle::lattice;
    const double channel_r = 500.0 * options.channel_width_m;
    const double acrylic_r = channel_r + 1000.0 * options.overlap_m;
    const double bolt_r = 500.0 * options.bolt_hole_m;
    const double need = bolt_r + kBoltWoodMm;
    // An ear's bolt from its channel's centre line: clear of the acrylic by half a millimetre and
    // of the channel by 3 mm of wood.
    const double ear_at = std::max(acrylic_r + 0.5, channel_r + kBoltWoodMm) + bolt_r;
    const double panel_r = channel_r + 1000.0 * (lattice ? options.wall_m : options.margin_m);
    const double reach =
        std::max({panel_r, acrylic_r, lattice ? ear_at + need : 0.0}) + kMarginExtraMm;
    Grid g;
    const double span = std::max(hi_x - lo_x, hi_y - lo_y) + 2.0 * reach;
    g.cell = std::max(kFineCellMm, span / kMaxCells);
    g.x0 = lo_x - reach;
    g.y0 = lo_y - reach;
    g.nx = static_cast<int>(std::ceil((hi_x - lo_x + 2.0 * reach) / g.cell)) + 1;
    g.ny = static_cast<int>(std::ceil((hi_y - lo_y + 2.0 * reach) / g.cell)) + 1;
    g.v.assign(static_cast<size_t>(g.nx) * g.ny, 0);
    const P centre{0.5 * (lo_x + hi_x), 0.5 * (lo_y + hi_y)};

    const Grid channels = capsules(g, lines, channel_r);
    // The panel, or the lattice with its openings left open.
    const Grid panel = lattice ? capsules(g, lines, panel_r) : filled(capsules(g, lines, panel_r));
    const Grid spacer = minus(panel, channels);
    const Grid windows = capsules(g, lines, 500.0 * options.window_width_m);
    const Grid acrylic = capsules(g, lines, acrylic_r);

    // Islands: each spacer piece, with bolt holes at its deepest points (or in ears) and a number.
    int count = 0;
    const std::vector<int> label = components(spacer, count);
    const std::vector<double> depth = distances(spacer);
    // Room for a bolt: 3 mm of wood around it, and its hole clear of the acrylic (which reaches
    // past the channels) by half a millimetre.
    const std::vector<double> clearance = distances(minus(panel, acrylic));
    std::vector<double> room(depth.size());
    for (size_t k = 0; k < room.size(); ++k) {
        room[k] = std::min(depth[k] - need, clearance[k] - bolt_r - 0.5);
    }
    std::vector<Circle> bolts;
    std::vector<Circle> ears;
    std::vector<std::vector<P>> numbers;
    std::vector<int> deepest(static_cast<size_t>(count), -1);
    std::vector<int> roomiest(static_cast<size_t>(count), -1);
    std::vector<std::array<double, 4>> box(static_cast<size_t>(count), {1e18, -1e18, 1e18, -1e18});
    for (int j = 0; j < g.ny; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            const size_t k = static_cast<size_t>(j) * g.nx + i;
            const int c = label[k];
            if (c < 0) {
                continue;
            }
            int &d = deepest[static_cast<size_t>(c)];
            if (d < 0 || depth[k] > depth[static_cast<size_t>(d)]) {
                d = static_cast<int>(k);
            }
            int &r = roomiest[static_cast<size_t>(c)];
            if (r < 0 || room[k] > room[static_cast<size_t>(r)]) {
                r = static_cast<int>(k);
            }
            const P p = g.centre(i, j);
            auto &b = box[static_cast<size_t>(c)];
            b = {std::min(b[0], p.x), std::max(b[1], p.x), std::min(b[2], p.y),
                 std::max(b[3], p.y)};
        }
    }
    for (int c = 0; c < count; ++c) {
        const int k0 = deepest[static_cast<size_t>(c)];
        const int kb = roomiest[static_cast<size_t>(c)];
        const P first = g.centre(kb % g.nx, kb / g.nx);
        std::vector<P> holes;
        if (room[static_cast<size_t>(kb)] >= 0.0) {
            holes.push_back(first);
            const auto &b = box[static_cast<size_t>(c)];
            if (std::hypot(b[1] - b[0], b[3] - b[2]) > kSecondBoltSpanMm) {
                // A second hole: deep enough, as far from the first as possible.
                double best = 0.0;
                P second{};
                for (size_t k = 0; k < label.size(); ++k) {
                    if (label[k] != c || room[k] < 0.0 ||
                        depth[k] < 0.6 * depth[static_cast<size_t>(kb)]) {
                        continue;
                    }
                    const P p = g.centre(static_cast<int>(k % g.nx), static_cast<int>(k / g.nx));
                    const double d = std::hypot(p.x - first.x, p.y - first.y);
                    if (d > best) {
                        best = d;
                        second = p;
                    }
                }
                if (best > 0.4 * kSecondBoltSpanMm) {
                    holes.push_back(second);
                }
            }
        } else if (lattice) {
            // Ears: from the wall's middle (half its deepest depth or more: an inside corner is
            // deeper than the rest), each spot pushed out from its nearest channel to ear_at,
            // kept where no other channel comes nearer.
            const double top = depth[static_cast<size_t>(k0)];
            std::vector<P> spots;
            for (size_t k = 0; k < label.size(); ++k) {
                if (label[k] != c || depth[k] < 0.5 * top) {
                    continue;
                }
                const P p = g.centre(static_cast<int>(k % g.nx), static_cast<int>(k / g.nx));
                P q{};
                const double d = nearest_on_lines(p, lines, q);
                if (d < 1e-9) {
                    continue;
                }
                const P spot{q.x + (p.x - q.x) * ear_at / d, q.y + (p.y - q.y) * ear_at / d};
                P unused{};
                if (nearest_on_lines(spot, lines, unused) >= ear_at - 0.5 * g.cell) {
                    spots.push_back(spot);
                }
            }
            if (!spots.empty()) {
                // The spot nearest the island's middle; on a long island, a second as far from
                // it as possible.
                const auto &b = box[static_cast<size_t>(c)];
                const P mid{0.5 * (b[0] + b[1]), 0.5 * (b[2] + b[3])};
                const auto from = [](const P &a, const P &z) {
                    return std::hypot(a.x - z.x, a.y - z.y);
                };
                const P one =
                    *std::min_element(spots.begin(), spots.end(), [&](const P &u, const P &v) {
                        return from(u, mid) < from(v, mid);
                    });
                holes.push_back(one);
                if (std::hypot(b[1] - b[0], b[3] - b[2]) > kSecondBoltSpanMm) {
                    const P two =
                        *std::max_element(spots.begin(), spots.end(), [&](const P &u, const P &v) {
                            return from(u, one) < from(v, one);
                        });
                    if (from(two, one) > 0.4 * kSecondBoltSpanMm) {
                        holes.push_back(two);
                    }
                }
                for (const P &h : holes) {
                    ears.push_back({h, need});
                }
            }
        }
        for (const P &h : holes) {
            bolts.push_back({h, bolt_r});
        }
        // The island's number: the deepest cell clear of its bolt holes by half its width.
        const double digits = static_cast<double>(std::to_string(c + 1).size());
        const double half_width = 0.5 * kNumberMaxMm * (0.85 * digits - 0.3);
        double best = 0.0;
        P at{};
        for (size_t k = 0; k < label.size(); ++k) {
            if (label[k] != c) {
                continue;
            }
            const P p = g.centre(static_cast<int>(k % g.nx), static_cast<int>(k / g.nx));
            bool clear = true;
            for (const P &h : holes) {
                clear = clear && std::hypot(p.x - h.x, p.y - h.y) > bolt_r + half_width + 2.0;
            }
            if (clear && depth[k] > best) {
                best = depth[k];
                at = p;
            }
        }
        const double height = std::min(kNumberMaxMm, 1.2 * best);
        if (height >= 2.0) {
            add_number(numbers, c + 1, at, height);
        }
    }
    kit.islands = count;
    kit.bolts = static_cast<int>(bolts.size());
    kit.ears = static_cast<int>(ears.size());
    // The ears join the back, spacer and front.
    const Grid body = with_discs(panel, ears);
    const Grid walls = minus(with_discs(spacer, ears), channels);
    const Grid front = minus(body, windows);

    std::vector<Circle> wires;
    for (const std::vector<P> &line : lines) {
        wires.push_back({line.front(), 500.0 * options.wire_hole_m});
        const bool ring = line.size() > 2 && std::hypot(line.front().x - line.back().x,
                                                        line.front().y - line.back().y) < 1e-3;
        if (!ring) {
            wires.push_back({line.back(), 500.0 * options.wire_hole_m});
        }
    }
    const std::vector<Loop> islands = outlines(walls);

    Drawing back;
    back.cut = outlines(body);
    back.holes = bolts;
    back.holes.insert(back.holes.end(), wires.begin(), wires.end());
    for (const Loop &l : islands) {
        back.engrave.push_back(l);
    }
    back.engrave.insert(back.engrave.end(), numbers.begin(), numbers.end());

    Drawing spacer_drawing;
    spacer_drawing.cut = islands;
    spacer_drawing.holes = bolts;
    spacer_drawing.engrave = numbers;

    Drawing acrylic_drawing;
    acrylic_drawing.cut = outlines(acrylic);

    Drawing front_drawing;
    front_drawing.cut = outlines(front);
    front_drawing.holes = bolts;

    kit.parts.push_back(write_part("back", back, centre, options));
    kit.parts.push_back(write_part("spacer", spacer_drawing, centre, options));
    kit.parts.push_back(write_part("acrylic", acrylic_drawing, centre, options));
    kit.parts.push_back(write_part("front", front_drawing, centre, options));

    // The assembly: every part on its own layers, stacked at its height and as thick as its sheet
    // (the back from 0 up); engraving on each part's top face.
    const std::array<std::pair<const Drawing *, double>, 4> stack = {
        {{&back, options.back_thickness_m},
         {&spacer_drawing, options.spacer_thickness_m},
         {&acrylic_drawing, options.acrylic_thickness_m},
         {&front_drawing, options.front_thickness_m}}};
    std::vector<std::pair<std::string, ExportColour>> layers;
    if (rig != nullptr) {
        for (int layer = 0; layer < 4; ++layer) {
            layers.push_back(
                {std::string("0_") + kExportLayerIds[layer], export_layer_colour(layer, *rig)});
        }
    }
    for (size_t i = 0; i < kit.parts.size(); ++i) {
        const std::string base = std::to_string(i + 1) + "_" + kit.parts[i].name;
        layers.push_back({base + "_cut", options.cut_colour});
        layers.push_back({base + "_engrave", options.engrave_colour});
    }
    DxfWriter assembly(layers);
    double z = 0.0;
    for (size_t i = 0; i < stack.size(); ++i) {
        const double thick = 1000.0 * stack[i].second;
        const std::string base = std::to_string(i + 1) + "_" + kit.parts[i].name;
        assembly.add(*stack[i].first, centre, base + "_cut", base + "_engrave", z, thick,
                     z + thick);
        z += thick;
    }
    if (rig != nullptr) { // On the back panel's top face, where the strips sit.
        assembly.add_shapes(rig_export_shapes(strips, notes, *rig),
                            1000.0 * options.back_thickness_m);
    }
    kit.assembly_dxf = assembly.finish();
    kit.height_m = z / 1000.0;
    return kit;
}

} // namespace psyight
