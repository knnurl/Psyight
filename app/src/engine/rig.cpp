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

bool is_ring(const StripSpec &s) {
    return s.polyline.size() > 2 && distance(s.polyline.front(), s.polyline.back()) < 1e-6;
}

// Each point's distance along the polyline.
std::vector<double> arc_lengths(const std::vector<Vec3> &p) {
    std::vector<double> s(p.size(), 0.0);
    for (size_t i = 1; i < p.size(); ++i) {
        s[i] = s[i - 1] + distance(p[i - 1], p[i]);
    }
    return s;
}

double point_to_segment_m(const Vec3 &p, const Vec3 &a, const Vec3 &b) {
    const Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
    const double len2 = d.x * d.x + d.y * d.y + d.z * d.z;
    const double t =
        len2 > 0.0 ? std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y + (p.z - a.z) * d.z) / len2,
                                0.0, 1.0)
                   : 0.0;
    return distance(p, lerp(a, b, t));
}

// The angle the polyline turns through at b, coming from a and going on to c.
double turn_deg(const Vec3 &a, const Vec3 &b, const Vec3 &c) {
    const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 v{c.x - b.x, c.y - b.y, c.z - b.z};
    const double lu = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    const double lv = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (lu <= 0.0 || lv <= 0.0) {
        return 0.0;
    }
    const double cosine = std::clamp((u.x * v.x + u.y * v.y + u.z * v.z) / (lu * lv), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / 3.14159265358979323846;
}

// The part [t0, t1] of segment a0 to a1 within r of segment b0 to b1; false when none. The
// distance is convex along the segment, so the part is one interval: its nearest point by
// ternary search, its edges by bisection.
bool near_part(const Vec3 &a0, const Vec3 &a1, const Vec3 &b0, const Vec3 &b1, double r, double &t0,
               double &t1) {
    const auto f = [&](double t) { return point_to_segment_m(lerp(a0, a1, t), b0, b1); };
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 60; ++i) {
        const double m1 = lo + (hi - lo) / 3.0;
        const double m2 = hi - (hi - lo) / 3.0;
        if (f(m1) < f(m2)) {
            hi = m2;
        } else {
            lo = m1;
        }
    }
    const double nearest = 0.5 * (lo + hi);
    if (f(nearest) >= r) {
        return false;
    }
    const auto edge = [&](double inside, double outside) {
        for (int i = 0; i < 60; ++i) {
            const double mid = 0.5 * (inside + outside);
            (f(mid) < r ? inside : outside) = mid;
        }
        return 0.5 * (inside + outside);
    };
    t0 = f(0.0) < r ? 0.0 : edge(nearest, 0.0);
    t1 = f(1.0) < r ? 1.0 : edge(nearest, 1.0);
    return true;
}

// Where two strips come within the clearance of each other: the stretch on each.
struct Contact {
    Span on_a;
    Span on_b;
};

bool touching(const Span &x, const Span &y) {
    return x.from_m <= y.to_m + 1e-6 && y.from_m <= x.to_m + 1e-6;
}

// A straight stretch of strip: its ends and where they lie along the polyline.
struct Stretch {
    Vec3 a;
    Vec3 b;
    double from_m = 0.0;
    double to_m = 0.0;
};

// The polyline's segments less the cuts (sorted by start): where the strip really is.
std::vector<Stretch> stretches(const std::vector<Vec3> &p, const std::vector<double> &arcs,
                               const std::vector<Span> &cuts) {
    std::vector<Stretch> out;
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        const double s0 = arcs[i];
        const double s1 = arcs[i + 1];
        if (s1 <= s0) {
            continue;
        }
        double at = s0;
        const auto keep = [&](double from, double to) {
            if (to > from) {
                out.push_back({lerp(p[i], p[i + 1], (from - s0) / (s1 - s0)),
                               lerp(p[i], p[i + 1], (to - s0) / (s1 - s0)), from, to});
            }
        };
        for (const Span &cut : cuts) {
            if (cut.to_m <= at || cut.from_m >= s1) {
                continue;
            }
            keep(at, std::max(at, cut.from_m));
            at = std::max(at, cut.to_m);
        }
        keep(at, s1);
    }
    return out;
}

} // namespace

std::vector<std::vector<Span>> strip_pieces(const std::vector<StripSpec> &strips,
                                            double clearance_m) {
    const double c = clearance_m;
    const size_t n = strips.size();
    std::vector<std::vector<double>> arcs(n);
    std::vector<std::pair<Vec3, Vec3>> boxes(n);
    std::vector<std::vector<Span>> cuts(n);
    std::vector<std::vector<Vec3>> corner_points(n); // Each strip's sharp corners.
    for (size_t i = 0; i < n; ++i) {
        const std::vector<Vec3> &p = strips[i].polyline;
        arcs[i] = arc_lengths(p);
        if (p.size() < 2 || strips[i].pixels_per_m < kJointMinPixelsPerM) {
            continue;
        }
        Vec3 lo = p.front();
        Vec3 hi = p.front();
        for (const Vec3 &q : p) {
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
        }
        boxes[i] = {lo, hi};
        // Sharp corners: both sides.
        for (size_t v = 1; v + 1 < p.size(); ++v) {
            if (turn_deg(p[v - 1], p[v], p[v + 1]) > kJointTurnDeg) {
                cuts[i].push_back({arcs[i][v] - c, arcs[i][v] + c});
                corner_points[i].push_back(p[v]);
            }
        }
        if (is_ring(strips[i]) && turn_deg(p[p.size() - 2], p[0], p[1]) > kJointTurnDeg) {
            cuts[i].push_back({0.0, c});
            cuts[i].push_back({arcs[i].back() - c, arcs[i].back()});
            corner_points[i].push_back(p[0]);
        }
    }
    // Whether p is one of strip s's corners.
    const auto on_corner = [&](size_t s, const Vec3 &p) {
        return std::any_of(corner_points[s].begin(), corner_points[s].end(),
                           [&](const Vec3 &v) { return distance(v, p) < 1e-4; });
    };
    const auto near_boxes = [&](size_t a, size_t b) {
        const auto &[la, ha] = boxes[a];
        const auto &[lb, hb] = boxes[b];
        return la.x <= hb.x + c && lb.x <= ha.x + c && la.y <= hb.y + c && lb.y <= ha.y + c &&
               la.z <= hb.z + c && lb.z <= ha.z + c;
    };
    // Which of two strips is cut where they cross away from their ends.
    const auto yields = [&](size_t a, size_t b) {
        if (is_ring(strips[a]) != is_ring(strips[b])) {
            return is_ring(strips[a]);
        }
        if (std::abs(arcs[a].back() - arcs[b].back()) > 1e-9) {
            return arcs[a].back() < arcs[b].back();
        }
        return a > b;
    };
    // Contacts are with where each strip really is: its corners already cut out, so a spoke
    // ending on a ring's corner runs into the gap there, level with the ring's pieces.
    std::vector<std::vector<Stretch>> material(n);
    for (size_t i = 0; i < n; ++i) {
        std::vector<Span> corners = cuts[i];
        std::sort(corners.begin(), corners.end(),
                  [](const Span &x, const Span &y) { return x.from_m < y.from_m; });
        material[i] = stretches(strips[i].polyline, arcs[i], corners);
    }
    for (size_t a = 0; a < n; ++a) {
        for (size_t b = a + 1; b < n; ++b) {
            if (strips[a].polyline.size() < 2 || strips[b].polyline.size() < 2 ||
                strips[a].pixels_per_m < kJointMinPixelsPerM ||
                strips[b].pixels_per_m < kJointMinPixelsPerM || !near_boxes(a, b)) {
                continue;
            }
            // Each pair of stretches within the clearance, merged into contacts that run on
            // together on both strips.
            std::vector<Contact> contacts;
            for (const Stretch &sa : material[a]) {
                for (const Stretch &sb : material[b]) {
                    const auto apart = [&](double a0, double a1, double b0, double b1) {
                        return std::min(a0, a1) > std::max(b0, b1) + c ||
                               std::min(b0, b1) > std::max(a0, a1) + c;
                    };
                    if (apart(sa.a.x, sa.b.x, sb.a.x, sb.b.x) ||
                        apart(sa.a.y, sa.b.y, sb.a.y, sb.b.y) ||
                        apart(sa.a.z, sa.b.z, sb.a.z, sb.b.z)) {
                        continue;
                    }
                    double t0 = 0.0, t1 = 0.0, u0 = 0.0, u1 = 0.0;
                    if (!near_part(sa.a, sa.b, sb.a, sb.b, c, t0, t1) ||
                        !near_part(sb.a, sb.b, sa.a, sa.b, c, u0, u1)) {
                        continue;
                    }
                    const double la = sa.to_m - sa.from_m;
                    const double lb = sb.to_m - sb.from_m;
                    Contact found{{sa.from_m + t0 * la, sa.from_m + t1 * la},
                                  {sb.from_m + u0 * lb, sb.from_m + u1 * lb}};
                    bool merged = false;
                    for (Contact &k : contacts) {
                        if (touching(k.on_a, found.on_a) && touching(k.on_b, found.on_b)) {
                            k.on_a = {std::min(k.on_a.from_m, found.on_a.from_m),
                                      std::max(k.on_a.to_m, found.on_a.to_m)};
                            k.on_b = {std::min(k.on_b.from_m, found.on_b.from_m),
                                      std::max(k.on_b.to_m, found.on_b.to_m)};
                            merged = true;
                            break;
                        }
                    }
                    if (!merged) {
                        contacts.push_back(found);
                    }
                }
            }
            // At a strip's end: the contact reaches within the clearance of it.
            const auto at_end = [&](size_t s, const Span &x) {
                return !is_ring(strips[s]) &&
                       (x.from_m <= c + 1e-9 || x.to_m >= arcs[s].back() - c - 1e-9);
            };
            // The end a contact is at.
            const auto end_of = [&](size_t s, const Span &x) {
                return x.from_m <= c + 1e-9 ? strips[s].polyline.front()
                                            : strips[s].polyline.back();
            };
            for (const Contact &k : contacts) {
                const bool a_ends = at_end(a, k.on_a);
                const bool b_ends = at_end(b, k.on_b);
                // An end on the other's corner runs on into the gap there.
                if (a_ends && !on_corner(b, end_of(a, k.on_a))) {
                    cuts[a].push_back(k.on_a);
                }
                if (b_ends && !on_corner(a, end_of(b, k.on_b))) {
                    cuts[b].push_back(k.on_b);
                }
                if (!a_ends && !b_ends) {
                    if (yields(a, b)) {
                        cuts[a].push_back(k.on_a);
                    } else {
                        cuts[b].push_back(k.on_b);
                    }
                }
            }
        }
    }
    std::vector<std::vector<Span>> pieces(n);
    for (size_t i = 0; i < n; ++i) {
        if (strips[i].polyline.size() < 2) {
            continue;
        }
        const double length = arcs[i].back();
        std::sort(cuts[i].begin(), cuts[i].end(),
                  [](const Span &x, const Span &y) { return x.from_m < y.from_m; });
        double at = 0.0;
        for (const Span &cut : cuts[i]) {
            if (cut.from_m > at) {
                pieces[i].push_back({at, std::min(cut.from_m, length)});
            }
            at = std::max(at, cut.to_m);
        }
        if (at < length) {
            pieces[i].push_back({at, length});
        }
        // Ends reaching another strip's corner.
        if (pieces[i].empty() || is_ring(strips[i]) ||
            strips[i].pixels_per_m < kJointMinPixelsPerM) {
            continue;
        }
        for (size_t j = 0; j < n; ++j) {
            if (j == i) {
                continue;
            }
            if (pieces[i].front().from_m <= 1e-9 && on_corner(j, strips[i].polyline.front())) {
                pieces[i].front().from_corner = true;
            }
            if (pieces[i].back().to_m >= length - 1e-9 && on_corner(j, strips[i].polyline.back())) {
                pieces[i].back().to_corner = true;
            }
        }
    }
    return pieces;
}

int pixels_fitting(const std::vector<Span> &pieces, double pixels_per_m) {
    int total = 0;
    for (const Span &p : pieces) {
        // A pixel on a corner end, then half a pitch clear of the other end at least.
        const double extra = p.from_corner || p.to_corner ? 0.5 : 0.0;
        total += std::max(
            0, static_cast<int>(std::floor((p.to_m - p.from_m) * pixels_per_m + extra + 1e-9)));
    }
    return total;
}

void fit_counts(std::vector<StripSpec> &strips) {
    const std::vector<std::vector<Span>> pieces = strip_pieces(strips);
    for (size_t i = 0; i < strips.size(); ++i) {
        if (strips[i].fills) {
            strips[i].count = pixels_fitting(pieces[i], strips[i].pixels_per_m);
        }
    }
}

void infer_fills(std::vector<StripSpec> &strips) {
    const std::vector<std::vector<Span>> pieces = strip_pieces(strips);
    for (size_t i = 0; i < strips.size(); ++i) {
        StripSpec &s = strips[i];
        if (s.polyline.size() < 2) {
            continue;
        }
        const int whole = pixels_fitting({{0.0, arc_lengths(s.polyline).back()}}, s.pixels_per_m);
        const int fits = pixels_fitting(pieces[i], s.pixels_per_m);
        s.fills = s.count <= whole + 1 && s.count >= static_cast<int>(std::floor(0.75 * fits));
    }
}

Rig build_rig(std::vector<StripSpec> strips) {
    Rig rig;
    rig.strips = std::move(strips);
    const std::vector<std::vector<Span>> pieces = strip_pieces(rig.strips);
    constexpr double kBig = std::numeric_limits<double>::max();
    rig.min = {kBig, kBig, kBig};
    rig.max = {-kBig, -kBig, -kBig};
    for (size_t s = 0; s < rig.strips.size(); ++s) {
        const StripSpec &strip = rig.strips[s];
        rig.fits.push_back(pixels_fitting(pieces[s], strip.pixels_per_m));
        if (strip.polyline.size() < 2 || strip.pixels_per_m <= 0.0) {
            continue;
        }
        const double pitch_m = 1.0 / strip.pixels_per_m;
        // Pixels per piece, filling each in turn (the last takes any beyond), and where the first
        // sits: a full piece is centred, its leftover shared between its two ends. Fewer than
        // the pieces hold (a count set by hand) are shared out over them in proportion to their
        // room, each piece's centred: a ring built with a pixel less on every side matches.
        const std::vector<Span> spans =
            pieces[s].empty() ? std::vector<Span>{{0.0, 0.0}} : pieces[s];
        std::vector<int> on(spans.size(), 0);
        std::vector<double> start_m(spans.size(), 0.0);
        const int all_room = pixels_fitting(spans, strip.pixels_per_m);
        if (spans.size() > 1 && strip.count > 0 && strip.count < all_room) {
            std::vector<double> remainder(spans.size(), 0.0);
            int given = 0;
            for (size_t k = 0; k < spans.size(); ++k) {
                const double exact = static_cast<double>(strip.count) *
                                     pixels_fitting({spans[k]}, strip.pixels_per_m) / all_room;
                on[k] = static_cast<int>(std::floor(exact + 1e-9));
                remainder[k] = exact - on[k];
                given += on[k];
            }
            for (; given < strip.count; ++given) { // The rest to the largest remainders.
                const auto most = std::max_element(remainder.begin(), remainder.end());
                ++on[static_cast<size_t>(most - remainder.begin())];
                *most = -1.0;
            }
            for (size_t k = 0; k < spans.size(); ++k) {
                start_m[k] = 0.5 * (spans[k].from_m + spans[k].to_m - on[k] * pitch_m);
            }
        }
        int left = all_room > strip.count && spans.size() > 1 ? 0 : strip.count;
        for (size_t k = 0; left > 0 && k < spans.size(); ++k) {
            const int room = pixels_fitting({spans[k]}, strip.pixels_per_m);
            on[k] = k + 1 == spans.size() ? left : std::min(left, room);
            left -= on[k];
            const double spare_m = spans[k].to_m - spans[k].from_m - room * pitch_m;
            if (spans[k].to_corner && on[k] >= room) {
                start_m[k] = spans[k].to_m - (room - 0.5) * pitch_m; // The last on the corner.
            } else if (spans[k].from_corner) {
                start_m[k] = spans[k].from_m - 0.5 * pitch_m; // The first on the corner.
            } else {
                start_m[k] = spans[k].from_m + (on[k] >= room ? 0.5 * spare_m : 0.0);
            }
        }
        size_t k = 0;
        int in_piece = 0;
        for (int i = 0; i < strip.count; ++i) {
            while (in_piece >= on[k] && k + 1 < spans.size()) {
                ++k;
                in_piece = 0;
            }
            Pixel pixel;
            pixel.position = along(strip.polyline, start_m[k] + (in_piece + 0.5) * pitch_m);
            ++in_piece;
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
    constexpr double kOut = 1.0 / 60.0; // The sides a pitch outside: room for the corners' joints.
    return build_rig({
        {"bottom", {{0.0, 0.0, 0.0}, {kWidth, 0.0, 0.0}}, 60.0, 150, {"frame", "bottom"}},
        {"right",
         {{kWidth + kOut, 0.0, 0.0}, {kWidth + kOut, kHeight, 0.0}},
         60.0,
         72,
         {"frame", "right"}},
        {"top", {{kWidth, kHeight, 0.0}, {0.0, kHeight, 0.0}}, 60.0, 150, {"frame", "top"}},
        {"left", {{-kOut, kHeight, 0.0}, {-kOut, 0.0, 0.0}}, 60.0, 72, {"frame", "left"}},
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
