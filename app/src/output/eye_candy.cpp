#include "output/eye_candy.h"

#include "engine/aurora.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

enum Show { hunt, billiards, bouncing, meteors, fireworks, ocean, lava, spiral, aurora };

constexpr std::array<const char *, kEyeCandyCount> kNames = {
    "Hunt",  "Billiards", "Bouncing balls", "Meteors", "Fireworks",
    "Ocean", "Lava lamp", "Spiral",         "Aurora"};
constexpr std::array<const char *, kEyeCandyCount> kDescriptions = {
    "A little game: a hunter chases five prey round the rig, nearest first, each caught in a "
    "burst of its colour; the prey blink faster as it closes in. A new round every 10 s.",
    "Four balls bouncing round the rig's box with fading trails, flaring white where two meet.",
    "Balls thrown up each strip, falling back under gravity.",
    "Meteors running along each strip, their trails breaking up into sparks.",
    "Shells rising from the bottom and bursting into drooping rings of rays.",
    "Layers of slow waves in deep blues and teals, white where they crest.",
    "Warm blobs drifting, merging and parting, as in a lava lamp.",
    "A colour wheel turning about the centre, twisted into a spiral, with rings breathing out.",
    "The Ambient step's aurora: soft waves drifting along each strip, fading in and out."};

struct V2 {
    double x = 0.0;
    double y = 0.0;
};

double length(V2 a, V2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::uint64_t mix(std::uint64_t x) { // splitmix64's finaliser.
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

// 0 to 1, from up to three numbers.
double unit(long long a, long long b = 0, long long c = 0) {
    std::uint64_t h = mix(static_cast<std::uint64_t>(a));
    h = mix(h ^ static_cast<std::uint64_t>(b));
    h = mix(h ^ static_cast<std::uint64_t>(c));
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

double bump(double d, double width) { return std::exp(-(d / width) * (d / width)); }

double smoothstep(double lo, double hi, double x) {
    const double t = std::clamp((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void add(Rgb &to, Rgb c, double k) {
    to.r += static_cast<float>(c.r * k);
    to.g += static_cast<float>(c.g * k);
    to.b += static_cast<float>(c.b * k);
}

// The rig about its centre in units of its larger half-size: x and y within -1 to 1, and the
// box's half-sizes in those units.
struct Space {
    std::vector<V2> at; // Per pixel.
    double hx = 1.0;
    double hy = 1.0;
};

Space space_of(const Rig &rig) {
    Space s;
    const double cx = 0.5 * (rig.min.x + rig.max.x);
    const double cy = 0.5 * (rig.min.y + rig.max.y);
    const double half_x = 0.5 * (rig.max.x - rig.min.x);
    const double half_y = 0.5 * (rig.max.y - rig.min.y);
    const double r = std::max({half_x, half_y, 1e-6});
    s.hx = std::max(half_x / r, 0.05);
    s.hy = std::max(half_y / r, 0.05);
    s.at.reserve(rig.pixels.size());
    for (const Pixel &p : rig.pixels) {
        s.at.push_back({(p.position.x - cx) / r, (p.position.y - cy) / r});
    }
    return s;
}

// --- Hunt -----------------------------------------------------------------------------------

constexpr double kRoundS = 10.0;
constexpr double kHuntS = 8.5; // The chase; then the hunter fades over the rest of the round.
constexpr int kPrey = 5;

void draw_hunt(const Rig &rig, const Space &s, double t, Frame &out) {
    const size_t n = rig.pixels.size();
    const auto round = static_cast<long long>(std::floor(t / kRoundS));
    const double tau = t - static_cast<double>(round) * kRoundS;
    const auto pick = [&](int which) {
        return s.at[std::min(n - 1, static_cast<size_t>(unit(round, which, 1) * n))];
    };
    std::array<V2, kPrey> prey;
    std::array<Rgb, kPrey> colour;
    for (int j = 0; j < kPrey; ++j) {
        prey[static_cast<size_t>(j)] = pick(j);
        colour[static_cast<size_t>(j)] = hsv(360.0 * unit(round, j, 2), 1.0, 1.0);
    }
    // The hunter's path: nearest prey first.
    std::array<int, kPrey> order{};
    std::array<double, kPrey> caught_s{}; // By prey.
    std::array<bool, kPrey> taken{};
    std::array<V2, kPrey + 1> path;
    path[0] = pick(99);
    std::array<double, kPrey + 1> along{}; // Path length to each corner.
    for (int step = 0; step < kPrey; ++step) {
        int best = -1;
        for (int j = 0; j < kPrey; ++j) {
            if (!taken[static_cast<size_t>(j)] &&
                (best < 0 ||
                 length(path[static_cast<size_t>(step)], prey[static_cast<size_t>(j)]) <
                     length(path[static_cast<size_t>(step)], prey[static_cast<size_t>(best)]))) {
                best = j;
            }
        }
        taken[static_cast<size_t>(best)] = true;
        order[static_cast<size_t>(step)] = best;
        path[static_cast<size_t>(step) + 1] = prey[static_cast<size_t>(best)];
        along[static_cast<size_t>(step) + 1] =
            along[static_cast<size_t>(step)] +
            length(path[static_cast<size_t>(step)], path[static_cast<size_t>(step) + 1]);
    }
    const double speed = std::max(along[kPrey], 1e-6) / kHuntS;
    for (int step = 0; step < kPrey; ++step) {
        caught_s[static_cast<size_t>(order[static_cast<size_t>(step)])] =
            along[static_cast<size_t>(step) + 1] / speed;
    }
    const auto hunter_at = [&](double when) {
        const double d = std::clamp(when, 0.0, kHuntS) * speed;
        for (size_t k = 0; k < kPrey; ++k) {
            if (d <= along[k + 1]) {
                const double seg = std::max(along[k + 1] - along[k], 1e-9);
                const double f = (d - along[k]) / seg;
                return V2{path[k].x + f * (path[k + 1].x - path[k].x),
                          path[k].y + f * (path[k + 1].y - path[k].y)};
            }
        }
        return path[kPrey];
    };
    constexpr int kTrail = 8;
    std::array<V2, kTrail + 1> hunter;
    for (int m = 0; m <= kTrail; ++m) {
        hunter[static_cast<size_t>(m)] = hunter_at(tau - 0.06 * m);
    }
    const double fade =
        tau < kHuntS ? 1.0 : std::max(0.0, 1.0 - (tau - kHuntS) / (kRoundS - kHuntS));
    for (size_t i = 0; i < n; ++i) {
        const V2 q = s.at[i];
        Rgb c;
        for (size_t j = 0; j < kPrey; ++j) {
            const double d = length(q, prey[j]);
            if (tau < caught_s[j]) {
                // Nervous as the hunter closes in: faster blinking.
                const double nervous = std::clamp(1.0 - length(hunter[0], prey[j]) / 0.6, 0.0, 1.0);
                const double blink =
                    0.55 + 0.45 * std::sin(2.0 * kPi * (1.2 + 5.0 * nervous) * tau + j);
                add(c, colour[j], bump(d, 0.07) * blink);
            } else if (const double age = tau - caught_s[j]; age < 0.9) {
                add(c, colour[j], bump(d - 0.8 * age / 0.9, 0.07) * (1.0 - age / 0.9));
            }
        }
        add(c, {1.0f, 0.9f, 0.7f}, bump(length(q, hunter[0]), 0.08) * fade);
        for (int m = 1; m <= kTrail; ++m) {
            add(c, {1.0f, 0.35f, 0.05f},
                bump(length(q, hunter[static_cast<size_t>(m)]), 0.06) * std::pow(0.75, m) * fade);
        }
        out[i] = c;
    }
}

// --- Billiards -----------------------------------------------------------------------------

double fold(double v) { // Bouncing between 0 and 1.
    const double f = v - 2.0 * std::floor(v / 2.0);
    return f < 1.0 ? f : 2.0 - f;
}

void draw_billiards(const Space &s, double t, Frame &out) {
    constexpr int kBalls = 4;
    constexpr int kTrail = 10;
    const auto ball_at = [&](int b, double when) {
        const double vx = (0.18 + 0.2 * unit(b, 3)) * (unit(b, 5) < 0.5 ? -1.0 : 1.0);
        const double vy = (0.15 + 0.2 * unit(b, 4)) * (unit(b, 6) < 0.5 ? -1.0 : 1.0);
        return V2{s.hx * (2.0 * fold(unit(b, 1) + vx * when) - 1.0),
                  s.hy * (2.0 * fold(unit(b, 2) + vy * when) - 1.0)};
    };
    std::array<std::array<V2, kTrail + 1>, kBalls> balls;
    std::array<double, kBalls> flare{};
    for (int b = 0; b < kBalls; ++b) {
        for (int m = 0; m <= kTrail; ++m) {
            balls[static_cast<size_t>(b)][static_cast<size_t>(m)] = ball_at(b, t - 0.05 * m);
        }
    }
    for (size_t a = 0; a < kBalls; ++a) {
        for (size_t b = a + 1; b < kBalls; ++b) {
            const double near = std::max(0.0, 1.0 - length(balls[a][0], balls[b][0]) / 0.25);
            flare[a] = std::max(flare[a], near);
            flare[b] = std::max(flare[b], near);
        }
    }
    for (size_t i = 0; i < s.at.size(); ++i) {
        Rgb c;
        for (size_t b = 0; b < kBalls; ++b) {
            const Rgb colour = hsv(20.0 + 90.0 * static_cast<double>(b), 1.0, 1.0);
            add(c, colour, bump(length(s.at[i], balls[b][0]), 0.1));
            add(c, {1.0f, 1.0f, 1.0f}, bump(length(s.at[i], balls[b][0]), 0.14) * flare[b]);
            for (int m = 1; m <= kTrail; ++m) {
                add(c, colour,
                    bump(length(s.at[i], balls[b][static_cast<size_t>(m)]), 0.07) *
                        std::pow(0.72, m));
            }
        }
        out[i] = c;
    }
}

// --- Along each strip ----------------------------------------------------------------------

void draw_bouncing(const Rig &rig, double t, Frame &out) {
    constexpr int kBalls = 3;
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        const Pixel &p = rig.pixels[i];
        const int count = std::max(1, rig.strips[static_cast<size_t>(p.strip)].count);
        Rgb c;
        for (int b = 0; b < kBalls; ++b) {
            const double period_s = 1.1 + 1.4 * unit(p.strip, b, 0);
            const double x = t / period_s + unit(p.strip, b, 1);
            const double f = x - std::floor(x);
            const double height = 4.0 * f * (1.0 - f) * (0.55 + 0.45 * unit(p.strip, b, 3));
            const double at = height * (count - 1);
            add(c, hsv(360.0 * unit(p.strip, b, 2), 1.0, 1.0), bump(p.index - at, 1.3));
        }
        out[i] = c;
    }
}

void draw_meteors(const Rig &rig, double t, Frame &out) {
    constexpr int kMeteors = 2;
    const auto frame_step = static_cast<long long>(std::floor(t * 14.0)); // Spark flicker.
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        const Pixel &p = rig.pixels[i];
        const int count = std::max(1, rig.strips[static_cast<size_t>(p.strip)].count);
        Rgb c;
        for (int m = 0; m < kMeteors; ++m) {
            const double speed = 18.0 + 25.0 * unit(p.strip, m, 0); // Pixels a second.
            const double tail = 8.0 + 12.0 * unit(p.strip, m, 1);
            const double period = count + tail;
            const double head = std::fmod(t * speed + unit(p.strip, m, 2) * period, period);
            const double behind = head - p.index;
            if (behind < -1.0 || behind > tail) {
                continue;
            }
            const double spark =
                behind < 2.0 || unit(static_cast<long long>(i), frame_step, m) > 0.3 ? 1.0 : 0.2;
            const double k = (behind < 0.0 ? 1.0 + behind : std::pow(0.82, behind)) * spark;
            add(c,
                behind < 1.5 ? Rgb{1.0f, 0.95f, 0.85f} : hsv(360.0 * unit(p.strip, m, 3), 0.9, 1.0),
                k);
        }
        out[i] = c;
    }
}

// --- Over the rig's plane ------------------------------------------------------------------

void draw_fireworks(const Space &s, double t, Frame &out) {
    constexpr double kEveryS = 0.8;
    constexpr double kRiseS = 0.7;
    constexpr double kBurstS = 1.5;
    const auto first = static_cast<long long>(std::floor((t - kRiseS - kBurstS) / kEveryS)) - 1;
    const auto last = static_cast<long long>(std::floor(t / kEveryS));
    const auto flicker = static_cast<long long>(std::floor(t * 20.0));
    for (size_t i = 0; i < s.at.size(); ++i) {
        out[i] = Rgb{};
    }
    for (long long k = std::max(0ll, first); k <= last; ++k) {
        const double tau = t - (static_cast<double>(k) * kEveryS + 0.3 * unit(k, 0));
        if (tau < 0.0 || tau >= kRiseS + kBurstS) {
            continue;
        }
        const V2 burst{s.hx * (1.4 * unit(k, 1) - 0.7), s.hy * (0.7 * unit(k, 2))};
        const Rgb colour = hsv(360.0 * unit(k, 4), 0.8, 1.0);
        if (tau < kRiseS) { // The rocket, slowing as it rises.
            const double f = tau / kRiseS;
            const double y = -s.hy + (burst.y + s.hy) * (1.0 - (1.0 - f) * (1.0 - f));
            for (size_t i = 0; i < s.at.size(); ++i) {
                add(out[i], {1.0f, 0.6f, 0.2f}, bump(length(s.at[i], {burst.x, y}), 0.05));
            }
            continue;
        }
        const double age = tau - kRiseS;
        const double radius = 0.6 * (1.0 - std::exp(-3.5 * age));
        const V2 centre{burst.x, burst.y - 0.2 * age * age}; // Drooping.
        const double fade = std::pow(1.0 - age / kBurstS, 1.5);
        const double turn = 2.0 * kPi * unit(k, 3);
        for (size_t i = 0; i < s.at.size(); ++i) {
            const V2 q = s.at[i];
            const double d = length(q, centre);
            const double angle = std::atan2(q.y - centre.y, q.x - centre.x);
            const double rays = std::pow(0.5 + 0.5 * std::cos(10.0 * angle + turn), 6.0);
            const double twinkle =
                age < 0.8 || unit(static_cast<long long>(i), flicker, k) > 0.4 ? 1.0 : 0.2;
            add(out[i], colour, bump(d - radius, 0.07) * (0.3 + 0.7 * rays) * fade * twinkle);
            if (age < 0.25) {
                add(out[i], {1.0f, 1.0f, 1.0f}, bump(d, 0.15) * (1.0 - age / 0.25));
            }
        }
    }
}

void draw_ocean(const Space &s, double t, Frame &out) {
    const Rgb deep{0.0f, 0.04f, 0.2f};
    const Rgb teal{0.0f, 0.45f, 0.55f};
    const Rgb crest{0.7f, 0.9f, 1.0f};
    const double swell = 0.8 + 0.2 * std::sin(2.0 * kPi * t / 11.0);
    for (size_t i = 0; i < s.at.size(); ++i) {
        const V2 q = s.at[i];
        const double w1 = std::sin(2.0 * kPi * (1.1 * (0.8 * q.x + 0.6 * q.y) - 0.13 * t));
        const double w2 = std::sin(2.0 * kPi * (1.7 * (-0.5 * q.x + 0.85 * q.y) - 0.21 * t) + 1.3);
        const double w3 = std::sin(2.0 * kPi * (2.6 * (0.95 * q.x - 0.3 * q.y) + 0.17 * t) + 2.1);
        const double sea = (w1 + 0.7 * w2 + 0.5 * w3) / 2.2; // -1 to 1.
        Rgb c = deep;
        add(c, teal, smoothstep(-0.2, 0.6, sea));
        add(c, crest, smoothstep(0.7, 0.95, sea));
        out[i] = scaled(c, swell);
    }
}

void draw_lava(const Space &s, double t, Frame &out) {
    constexpr int kBlobs = 4;
    const std::array<Rgb, kBlobs> colours = {Rgb{1.0f, 0.2f, 0.0f}, Rgb{1.0f, 0.0f, 0.35f},
                                             Rgb{1.0f, 0.5f, 0.0f}, Rgb{0.9f, 0.0f, 0.6f}};
    std::array<V2, kBlobs> at;
    std::array<double, kBlobs> radius{};
    for (int b = 0; b < kBlobs; ++b) {
        const double fx = 0.03 + 0.04 * unit(b, 0);
        const double fy = 0.025 + 0.04 * unit(b, 1);
        at[static_cast<size_t>(b)] = {0.75 * s.hx * std::sin(2.0 * kPi * (fx * t + unit(b, 2))),
                                      0.8 * s.hy * std::sin(2.0 * kPi * (fy * t + unit(b, 3)))};
        radius[static_cast<size_t>(b)] = 0.25 + 0.15 * unit(b, 4);
    }
    for (size_t i = 0; i < s.at.size(); ++i) {
        double field = 0.0;
        Rgb mixed;
        for (size_t b = 0; b < kBlobs; ++b) {
            const double d2 = std::max(1e-4, std::pow(length(s.at[i], at[b]), 2.0));
            const double w = radius[b] * radius[b] / d2;
            field += w;
            add(mixed, colours[b], w);
        }
        const Rgb colour = scaled(mixed, 1.0 / field);
        const double level = 0.12 * std::min(field, 1.0) + 0.88 * smoothstep(0.85, 1.15, field);
        out[i] = scaled(colour, level);
    }
}

void draw_spiral(const Space &s, double t, Frame &out) {
    constexpr double kWheelDegPerS = 24.0; // A turn in 15 s.
    constexpr double kTwistTurns = 0.6;    // Hue turns from the centre to the edge.
    constexpr double kRingS = 2.5;         // A ring out of the centre this often.
    constexpr double kRingsAcross = 1.5;
    const double breath = 0.85 + 0.15 * std::sin(2.0 * kPi * t / 8.0);
    for (size_t i = 0; i < s.at.size(); ++i) {
        const V2 q = s.at[i];
        const double r = std::hypot(q.x, q.y);
        const double turn = std::atan2(q.y, q.x) / (2.0 * kPi);
        const double hue = 360.0 * (turn + kTwistTurns * r) + kWheelDegPerS * t;
        const double ring = 0.5 + 0.5 * std::cos(2.0 * kPi * (kRingsAcross * r - t / kRingS));
        out[i] = hsv(hue, 1.0, breath * (0.25 + 0.75 * ring * ring));
    }
}

} // namespace

Frame render_eye_candy(int show, const Rig &rig, double clock_s, float level) {
    Frame out(rig.pixels.size());
    if (rig.pixels.empty()) {
        return out;
    }
    const double t = std::max(0.0, clock_s);
    switch (std::clamp(show, 0, kEyeCandyCount - 1)) {
    case hunt:
        draw_hunt(rig, space_of(rig), t, out);
        break;
    case billiards:
        draw_billiards(space_of(rig), t, out);
        break;
    case bouncing:
        draw_bouncing(rig, t, out);
        break;
    case meteors:
        draw_meteors(rig, t, out);
        break;
    case fireworks:
        draw_fireworks(space_of(rig), t, out);
        break;
    case ocean:
        draw_ocean(space_of(rig), t, out);
        break;
    case lava:
        draw_lava(space_of(rig), t, out);
        break;
    case spiral:
        draw_spiral(space_of(rig), t, out);
        break;
    case aurora:
    default:
        out = render_aurora(rig, t, 1);
        break;
    }
    // Overlaps keep their hue (scaled back, not clipped to white), then the pattern's level.
    for (Rgb &c : out) {
        const float peak = std::max({c.r, c.g, c.b});
        c = scaled(c, (peak > 1.0f ? 1.0 / peak : 1.0) * std::clamp(level, 0.0f, 1.0f));
    }
    return out;
}

const char *eye_candy_name(int show) {
    return kNames[static_cast<size_t>(std::clamp(show, 0, kEyeCandyCount - 1))];
}

const char *eye_candy_description(int show) {
    return kDescriptions[static_cast<size_t>(std::clamp(show, 0, kEyeCandyCount - 1))];
}

} // namespace psyight
