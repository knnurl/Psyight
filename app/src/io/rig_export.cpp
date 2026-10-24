#include "io/rig_export.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace psyight {

namespace {

constexpr double kMmPerM = 1000.0;
constexpr double kPixelRadiusMm = 1.5;
constexpr double kArrowLengthMm = 8.0;
constexpr double kArrowHalfWidthMm = 3.0;
constexpr double kLabelHeightMm = 4.0;
constexpr double kMarginMm = 10.0;
constexpr double kMaxMitre = 2.0; // Corner offsets at most this many half widths.

enum Layer { kCentre, kChannels, kPixels, kWiring, kLayers };

struct LayerStyle {
    const char *svg_name;
    const char *dxf_name; // R12 layer names: letters, digits, $, - and _.
    const char *svg_colour;
    int dxf_colour;
};

constexpr std::array<LayerStyle, kLayers> kStyles = {{
    {"Centre lines", "Centre_lines", "#00a0c0", 4},
    {"Channels", "Channels", "#d03030", 1},
    {"Pixels", "Pixels", "#c08000", 2},
    {"Wiring", "Wiring", "#208040", 3},
}};

struct P2 {
    double x = 0.0;
    double y = 0.0;
};

struct Prim {
    Layer layer = kCentre;
    enum Kind { polyline, circle, text } kind = polyline;
    std::vector<P2> points; // Polyline; a circle's or a text's first point is its position.
    bool closed = false;
    double radius = 0.0;
    std::string label;
    double angle_deg = 0.0;
};

P2 mm(const Vec3 &v) { return {v.x * kMmPerM, v.y * kMmPerM}; }

P2 unit(P2 v) {
    const double n = std::hypot(v.x, v.y);
    return n > 0.0 ? P2{v.x / n, v.y / n} : P2{1.0, 0.0};
}

// The left normal of the segment a to b.
P2 normal(P2 a, P2 b) {
    const P2 d = unit({b.x - a.x, b.y - a.y});
    return {-d.y, d.x};
}

// The polyline offset by `by` to its left (negative: right), corners mitred.
std::vector<P2> offset(const std::vector<P2> &p, double by, bool closed) {
    const size_t n = p.size();
    std::vector<P2> out;
    for (size_t i = 0; i < n; ++i) {
        const bool has_prev = i > 0 || closed;
        const bool has_next = i + 1 < n || closed;
        const P2 prev = p[i > 0 ? i - 1 : n - 1];
        const P2 next = p[i + 1 < n ? i + 1 : 0];
        const P2 n_in = has_prev ? normal(prev, p[i]) : normal(p[i], next);
        const P2 n_out = has_next ? normal(p[i], next) : n_in;
        P2 m = unit({n_in.x + n_out.x, n_in.y + n_out.y});
        const double cos_half = m.x * n_out.x + m.y * n_out.y;
        const double scale = std::min(kMaxMitre, 1.0 / std::max(1e-6, cos_half));
        out.push_back({p[i].x + m.x * by * scale, p[i].y + m.y * by * scale});
    }
    return out;
}

std::vector<P2> without_repeats(const std::vector<Vec3> &polyline) {
    std::vector<P2> out;
    for (const Vec3 &v : polyline) {
        const P2 q = mm(v);
        if (out.empty() || std::hypot(q.x - out.back().x, q.y - out.back().y) > 1e-6) {
            out.push_back(q);
        }
    }
    return out;
}

std::vector<Prim> primitives(const std::vector<StripSpec> &strips,
                             const std::vector<std::string> &notes,
                             const RigExportOptions &options) {
    std::vector<Prim> prims;
    const double half = 0.5 * options.channel_width_m * kMmPerM;
    for (size_t s = 0; s < strips.size(); ++s) {
        std::vector<P2> p = without_repeats(strips[s].polyline);
        if (p.size() < 2) {
            continue;
        }
        const bool closed =
            p.size() > 2 && std::hypot(p.front().x - p.back().x, p.front().y - p.back().y) < 1e-3;
        if (options.centre_lines) {
            prims.push_back({kCentre, Prim::polyline, p, false});
        }
        if (options.channels && half > 0.0) {
            if (closed) {
                std::vector<P2> loop(p.begin(), p.end() - 1);
                prims.push_back({kChannels, Prim::polyline, offset(loop, half, true), true});
                prims.push_back({kChannels, Prim::polyline, offset(loop, -half, true), true});
            } else {
                std::vector<P2> slot = offset(p, half, false);
                std::vector<P2> right = offset(p, -half, false);
                slot.insert(slot.end(), right.rbegin(), right.rend());
                prims.push_back({kChannels, Prim::polyline, slot, true});
            }
        }
        if (options.wiring) {
            const P2 d = unit({p[1].x - p[0].x, p[1].y - p[0].y});
            const P2 nrm{-d.y, d.x};
            const P2 tip{p[0].x + d.x * kArrowLengthMm, p[0].y + d.y * kArrowLengthMm};
            prims.push_back(
                {kWiring,
                 Prim::polyline,
                 {tip,
                  {p[0].x + nrm.x * kArrowHalfWidthMm, p[0].y + nrm.y * kArrowHalfWidthMm},
                  {p[0].x - nrm.x * kArrowHalfWidthMm, p[0].y - nrm.y * kArrowHalfWidthMm}},
                 true});
            Prim label{kWiring,
                       Prim::text,
                       {{p[0].x + nrm.x * 2.0 * kArrowHalfWidthMm,
                         p[0].y + nrm.y * 2.0 * kArrowHalfWidthMm}}};
            label.label = std::to_string(s + 1) + ". " + strips[s].name;
            if (s < notes.size() && !notes[s].empty()) {
                label.label += ", " + notes[s];
            }
            prims.push_back(label);
        }
    }
    if (options.pixels) {
        for (const Pixel &px : build_rig(strips).pixels) {
            Prim dot{kPixels, Prim::circle, {mm(px.position)}};
            dot.radius = kPixelRadiusMm;
            prims.push_back(dot);
        }
    }
    // Centred on the middle of the rig's paths: the drawing's origin is the rig's centre.
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = min_x;
    double max_x = -min_x;
    double max_y = -min_x;
    for (const StripSpec &s : strips) {
        for (const Vec3 &v : s.polyline) {
            const P2 q = mm(v);
            min_x = std::min(min_x, q.x);
            max_x = std::max(max_x, q.x);
            min_y = std::min(min_y, q.y);
            max_y = std::max(max_y, q.y);
        }
    }
    if (std::isfinite(min_x)) {
        const P2 centre{0.5 * (min_x + max_x), 0.5 * (min_y + max_y)};
        for (Prim &p : prims) {
            for (P2 &q : p.points) {
                q.x -= centre.x;
                q.y -= centre.y;
            }
        }
    }
    return prims;
}

// How far a primitive reaches from the origin in x and y (a text's width estimated).
P2 reach(const Prim &p) {
    P2 r;
    for (const P2 &q : p.points) {
        const double right = p.kind == Prim::text
                                 ? q.x + 0.6 * kLabelHeightMm * static_cast<double>(p.label.size())
                                 : q.x + p.radius;
        r.x = std::max({r.x, std::abs(q.x - p.radius), std::abs(right)});
        const double top = p.kind == Prim::text ? q.y + kLabelHeightMm : q.y + p.radius;
        r.y = std::max({r.y, std::abs(q.y - p.radius), std::abs(top)});
    }
    return r;
}

std::string number(double v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", std::abs(v) < 5e-4 ? 0.0 : v);
    return text;
}

std::string escaped(const std::string &s) {
    std::string out;
    for (const char ch : s) {
        out += ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : std::string(1, ch);
    }
    return out;
}

} // namespace

std::string rig_to_svg(const std::vector<StripSpec> &strips, const std::vector<std::string> &notes,
                       const RigExportOptions &options) {
    const std::vector<Prim> prims = primitives(strips, notes, options);
    // The page symmetric about the rig's centre (the primitives' origin), with room for all.
    P2 half;
    for (const Prim &p : prims) {
        const P2 r = reach(p);
        half.x = std::max(half.x, r.x);
        half.y = std::max(half.y, r.y);
    }
    const double width = 2.0 * (half.x + kMarginMm);
    const double height = 2.0 * (half.y + kMarginMm);
    const auto X = [&](double x) { return number(x + 0.5 * width); };
    const auto Y = [&](double y) { return number(0.5 * height - y); }; // SVG's y runs down.
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<!-- Exported from Psyight: the rig in millimetres. -->\n"
                      "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                      "xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" width=\"" +
                      number(width) + "mm\" height=\"" + number(height) + "mm\" viewBox=\"0 0 " +
                      number(width) + " " + number(height) + "\">\n";
    for (int layer = 0; layer < kLayers; ++layer) {
        const LayerStyle &style = kStyles[static_cast<size_t>(layer)];
        std::string body;
        for (const Prim &p : prims) {
            if (p.layer != layer) {
                continue;
            }
            if (p.kind == Prim::polyline) {
                body += "    <path d=\"M";
                for (const P2 &q : p.points) {
                    body += " " + X(q.x) + " " + Y(q.y);
                }
                body += p.closed ? " Z\"/>\n" : "\"/>\n";
            } else if (p.kind == Prim::circle) {
                body += "    <circle cx=\"" + X(p.points[0].x) + "\" cy=\"" + Y(p.points[0].y) +
                        "\" r=\"" + number(p.radius) + "\"/>\n";
            } else {
                body += "    <text x=\"" + X(p.points[0].x) + "\" y=\"" + Y(p.points[0].y) +
                        "\" font-size=\"" + number(kLabelHeightMm) + "\" fill=\"" +
                        style.svg_colour + "\" stroke=\"none\">" + escaped(p.label) + "</text>\n";
            }
        }
        if (body.empty()) {
            continue;
        }
        out += "  <g inkscape:groupmode=\"layer\" inkscape:label=\"" + std::string(style.svg_name) +
               "\" fill=\"none\" stroke=\"" + style.svg_colour +
               "\" stroke-width=\"0.3\" font-family=\"sans-serif\">\n" + body + "  </g>\n";
    }
    out += "</svg>\n";
    return out;
}

std::string rig_to_dxf(const std::vector<StripSpec> &strips, const std::vector<std::string> &notes,
                       const RigExportOptions &options) {
    const std::vector<Prim> prims = primitives(strips, notes, options);
    std::string out;
    const auto pair = [&](int code, const std::string &value) {
        out += std::to_string(code) + "\n" + value + "\n";
    };
    pair(0, "SECTION");
    pair(2, "HEADER");
    pair(9, "$ACADVER");
    pair(1, "AC1009");
    pair(9, "$INSUNITS");
    pair(70, "4"); // Millimetres.
    pair(0, "ENDSEC");
    pair(0, "SECTION");
    pair(2, "TABLES");
    pair(0, "TABLE");
    pair(2, "LTYPE");
    pair(70, "1");
    pair(0, "LTYPE");
    pair(2, "CONTINUOUS");
    pair(70, "0");
    pair(3, "Solid line");
    pair(72, "65");
    pair(73, "0");
    pair(40, "0.0");
    pair(0, "ENDTAB");
    pair(0, "TABLE");
    pair(2, "LAYER");
    pair(70, std::to_string(kLayers));
    for (const LayerStyle &style : kStyles) {
        pair(0, "LAYER");
        pair(2, style.dxf_name);
        pair(70, "0");
        pair(62, std::to_string(style.dxf_colour));
        pair(6, "CONTINUOUS");
    }
    pair(0, "ENDTAB");
    pair(0, "ENDSEC");
    pair(0, "SECTION");
    pair(2, "ENTITIES");
    for (const Prim &p : prims) {
        const std::string layer = kStyles[static_cast<size_t>(p.layer)].dxf_name;
        if (p.kind == Prim::polyline) {
            pair(0, "POLYLINE");
            pair(8, layer);
            pair(66, "1");
            pair(10, "0.0");
            pair(20, "0.0");
            pair(30, "0.0");
            pair(70, p.closed ? "1" : "0");
            for (const P2 &q : p.points) {
                pair(0, "VERTEX");
                pair(8, layer);
                pair(10, number(q.x));
                pair(20, number(q.y));
                pair(30, "0.0");
            }
            pair(0, "SEQEND");
            pair(8, layer);
        } else if (p.kind == Prim::circle) {
            pair(0, "CIRCLE");
            pair(8, layer);
            pair(10, number(p.points[0].x));
            pair(20, number(p.points[0].y));
            pair(30, "0.0");
            pair(40, number(p.radius));
        } else {
            pair(0, "TEXT");
            pair(8, layer);
            pair(10, number(p.points[0].x));
            pair(20, number(p.points[0].y));
            pair(30, "0.0");
            pair(40, number(kLabelHeightMm));
            pair(1, p.label);
        }
    }
    pair(0, "ENDSEC");
    pair(0, "EOF");
    return out;
}

} // namespace psyight
