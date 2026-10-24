#include "io/svg_rig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetresPerPx = 0.0254 / 96.0; // CSS pixels: 96 per inch.
constexpr double kCurveStepM = 0.005;          // Curves become segments at most about this long...
constexpr int kMinCurveSegments = 16;          // ...and at least this many per curve (any scale).
constexpr int kArcSegmentsPerTurn = 64;
constexpr int kMaxCurveSegments = 1024;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

// x' = a x + c y + e, y' = b x + d y + f (SVG's matrix(a b c d e f)).
struct Affine {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;
};

// m after n: n applies first.
Affine operator*(const Affine &m, const Affine &n) {
    return {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b,       m.a * n.c + m.c * n.d,
            m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f};
}

Point apply(const Affine &m, Point p) {
    return {m.a * p.x + m.c * p.y + m.e, m.b * p.x + m.d * p.y + m.f};
}

double distance(Point p, Point q) { return std::hypot(q.x - p.x, q.y - p.y); }

std::string lower(std::string s) {
    for (char &ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string trimmed(std::string_view s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const size_t last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

// ---- XML: just enough of it for drawings: tags and their attributes; text is ignored.

struct Tag {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    bool closing = false;      // </name>
    bool self_closing = false; // <name ... />

    const std::string *attribute(std::string_view key) const {
        for (const auto &[k, v] : attributes) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }
};

// The predefined entities and character references (ASCII only; others stay as written).
std::string decoded(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            const size_t end = s.find(';', i);
            if (end != std::string_view::npos && end - i <= 8) {
                const std::string_view name = s.substr(i + 1, end - i - 1);
                char ch = 0;
                if (name == "amp") {
                    ch = '&';
                } else if (name == "lt") {
                    ch = '<';
                } else if (name == "gt") {
                    ch = '>';
                } else if (name == "quot") {
                    ch = '"';
                } else if (name == "apos") {
                    ch = '\'';
                } else if (name.size() > 1 && name[0] == '#') {
                    const bool hex = name[1] == 'x' || name[1] == 'X';
                    const long code = std::strtol(std::string(name.substr(hex ? 2 : 1)).c_str(),
                                                  nullptr, hex ? 16 : 10);
                    if (code > 0 && code < 128) {
                        ch = static_cast<char>(code);
                    }
                }
                if (ch != 0) {
                    out += ch;
                    i = end;
                    continue;
                }
            }
        }
        out += s[i];
    }
    return out;
}

class XmlReader {
  public:
    explicit XmlReader(std::string_view text) : text_(text) {}

    // The next tag; false at the end of the text (or where it stops making sense).
    bool next(Tag &tag) {
        while (true) {
            const size_t open = text_.find('<', pos_);
            if (open == std::string_view::npos) {
                return false;
            }
            pos_ = open;
            if (starts_with("<!--")) {
                skip_past("-->");
            } else if (starts_with("<?")) {
                skip_past("?>");
            } else if (starts_with("<![CDATA[")) {
                skip_past("]]>");
            } else if (starts_with("<!")) { // DOCTYPE, maybe with an internal subset.
                const size_t bracket = text_.find('[', pos_);
                const size_t close = text_.find('>', pos_);
                if (bracket != std::string_view::npos && bracket < close) {
                    pos_ = bracket;
                    skip_past("]");
                }
                skip_past(">");
            } else {
                return read_tag(tag);
            }
            if (pos_ >= text_.size()) {
                return false;
            }
        }
    }

  private:
    bool starts_with(std::string_view s) const { return text_.substr(pos_, s.size()) == s; }

    void skip_past(std::string_view s) {
        const size_t at = text_.find(s, pos_);
        pos_ = at == std::string_view::npos ? text_.size() : at + s.size();
    }

    void skip_space() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    std::string read_name() {
        const size_t start = pos_;
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '/' || ch == '>' ||
                ch == '=') {
                break;
            }
            ++pos_;
        }
        return std::string(text_.substr(start, pos_ - start));
    }

    bool read_tag(Tag &tag) {
        tag = Tag{};
        ++pos_; // '<'
        if (pos_ < text_.size() && text_[pos_] == '/') {
            tag.closing = true;
            ++pos_;
        }
        tag.name = read_name();
        while (pos_ < text_.size()) {
            skip_space();
            if (pos_ >= text_.size()) {
                return false;
            }
            if (text_[pos_] == '>') {
                ++pos_;
                return !tag.name.empty();
            }
            if (starts_with("/>")) {
                pos_ += 2;
                tag.self_closing = true;
                return !tag.name.empty();
            }
            std::string key = read_name();
            if (key.empty()) {
                ++pos_; // Something unexpected: step over it.
                continue;
            }
            skip_space();
            std::string value;
            if (pos_ < text_.size() && text_[pos_] == '=') {
                ++pos_;
                skip_space();
                if (pos_ < text_.size() && (text_[pos_] == '"' || text_[pos_] == '\'')) {
                    const char quote = text_[pos_++];
                    const size_t end = text_.find(quote, pos_);
                    if (end == std::string_view::npos) {
                        return false;
                    }
                    value = decoded(text_.substr(pos_, end - pos_));
                    pos_ = end + 1;
                } else {
                    value = read_name();
                }
            }
            tag.attributes.emplace_back(std::move(key), std::move(value));
        }
        return false;
    }

    std::string_view text_;
    size_t pos_ = 0;
};

// ---- Numbers, lengths and transforms.

class NumberReader {
  public:
    explicit NumberReader(std::string_view s) : s_(s) {}

    void skip_separators() {
        while (pos_ < s_.size() &&
               (std::isspace(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == ',')) {
            ++pos_;
        }
    }

    bool at_end() {
        skip_separators();
        return pos_ >= s_.size();
    }

    char peek() {
        skip_separators();
        return pos_ < s_.size() ? s_[pos_] : '\0';
    }

    void advance() { ++pos_; }

    // A number such as "-1.5e3" or ".5"; "1.5.5" is two numbers and "1-2" two, as SVG allows.
    std::optional<double> number() {
        skip_separators();
        size_t i = pos_;
        if (i < s_.size() && (s_[i] == '+' || s_[i] == '-')) {
            ++i;
        }
        bool digits = false;
        while (i < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i]))) {
            ++i;
            digits = true;
        }
        if (i < s_.size() && s_[i] == '.') {
            ++i;
            while (i < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i]))) {
                ++i;
                digits = true;
            }
        }
        if (!digits) {
            return std::nullopt;
        }
        if (i < s_.size() && (s_[i] == 'e' || s_[i] == 'E')) {
            size_t j = i + 1;
            if (j < s_.size() && (s_[j] == '+' || s_[j] == '-')) {
                ++j;
            }
            if (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) {
                while (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) {
                    ++j;
                }
                i = j;
            }
        }
        const double value = std::strtod(std::string(s_.substr(pos_, i - pos_)).c_str(), nullptr);
        pos_ = i;
        return value;
    }

    // An arc's flag: one character, "0" or "1", which may run straight into the next number.
    std::optional<bool> flag() {
        skip_separators();
        if (pos_ < s_.size() && (s_[pos_] == '0' || s_[pos_] == '1')) {
            return s_[pos_++] == '1';
        }
        return std::nullopt;
    }

    std::string_view rest() const { return s_.substr(pos_); }

  private:
    std::string_view s_;
    size_t pos_ = 0;
};

// A length in metres per unit: "140mm" is 140 and 0.001. Percentages and unknown units: none.
struct Length {
    double value = 0.0;
    double metres_per_unit = 0.0; // 0: no real unit (pixels are one).
};

std::optional<Length> parse_length(std::string_view s) {
    NumberReader r(s);
    const std::optional<double> value = r.number();
    if (!value) {
        return std::nullopt;
    }
    const std::string unit = lower(trimmed(r.rest()));
    const double per = unit == "mm"                   ? 0.001
                       : unit == "cm"                 ? 0.01
                       : unit == "m"                  ? 1.0
                       : unit == "in"                 ? 0.0254
                       : unit == "pt"                 ? 0.0254 / 72.0
                       : unit == "pc"                 ? 0.0254 / 6.0
                       : unit == "px" || unit.empty() ? kMetresPerPx
                                                      : 0.0;
    return Length{*value, per};
}

Affine parse_transform(std::string_view s) {
    Affine m;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t open = s.find('(', pos);
        const size_t close = s.find(')', open == std::string_view::npos ? pos : open);
        if (open == std::string_view::npos || close == std::string_view::npos) {
            break;
        }
        std::string name = trimmed(s.substr(pos, open - pos));
        if (!name.empty() && name.front() == ',') {
            name = trimmed(name.substr(1));
        }
        NumberReader r(s.substr(open + 1, close - open - 1));
        std::vector<double> v;
        while (const std::optional<double> x = r.number()) {
            v.push_back(*x);
        }
        Affine t;
        if (name == "matrix" && v.size() == 6) {
            t = {v[0], v[1], v[2], v[3], v[4], v[5]};
        } else if (name == "translate" && !v.empty()) {
            t.e = v[0];
            t.f = v.size() > 1 ? v[1] : 0.0;
        } else if (name == "scale" && !v.empty()) {
            t.a = v[0];
            t.d = v.size() > 1 ? v[1] : v[0];
        } else if (name == "rotate" && !v.empty()) {
            const double r_rad = v[0] * kPi / 180.0;
            const Affine rotation{
                std::cos(r_rad), std::sin(r_rad), -std::sin(r_rad), std::cos(r_rad), 0.0, 0.0};
            if (v.size() >= 3) { // About (cx, cy).
                t = Affine{1, 0, 0, 1, v[1], v[2]} * rotation * Affine{1, 0, 0, 1, -v[1], -v[2]};
            } else {
                t = rotation;
            }
        } else if (name == "skewX" && !v.empty()) {
            t.c = std::tan(v[0] * kPi / 180.0);
        } else if (name == "skewY" && !v.empty()) {
            t.b = std::tan(v[0] * kPi / 180.0);
        }
        m = m * t;
        pos = close + 1;
    }
    return m;
}

// ---- Shapes as subpaths of points, already through the element's transform (in metres, y down).

using Subpath = std::vector<Point>;

class PathBuilder {
  public:
    explicit PathBuilder(const Affine &m) : m_(m) {}

    void move_to(Point p) {
        finish();
        start_ = current_ = p;
        points_.push_back(apply(m_, p));
    }
    void line_to(Point p) {
        begin_if_needed();
        current_ = p;
        points_.push_back(apply(m_, p));
    }
    // A cubic Bezier, its control points through the transform (an affine map keeps it a Bezier).
    void cubic_to(Point c1, Point c2, Point p) {
        begin_if_needed();
        const Point a = apply(m_, current_);
        const Point b = apply(m_, c1);
        const Point c = apply(m_, c2);
        const Point d = apply(m_, p);
        const int n =
            segments_for(distance(a, b) + distance(b, c) + distance(c, d), kMinCurveSegments);
        for (int i = 1; i <= n; ++i) {
            const double t = static_cast<double>(i) / n;
            const double u = 1.0 - t;
            points_.push_back(
                {u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                 u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y});
        }
        current_ = p;
    }
    void quadratic_to(Point c, Point p) {
        // As a cubic: control points two thirds of the way to c.
        const Point from = current_;
        cubic_to({from.x + 2.0 / 3.0 * (c.x - from.x), from.y + 2.0 / 3.0 * (c.y - from.y)},
                 {p.x + 2.0 / 3.0 * (c.x - p.x), p.y + 2.0 / 3.0 * (c.y - p.y)}, p);
    }
    // SVG's elliptical arc (endpoint form, SVG 1.1 appendix F.6).
    void arc_to(double rx, double ry, double rotation_deg, bool large, bool sweep, Point p) {
        begin_if_needed();
        const Point p0 = current_;
        rx = std::abs(rx);
        ry = std::abs(ry);
        if (rx == 0.0 || ry == 0.0 || (p0.x == p.x && p0.y == p.y)) {
            line_to(p);
            return;
        }
        const double phi = rotation_deg * kPi / 180.0;
        const double cos_phi = std::cos(phi);
        const double sin_phi = std::sin(phi);
        const double dx = 0.5 * (p0.x - p.x);
        const double dy = 0.5 * (p0.y - p.y);
        const double x1 = cos_phi * dx + sin_phi * dy;
        const double y1 = -sin_phi * dx + cos_phi * dy;
        const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
        if (lambda > 1.0) { // Radii too small: scaled up until the arc fits.
            rx *= std::sqrt(lambda);
            ry *= std::sqrt(lambda);
        }
        const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        double k = std::sqrt(std::max(0.0, num / den));
        if (large == sweep) {
            k = -k;
        }
        const double cx1 = k * rx * y1 / ry;
        const double cy1 = -k * ry * x1 / rx;
        const double cx = cos_phi * cx1 - sin_phi * cy1 + 0.5 * (p0.x + p.x);
        const double cy = sin_phi * cx1 + cos_phi * cy1 + 0.5 * (p0.y + p.y);
        const double theta1 = std::atan2((y1 - cy1) / ry, (x1 - cx1) / rx);
        double delta = std::atan2((-y1 - cy1) / ry, (-x1 - cx1) / rx) - theta1;
        if (sweep && delta < 0.0) {
            delta += 2.0 * kPi;
        } else if (!sweep && delta > 0.0) {
            delta -= 2.0 * kPi;
        }
        // Segments by the arc's length after the transform (its radius, scaled roughly).
        const double scale = std::sqrt(std::abs(m_.a * m_.d - m_.b * m_.c));
        const int n = segments_for(
            std::abs(delta) * std::max(rx, ry) * scale,
            static_cast<int>(std::ceil(std::abs(delta) / (2.0 * kPi) * kArcSegmentsPerTurn)));
        for (int i = 1; i <= n; ++i) {
            const double t = theta1 + delta * i / n;
            const double ex = rx * std::cos(t);
            const double ey = ry * std::sin(t);
            const Point q =
                i == n ? p
                       : Point{cos_phi * ex - sin_phi * ey + cx, sin_phi * ex + cos_phi * ey + cy};
            points_.push_back(apply(m_, q));
        }
        current_ = p;
    }
    void close() {
        if (!points_.empty()) {
            const Point first = points_.front();
            if (distance(first, points_.back()) > 1e-12) {
                points_.push_back(first);
            }
        }
        finish();
        current_ = start_;
    }
    Point current() const { return current_; }

    std::vector<Subpath> take() {
        finish();
        return std::move(done_);
    }

  private:
    static int segments_for(double length_m, int minimum) {
        return std::clamp(static_cast<int>(std::ceil(length_m / kCurveStepM)), minimum,
                          kMaxCurveSegments);
    }
    void begin_if_needed() {
        if (points_.empty()) {
            start_ = current_;
            points_.push_back(apply(m_, current_));
        }
    }
    void finish() {
        if (points_.size() >= 2) {
            done_.push_back(std::move(points_));
        }
        points_.clear();
    }

    Affine m_;
    Point start_;
    Point current_;
    Subpath points_;
    std::vector<Subpath> done_;
};

// Path data ("M 0 0 L 10 0 Z ..."), all commands, absolute and relative.
std::vector<Subpath> path_subpaths(std::string_view d, const Affine &m) {
    PathBuilder b(m);
    NumberReader r(d);
    char command = 0;
    Point last_control; // Of the last cubic (S) or quadratic (T) segment.
    char last_command = 0;
    while (!r.at_end()) {
        const char ch = r.peek();
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            command = ch;
            r.advance();
        } else if (command == 0) {
            break; // Numbers before any command.
        }
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const Point at = b.current();
        const auto point = [&](std::optional<double> x, std::optional<double> y) {
            return Point{*x + (relative ? at.x : 0.0), *y + (relative ? at.y : 0.0)};
        };
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        bool ok = true;
        if (upper == 'Z') {
            b.close();
            last_command = 'Z';
            command = 0; // A number after Z is an error; another command must follow.
            continue;
        }
        if (upper == 'M' || upper == 'L' || upper == 'T') {
            const auto x = r.number();
            const auto y = r.number();
            ok = x && y;
            if (ok) {
                const Point p = point(x, y);
                if (upper == 'M') {
                    b.move_to(p);
                    command = relative ? 'l' : 'L'; // Further pairs are lines.
                } else if (upper == 'L') {
                    b.line_to(p);
                } else {
                    const Point c =
                        last_command == 'Q' || last_command == 'T'
                            ? Point{2 * at.x - last_control.x, 2 * at.y - last_control.y}
                            : at;
                    b.quadratic_to(c, p);
                    last_control = c;
                }
            }
        } else if (upper == 'H' || upper == 'V') {
            const auto v = r.number();
            ok = v.has_value();
            if (ok) {
                b.line_to(upper == 'H' ? Point{*v + (relative ? at.x : 0.0), at.y}
                                       : Point{at.x, *v + (relative ? at.y : 0.0)});
            }
        } else if (upper == 'C') {
            const auto x1 = r.number(), y1 = r.number(), x2 = r.number(), y2 = r.number(),
                       x = r.number(), y = r.number();
            ok = x1 && y1 && x2 && y2 && x && y;
            if (ok) {
                const Point c2 = point(x2, y2);
                b.cubic_to(point(x1, y1), c2, point(x, y));
                last_control = c2;
            }
        } else if (upper == 'S') {
            const auto x2 = r.number(), y2 = r.number(), x = r.number(), y = r.number();
            ok = x2 && y2 && x && y;
            if (ok) {
                const Point c1 = last_command == 'C' || last_command == 'S'
                                     ? Point{2 * at.x - last_control.x, 2 * at.y - last_control.y}
                                     : at;
                const Point c2 = point(x2, y2);
                b.cubic_to(c1, c2, point(x, y));
                last_control = c2;
            }
        } else if (upper == 'Q') {
            const auto x1 = r.number(), y1 = r.number(), x = r.number(), y = r.number();
            ok = x1 && y1 && x && y;
            if (ok) {
                const Point c = point(x1, y1);
                b.quadratic_to(c, point(x, y));
                last_control = c;
            }
        } else if (upper == 'A') {
            const auto rx = r.number(), ry = r.number(), rotation = r.number();
            const auto large = r.flag();
            const auto sweep = r.flag();
            const auto x = r.number(), y = r.number();
            ok = rx && ry && rotation && large && sweep && x && y;
            if (ok) {
                b.arc_to(*rx, *ry, *rotation, *large, *sweep, point(x, y));
            }
        } else {
            ok = false; // An unknown command.
        }
        if (!ok) {
            break; // Bad path data: keep what came before, as browsers do.
        }
        last_command = upper == 'M' ? 'L' : upper;
    }
    return b.take();
}

double number_attribute(const Tag &tag, std::string_view key) {
    const std::string *v = tag.attribute(key);
    if (v == nullptr) {
        return 0.0;
    }
    NumberReader r(*v);
    return r.number().value_or(0.0);
}

std::vector<Subpath> points_subpaths(const Tag &tag, const Affine &m, bool closed) {
    const std::string *v = tag.attribute("points");
    if (v == nullptr) {
        return {};
    }
    PathBuilder b(m);
    NumberReader r(*v);
    bool first = true;
    while (true) {
        const auto x = r.number();
        const auto y = r.number();
        if (!x || !y) {
            break;
        }
        if (first) {
            b.move_to({*x, *y});
            first = false;
        } else {
            b.line_to({*x, *y});
        }
    }
    if (closed) {
        b.close();
    }
    return b.take();
}

std::vector<Subpath> ellipse_subpaths(double cx, double cy, double rx, double ry, const Affine &m) {
    if (rx <= 0.0 || ry <= 0.0) {
        return {};
    }
    // Two half arcs from the rightmost point, clockwise on screen (y down), as browsers draw.
    PathBuilder b(m);
    b.move_to({cx + rx, cy});
    b.arc_to(rx, ry, 0.0, false, true, {cx - rx, cy});
    b.arc_to(rx, ry, 0.0, false, true, {cx + rx, cy});
    b.close();
    return b.take();
}

std::vector<Subpath> shape_subpaths(const Tag &tag, const Affine &m) {
    const std::string &n = tag.name;
    if (n == "path") {
        const std::string *d = tag.attribute("d");
        return d != nullptr ? path_subpaths(*d, m) : std::vector<Subpath>{};
    }
    if (n == "line") {
        PathBuilder b(m);
        b.move_to({number_attribute(tag, "x1"), number_attribute(tag, "y1")});
        b.line_to({number_attribute(tag, "x2"), number_attribute(tag, "y2")});
        return b.take();
    }
    if (n == "polyline" || n == "polygon") {
        return points_subpaths(tag, m, n == "polygon");
    }
    if (n == "rect") {
        const double x = number_attribute(tag, "x");
        const double y = number_attribute(tag, "y");
        const double w = number_attribute(tag, "width");
        const double h = number_attribute(tag, "height");
        if (w <= 0.0 || h <= 0.0) {
            return {};
        }
        PathBuilder b(m);
        b.move_to({x, y});
        b.line_to({x + w, y});
        b.line_to({x + w, y + h});
        b.line_to({x, y + h});
        b.close();
        return b.take();
    }
    if (n == "circle") {
        const double r = number_attribute(tag, "r");
        return ellipse_subpaths(number_attribute(tag, "cx"), number_attribute(tag, "cy"), r, r, m);
    }
    if (n == "ellipse") {
        return ellipse_subpaths(number_attribute(tag, "cx"), number_attribute(tag, "cy"),
                                number_attribute(tag, "rx"), number_attribute(tag, "ry"), m);
    }
    return {};
}

bool hidden(const Tag &tag) {
    if (const std::string *display = tag.attribute("display");
        display && trimmed(*display) == "none") {
        return true;
    }
    if (const std::string *style = tag.attribute("style")) {
        std::string s;
        for (const char ch : *style) {
            if (!std::isspace(static_cast<unsigned char>(ch))) {
                s += ch;
            }
        }
        return s.find("display:none") != std::string::npos;
    }
    return false;
}

bool skipped_subtree(const std::string &name) {
    for (const char *n : {"defs", "clipPath", "mask", "pattern", "marker", "symbol", "metadata",
                          "sodipodi:namedview", "title", "desc", "style", "script", "text", "image",
                          "use", "linearGradient", "radialGradient", "filter", "foreignObject"}) {
        if (name == n) {
            return true;
        }
    }
    return false;
}

struct Frame {
    std::string name;
    Affine m;
    std::vector<std::string> groups;
    bool skip = false;
};

struct Shape {
    std::string name;
    std::vector<std::string> groups;
    Subpath points; // Metres, y down, at the document's size.
};

double polyline_length(const std::vector<Vec3> &points) {
    double length_m = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        length_m += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    }
    return length_m;
}

} // namespace

Result<SvgRig> rig_from_svg(std::string_view svg, const SvgRigOptions &options) {
    XmlReader reader(svg);
    std::vector<Frame> stack;
    std::vector<Shape> shapes;
    bool found_svg = false;
    int unnamed = 0;
    Tag tag;
    while (reader.next(tag)) {
        if (tag.closing) {
            if (!stack.empty()) {
                stack.pop_back();
            }
            continue;
        }
        Frame frame;
        frame.name = tag.name;
        if (!stack.empty()) {
            frame.m = stack.back().m;
            frame.groups = stack.back().groups;
            frame.skip = stack.back().skip;
        }
        frame.skip = frame.skip || skipped_subtree(tag.name) || hidden(tag);
        if (!frame.skip) {
            if (tag.name == "svg" && !found_svg) {
                // The root: its viewBox in real units, so the drawing is in metres from here on.
                found_svg = true;
                double sx = kMetresPerPx;
                double sy = kMetresPerPx;
                double vx = 0.0;
                double vy = 0.0;
                const std::string *view_box = tag.attribute("viewBox");
                const std::optional<Length> w =
                    tag.attribute("width") ? parse_length(*tag.attribute("width")) : std::nullopt;
                const std::optional<Length> h =
                    tag.attribute("height") ? parse_length(*tag.attribute("height")) : std::nullopt;
                if (view_box != nullptr) {
                    NumberReader r(*view_box);
                    const auto x = r.number(), y = r.number(), vw = r.number(), vh = r.number();
                    if (x && y && vw && vh && *vw > 0.0 && *vh > 0.0) {
                        vx = *x;
                        vy = *y;
                        if (w && w->metres_per_unit > 0.0) {
                            sx = w->value * w->metres_per_unit / *vw;
                        }
                        if (h && h->metres_per_unit > 0.0) {
                            sy = h->value * h->metres_per_unit / *vh;
                        }
                        if (!(h && h->metres_per_unit > 0.0)) {
                            sy = sx; // Width only: keep the aspect.
                        }
                        if (!(w && w->metres_per_unit > 0.0)) {
                            sx = sy;
                        }
                    }
                }
                frame.m = frame.m * Affine{sx, 0, 0, sy, -vx * sx, -vy * sy};
            }
            if (const std::string *t = tag.attribute("transform")) {
                frame.m = frame.m * parse_transform(*t);
            }
            const std::string *label = tag.attribute("inkscape:label");
            if (tag.name == "g" && label != nullptr) {
                const std::string group = lower(trimmed(*label));
                if (!group.empty() && std::find(frame.groups.begin(), frame.groups.end(), group) ==
                                          frame.groups.end()) {
                    frame.groups.push_back(group);
                }
            }
            std::vector<Subpath> subpaths = shape_subpaths(tag, frame.m);
            if (!subpaths.empty()) {
                std::string name;
                if (label != nullptr && !trimmed(*label).empty()) {
                    name = trimmed(*label);
                } else if (const std::string *id = tag.attribute("id"); id && !id->empty()) {
                    name = *id;
                } else {
                    name = "Path " + std::to_string(++unnamed);
                }
                for (size_t i = 0; i < subpaths.size(); ++i) {
                    shapes.push_back(
                        {subpaths.size() == 1 ? name : name + " " + std::to_string(i + 1),
                         frame.groups, std::move(subpaths[i])});
                }
            }
        }
        if (!tag.self_closing) {
            stack.push_back(std::move(frame));
        }
    }
    if (!found_svg) {
        return Error{"Not an SVG drawing (no <svg> element)."};
    }
    std::vector<DrawingShape> drawn;
    for (Shape &shape : shapes) {
        DrawingShape d{std::move(shape.name), std::move(shape.groups), {}};
        for (const Point &q : shape.points) {
            d.points.push_back({q.x, -q.y, 0.0}); // SVG's y runs down.
        }
        drawn.push_back(std::move(d));
    }
    return rig_from_shapes(std::move(drawn), options);
}

namespace {

constexpr double kCentreLineStepM = 0.005; // The outer outline resampled this finely.

bool closed_shape(const DrawingShape &s) {
    return s.points.size() > 3 && std::hypot(s.points.front().x - s.points.back().x,
                                             s.points.front().y - s.points.back().y) < 1e-6;
}

// The nearest point to p on the polyline.
Vec3 nearest_on(const std::vector<Vec3> &line, const Vec3 &p) {
    Vec3 best = line.front();
    double best_d = std::numeric_limits<double>::infinity();
    for (size_t i = 1; i < line.size(); ++i) {
        const Vec3 &a = line[i - 1];
        const Vec3 &b = line[i];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        const double t =
            len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
        const Vec3 q{a.x + t * dx, a.y + t * dy, 0.0};
        const double d = std::hypot(q.x - p.x, q.y - p.y);
        if (d < best_d) {
            best_d = d;
            best = q;
        }
    }
    return best;
}

bool inside(const std::vector<Vec3> &polygon, const Vec3 &p) {
    bool in = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec3 &a = polygon[i];
        const Vec3 &b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            in = !in;
        }
    }
    return in;
}

std::vector<Vec3> resampled(const std::vector<Vec3> &line, double step_m) {
    std::vector<Vec3> out{line.front()};
    for (size_t i = 1; i < line.size(); ++i) {
        const Vec3 &a = line[i - 1];
        const Vec3 &b = line[i];
        const int n =
            std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / step_m)));
        for (int k = 1; k <= n; ++k) {
            const double t = static_cast<double>(k) / n;
            out.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), 0.0});
        }
    }
    return out;
}

// Pairs of nested closed outlines with a steady gap become one centre line; returns the pairs.
int merge_centre_lines(std::vector<DrawingShape> &shapes, double max_width_m) {
    struct Pairing {
        double gap_m;
        size_t outer;
        size_t inner;
    };
    std::vector<Pairing> pairings;
    for (size_t a = 0; a < shapes.size(); ++a) {
        if (!closed_shape(shapes[a])) {
            continue;
        }
        for (size_t b = 0; b < shapes.size(); ++b) {
            if (b == a || !closed_shape(shapes[b]) ||
                !inside(shapes[a].points, shapes[b].points.front())) {
                continue;
            }
            double sum = 0.0;
            double sum2 = 0.0;
            bool all_in = true;
            for (const Vec3 &p : shapes[b].points) {
                all_in = all_in && inside(shapes[a].points, p);
                const Vec3 q = nearest_on(shapes[a].points, p);
                const double d = std::hypot(q.x - p.x, q.y - p.y);
                sum += d;
                sum2 += d * d;
            }
            const double n = static_cast<double>(shapes[b].points.size());
            const double mean = sum / n;
            const double sd = std::sqrt(std::max(0.0, sum2 / n - mean * mean));
            if (all_in && mean > 0.0 && mean <= max_width_m && sd <= 0.25 * mean + 0.002) {
                pairings.push_back({mean, a, b});
            }
        }
    }
    std::sort(pairings.begin(), pairings.end(),
              [](const Pairing &x, const Pairing &y) { return x.gap_m < y.gap_m; });
    std::vector<bool> used(shapes.size(), false);
    std::vector<bool> remove(shapes.size(), false);
    int merged = 0;
    for (const Pairing &p : pairings) {
        if (used[p.outer] || used[p.inner]) {
            continue;
        }
        used[p.outer] = used[p.inner] = true;
        std::vector<Vec3> middle;
        for (const Vec3 &q : resampled(shapes[p.outer].points, kCentreLineStepM)) {
            const Vec3 r = nearest_on(shapes[p.inner].points, q);
            middle.push_back({0.5 * (q.x + r.x), 0.5 * (q.y + r.y), 0.0});
        }
        middle.back() = middle.front(); // Still closed.
        shapes[p.outer].points = std::move(middle);
        remove[p.inner] = true;
        ++merged;
    }
    std::vector<DrawingShape> kept;
    for (size_t i = 0; i < shapes.size(); ++i) {
        if (!remove[i]) {
            kept.push_back(std::move(shapes[i]));
        }
    }
    shapes = std::move(kept);
    return merged;
}

} // namespace

Result<SvgRig> rig_from_shapes(std::vector<DrawingShape> shapes, const SvgRigOptions &options) {
    shapes.erase(std::remove_if(shapes.begin(), shapes.end(),
                                [](const DrawingShape &s) { return s.points.size() < 2; }),
                 shapes.end());
    const int centre_lines =
        options.centre_lines ? merge_centre_lines(shapes, options.ring_width_m) : 0;
    if (shapes.empty()) {
        return Error{"The drawing has no visible paths or shapes."};
    }
    double min_x = shapes.front().points.front().x;
    double max_x = min_x;
    double min_y = shapes.front().points.front().y;
    double max_y = min_y;
    for (const DrawingShape &s : shapes) {
        for (const Vec3 &p : s.points) {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
    }
    SvgRig rig;
    rig.centre_lines = centre_lines;
    rig.natural_width_m = max_x - min_x;
    const double k = options.width_m > 0.0 && rig.natural_width_m > 0.0
                         ? options.width_m / rig.natural_width_m
                         : 1.0;
    rig.width_m = rig.natural_width_m * k;
    rig.height_m = (max_y - min_y) * k;
    const double centre_x = 0.5 * (min_x + max_x);
    for (DrawingShape &s : shapes) {
        StripSpec strip;
        strip.name = std::move(s.name);
        strip.pixels_per_m = options.pixels_per_m;
        double lo = 0.0;
        double hi = 0.0;
        for (size_t i = 0; i < s.points.size(); ++i) {
            const Vec3 p{(s.points[i].x - centre_x) * k,
                         options.base_m + (s.points[i].y - min_y) * k, 0.0};
            lo = i == 0 ? p.x : std::min(lo, p.x);
            hi = i == 0 ? p.x : std::max(hi, p.x);
            strip.polyline.push_back(p);
        }
        strip.count = static_cast<int>(
            std::floor(polyline_length(strip.polyline) * options.pixels_per_m + 1e-9));
        if (strip.count <= 0) {
            ++rig.too_short;
            continue;
        }
        strip.groups = std::move(s.groups);
        const double middle = 0.5 * (lo + hi);
        for (const char *side : {"left", "right"}) {
            const bool on_side = side[0] == 'l' ? middle <= 1e-9 : middle >= -1e-9;
            if (on_side &&
                std::find(strip.groups.begin(), strip.groups.end(), side) == strip.groups.end()) {
                strip.groups.emplace_back(side);
            }
        }
        rig.strips.push_back(std::move(strip));
    }
    if (rig.strips.empty()) {
        return Error{"Every path in the drawing is shorter than one pixel."};
    }
    return rig;
}

} // namespace psyight
