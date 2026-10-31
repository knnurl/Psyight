// The hexagon effects (engine/scene.h): built on engine/space.h's hex frame, so each hexagon
// ring about the rig's centre is one hex distance.

#include "engine/scene.h"
#include "engine/space.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

double frac(double x) { return x - std::floor(x); }

long long mod(long long a, long long n) { return ((a % n) + n) % n; }

// A beat's length, or 150 BPM's without a tempo.
double beat_of(const EngineInputs &in) { return in.beat_s > 0.0 ? in.beat_s : 0.4; }

Clock clock_of(int c) { return static_cast<Clock>(std::clamp(c, 0, kClockCount - 1)); }

Rgb mix(const Rgb &a, const Rgb &b, double t) {
    const auto f = static_cast<float>(std::clamp(t, 0.0, 1.0));
    return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f};
}

// The latest trigger: its index (-1: none yet), the time since it, and the spacing of earlier
// ones (0 when unknown: only the latest is drawn).
struct Trigger {
    long long index = -1;
    double since_s = 0.0;
    double spacing_s = 0.0;
    const EventAges *ages = nullptr; // Hats and bass notes: their real recent times.
};

Trigger latest(const EngineInputs &in, int trigger) {
    const double beat_s = beat_of(in);
    Trigger t;
    switch (trigger) {
    case 0: // Kick: earlier kicks taken a beat apart.
        if (in.kicks_so_far > 0 && std::isfinite(in.since_kick_s)) {
            t = {in.kicks_so_far - 1, in.since_kick_s, beat_s};
        }
        break;
    case 1: // Beat.
        t = {in.beat_index, in.beat_phase * beat_s, beat_s};
        break;
    case 2: // Bar.
        t = {in.bar_index, in.bar_phase * 4.0 * beat_s, 4.0 * beat_s};
        break;
    case 3: // Bass note.
        if (in.bass_notes_so_far > 0 && std::isfinite(in.since_bass_onset_s)) {
            t = {in.bass_notes_so_far - 1, in.since_bass_onset_s, 0.0, &in.bass_onset_ages_s};
        }
        break;
    default: // Hat.
        if (in.hats_so_far > 0 && std::isfinite(in.since_hat_s)) {
            t = {in.hats_so_far - 1, in.since_hat_s, 0.0, &in.hat_ages_s};
        }
        break;
    }
    return t;
}

class HexPulse final : public Effect {
  public:
    explicit HexPulse(const HexPulseParams &p) : p_(p) {}
    const char *name() const override { return "Hex pulse"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        std::fill(out.begin(), out.end(), Rgb{});
        const Trigger t = latest(c.in, p_.trigger);
        if (t.index < 0) {
            return;
        }
        const HexFrame frame = hex_frame(c.rig, p_.corners);
        std::vector<double> u(out.size());
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            u[i] = hex_radius(frame, q.x, q.y);
        }
        const double travel_s = std::max(1e-3, p_.travel_beats * beat_of(c.in));
        const double width = std::max(0.01, p_.width);
        const long long every = std::max(1, p_.every);
        const int keep = t.spacing_s > 0.0   ? std::clamp(p_.keep, 1, 6)
                         : t.ages != nullptr ? std::clamp(p_.keep, 1, kEventHistory)
                                             : 1;
        // Walk back over triggers until `keep` pulses (every nth trigger) are drawn.
        int drawn = 0;
        for (long long k = 0; drawn < keep && k < keep * every; ++k) {
            const long long event = t.index - k;
            if (event < 0 || (t.ages != nullptr && k >= kEventHistory)) {
                break;
            }
            if (mod(event, every) != 0) {
                continue;
            }
            ++drawn;
            const double age = t.ages != nullptr ? (*t.ages)[static_cast<size_t>(k)]
                                                 : t.since_s + static_cast<double>(k) * t.spacing_s;
            if (!std::isfinite(age)) {
                break;
            }
            const double fade = std::exp(-age / std::max(1e-3, p_.decay_s));
            const double travelled = age / travel_s;
            if (fade < 0.01 || travelled > 1.0 + 3.0 * width) {
                continue;
            }
            const double front = p_.direction == 0 ? travelled : 1.0 - travelled;
            const Rgb pulse = c.palette.at(0.25 * static_cast<double>(mod(event, 4)));
            for (size_t i = 0; i < out.size(); ++i) {
                const double x = (u[i] - front) / width;
                if (std::fabs(x) > 3.0) {
                    continue;
                }
                const double v = p_.level * fade * std::exp(-x * x);
                const Rgb colour = scaled(p_.colours == 0 ? pulse : c.palette.at(u[i]), v);
                out[i] = {std::max(out[i].r, colour.r), std::max(out[i].g, colour.g),
                          std::max(out[i].b, colour.b)};
            }
        }
    }

  private:
    HexPulseParams p_;
};

class Cube final : public Effect {
  public:
    explicit Cube(const CubeParams &p) : p_(p) {}
    const char *name() const override { return "Cube"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const EngineInputs &in = c.in;
        const double beat_s = beat_of(in);
        // Flips so far, and the time since the last.
        long long flips = 0;
        double since_s = 1e9;
        switch (p_.flip) {
        case 0: // Beat.
            flips = in.beat_index;
            since_s = in.beat_phase * beat_s;
            break;
        case 1: // Bar.
            flips = in.bar_index;
            since_s = in.bar_phase * 4.0 * beat_s;
            break;
        case 2: // Two bars.
            flips = (in.bar_index - mod(in.bar_index, 2)) / 2;
            since_s = (static_cast<double>(mod(in.bar_index, 2)) + in.bar_phase) * 4.0 * beat_s;
            break;
        case 3: // Phrase (its length taken as 8 bars, for the fade only).
            flips = std::max(0, in.phrase_index);
            since_s = in.phrase_phase * 32.0 * beat_s;
            break;
        default: // Kick.
            flips = in.kicks_so_far;
            since_s = std::isfinite(in.since_kick_s) ? in.since_kick_s : 1e9;
            break;
        }
        const int now = static_cast<int>(mod(flips, 2));
        const double settled = p_.fade_s > 0.0 ? 1.0 - std::exp(-since_s / p_.fade_s) : 1.0;
        const HexFrame frame = hex_frame(c.rig, p_.corners);
        const double edge_m = std::max(0.0, p_.edge_width) * frame.reach_m;
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double dx = q.x - frame.cx;
            const double dy = q.y - frame.cy;
            out[i] = mix(seen(c, frame, 1 - now, dx, dy, edge_m),
                         seen(c, frame, now, dx, dy, edge_m), settled);
        }
    }

  private:
    // A pixel's colour with the cube one way (0: from above, its top face up; 1: from below).
    Rgb seen(const EffectContext &c, const HexFrame &frame, int way, double dx, double dy,
             double edge_m) const {
        // Faces centred on every other corner, from the top one (or the next); the Y between.
        const double base = frame.corner_deg + (way == 0 ? 0.0 : 60.0);
        for (int j = 0; j < 3; ++j) {
            const double a = (base + 60.0 + 120.0 * j) * kPi / 180.0;
            const double along = dx * std::cos(a) + dy * std::sin(a);
            const double across = std::fabs(-dx * std::sin(a) + dy * std::cos(a));
            if (along > 0.0 && across < edge_m) {
                return scaled(c.palette.at(0.5), p_.edges);
            }
        }
        const double deg = std::atan2(dy, dx) * 180.0 / kPi;
        const double turned = frac((deg - base + 60.0) / 360.0);
        const int face = std::min(2, static_cast<int>(turned * 3.0));
        const double shade = std::clamp(p_.shade, 0.0, 1.0);
        const double tone = face == 0 ? 1.0 : face == 1 ? 1.0 - 0.5 * shade : 1.0 - shade;
        return scaled(c.palette.at(p_.colour_spread * face / 3.0), p_.level * tone);
    }

    CubeParams p_;
};

class Sectors final : public Effect {
  public:
    explicit Sectors(const SectorsParams &p) : p_(p) {}
    const char *name() const override { return "Sectors"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const int n = std::clamp(p_.count, 3, 12);
        const auto step =
            static_cast<long long>(std::floor(clock_cycles(c.in, clock_of(p_.clock), p_.rate)));
        const double tail = std::max(0.5, p_.tail);
        const auto faded = [&](long long ago) {
            return ago == 0                          ? 1.0
                   : static_cast<double>(ago) < tail ? (1.0 - static_cast<double>(ago) / tail) *
                                                           (1.0 - static_cast<double>(ago) / tail)
                                                     : 0.0;
        };
        // Each slice's brightness this step.
        std::vector<double> lit(static_cast<size_t>(n), 0.0);
        for (int s = 0; s < n; ++s) {
            double v = 0.0;
            switch (p_.mode) {
            case 0: // Chase, clockwise, with a fading tail.
                v = faded(mod(step - s, n));
                break;
            case 1: // Alternate halves.
                v = mod(s + step, 2) == 0 ? 1.0 : 0.0;
                break;
            case 2: // Fill round, then clear.
                v = s <= mod(step, n) ? 1.0 : 0.0;
                break;
            default: // A seeded slice each step, earlier ones fading.
                for (long long ago = 0; static_cast<double>(ago) < tail + 1.0; ++ago) {
                    const auto pick = static_cast<int>(
                        hash_unit(c.seed, static_cast<std::uint64_t>(step - ago), 61) * n);
                    if (std::min(pick, n - 1) == s) {
                        v = std::max(v, faded(ago));
                    }
                }
                break;
            }
            lit[static_cast<size_t>(s)] = v;
        }
        const HexFrame frame = hex_frame(c.rig, p_.corners);
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double turn = hex_turn(frame, q.x, q.y);
            const double at = p_.align == 0 ? frac(turn + 0.5 / n) : turn;
            const int s = std::min(n - 1, static_cast<int>(at * n));
            const double v = lit[static_cast<size_t>(s)];
            out[i] = v > 0.0 ? scaled(c.palette.at(p_.colour_spread * s / n), p_.level * v) : Rgb{};
        }
    }

  private:
    SectorsParams p_;
};

class Interference final : public Effect {
  public:
    explicit Interference(const InterferenceParams &p) : p_(p) {}
    const char *name() const override { return "Interference"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const EngineInputs &in = c.in;
        const HexFrame frame = hex_frame(c.rig, p_.corners);
        const double k = 2.0 * kPi / std::max(0.01, p_.scale_m);
        const double drift = 2.0 * kPi * clock_cycles(in, clock_of(p_.clock), p_.rate);
        const double pump = std::isfinite(in.since_kick_s)
                                ? p_.kick_pump * std::exp(-in.since_kick_s / (0.5 * beat_of(in)))
                                : 0.0;
        const double contrast = p_.contrast * (1.0 + pump);
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double dx = q.x - frame.cx;
            const double dy = q.y - frame.cy;
            // Three waves towards every other corner; the lattice slides along the first.
            double sum = 0.0;
            for (int j = 0; j < 3; ++j) {
                const double a = (frame.corner_deg + 120.0 * j) * kPi / 180.0;
                const double slide = j == 0 ? 1.0 : -0.5;
                sum += std::cos(k * (dx * std::cos(a) + dy * std::sin(a)) - drift * slide);
            }
            const double v = (sum + 1.5) / 4.5; // 0 to 1.
            const double shown = std::clamp(0.5 + (v - 0.5) * contrast, 0.0, 1.0);
            out[i] = scaled(c.palette.at(p_.colour_spread * v), p_.level * shown);
        }
    }

  private:
    InterferenceParams p_;
};

} // namespace

std::unique_ptr<Effect> make_hex_pulse(const HexPulseParams &params) {
    return std::make_unique<HexPulse>(params);
}

std::unique_ptr<Effect> make_cube(const CubeParams &params) {
    return std::make_unique<Cube>(params);
}

std::unique_ptr<Effect> make_sectors(const SectorsParams &params) {
    return std::make_unique<Sectors>(params);
}

std::unique_ptr<Effect> make_interference(const InterferenceParams &params) {
    return std::make_unique<Interference>(params);
}

} // namespace psyight
