#include "engine/scene.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Deterministic hash to 0 to 1 (SplitMix64 finaliser): no generator state, so rendering stays
// a pure function of its inputs.
double hash01(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    std::uint64_t z = a * 0x9E3779B97F4A7C15ull ^
                      (b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull ^
                      (c + 0x85EBCA77C2B2AE63ull) * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) * 0x1.0p-53;
}

bool is_groove(const std::string &section) { return section == "groove" || section == "drop"; }

Vec3 centre_of(const Rig &rig) {
    return {0.5 * (rig.min.x + rig.max.x), 0.5 * (rig.min.y + rig.max.y),
            0.5 * (rig.min.z + rig.max.z)};
}

class Plasma final : public Effect {
  public:
    explicit Plasma(const PlasmaParams &p) : p_(p) {}
    const char *name() const override { return "Plasma"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const double level = is_groove(c.in.section) ? p_.groove_level : p_.ambient_level;
        const double t = c.in.t_s * p_.speed_hz;
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double a = std::sin(2.0 * kPi * (q.x / p_.scale_m + t));
            const double b = std::sin(2.0 * kPi * (q.y / (0.7 * p_.scale_m) - 1.7 * t) + q.x);
            const double v = 0.5 + 0.25 * (a + b); // 0 to 1.
            out[i] = scaled(c.palette.at(0.5 * v + 0.1 * t), level * (0.4 + 0.6 * v));
        }
    }

  private:
    PlasmaParams p_;
};

class Shockwave final : public Effect {
  public:
    explicit Shockwave(const ShockwaveParams &p) : p_(p) {}
    const char *name() const override { return "Kick shockwave"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        if (!std::isfinite(c.in.since_kick_s)) {
            std::fill(out.begin(), out.end(), Rgb{});
            return;
        }
        const double radius_m = c.in.since_kick_s * p_.speed_m_per_s;
        const double level = std::exp(-c.in.since_kick_s / p_.decay_s);
        const Vec3 centre = centre_of(c.rig);
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double d = std::sqrt((q.x - centre.x) * (q.x - centre.x) +
                                       (q.y - centre.y) * (q.y - centre.y) +
                                       (q.z - centre.z) * (q.z - centre.z));
            const double x = (d - radius_m) / p_.width_m;
            const double v = level * std::exp(-x * x);
            out[i] = scaled({1.0f, 1.0f, 1.0f}, v);
        }
    }

  private:
    ShockwaveParams p_;
};

class Conveyor final : public Effect {
  public:
    explicit Conveyor(const ConveyorParams &p) : p_(p) {}
    const char *name() const override { return "Bass conveyor"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const EngineInputs &in = c.in;
        // Brightness: velocity times the note's decay while it sounds, then a short release.
        double level = 0.0;
        if (std::isfinite(in.since_bass_onset_s)) {
            const double tau_s = in.bass_decay_tau_ms > 0.0 ? in.bass_decay_tau_ms * 1e-3 : 0.2;
            level = std::max(0.2, in.bass_velocity) * std::exp(-in.since_bass_onset_s / tau_s);
            if (!in.bass_active) {
                level *= std::exp(-in.since_bass_offset_s / p_.release_s); // After the note ends.
            }
        }
        const Rgb colour = hsv(p_.hue_offset_deg +
                                   p_.hue_per_semitone_deg * (in.bass_midi + in.bass_cents / 100.0),
                               1.0, 1.0);
        const int period = std::max(2, p_.segment_pixels);
        const double lit = std::clamp(in.bass_gate_ratio, 0.1, 1.0) * 0.5 * period;
        for (size_t i = 0; i < out.size(); ++i) {
            const Pixel &px = c.rig.pixels[i];
            // The pattern steps one segment per note; alternate strips run the other way.
            const long long shift = in.bass_notes_so_far * (period / 2);
            const long long position = (px.strip % 2 == 0 ? px.index : -px.index) + shift;
            const long long phase = ((position % period) + period) % period;
            out[i] = phase < lit ? scaled(colour, level) : Rgb{};
        }
    }

  private:
    ConveyorParams p_;
};

class Sparkle final : public Effect {
  public:
    explicit Sparkle(const SparkleParams &p) : p_(p) {}
    const char *name() const override { return "Hat sparkle"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const double level =
            std::isfinite(c.in.since_hat_s) ? std::exp(-c.in.since_hat_s / p_.decay_s) : 0.0;
        for (size_t i = 0; i < out.size(); ++i) {
            const bool lit =
                hash01(c.seed, static_cast<std::uint64_t>(c.in.hats_so_far), i) < p_.density;
            out[i] = lit ? scaled(c.palette.at(hash01(c.seed, i, 7)), level) : Rgb{};
        }
    }

  private:
    SparkleParams p_;
};

class Shimmer final : public Effect {
  public:
    explicit Shimmer(const ShimmerParams &p) : p_(p) {}
    const char *name() const override { return "Residual shimmer"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        if (c.in.bass_active) {
            std::fill(out.begin(), out.end(), Rgb{}); // Only the rests.
            return;
        }
        // Dark sounds lean violet, bright ones cyan (SPEC 7.5).
        const double brightness = std::clamp(c.in.residual_centroid_hz / 6000.0, 0.0, 1.0);
        const Rgb colour = hsv(270.0 - 90.0 * brightness, 0.8, 1.0);
        const auto tick = static_cast<std::uint64_t>(std::floor(c.in.t_s * p_.rate_hz));
        for (size_t i = 0; i < out.size(); ++i) {
            const double flicker = hash01(c.seed, tick, i);
            out[i] = scaled(colour, p_.level * c.in.residual_level * flicker * flicker);
        }
    }

  private:
    ShimmerParams p_;
};

} // namespace

std::unique_ptr<Effect> make_plasma(const PlasmaParams &params) {
    return std::make_unique<Plasma>(params);
}
std::unique_ptr<Effect> make_shockwave(const ShockwaveParams &params) {
    return std::make_unique<Shockwave>(params);
}
std::unique_ptr<Effect> make_conveyor(const ConveyorParams &params) {
    return std::make_unique<Conveyor>(params);
}
std::unique_ptr<Effect> make_sparkle(const SparkleParams &params) {
    return std::make_unique<Sparkle>(params);
}
std::unique_ptr<Effect> make_shimmer(const ShimmerParams &params) {
    return std::make_unique<Shimmer>(params);
}

Frame render_scene(const Scene &scene, const Rig &rig, const EngineInputs &inputs,
                   const std::vector<Palette> &palettes, std::uint64_t seed) {
    Frame frame(rig.pixels.size());
    if (palettes.empty()) {
        return frame;
    }
    const Palette &palette = palettes[static_cast<size_t>(
        std::clamp(scene.palette, 0, static_cast<int>(palettes.size()) - 1))];
    const EffectContext context{rig, inputs, palette, seed};
    Frame layer(rig.pixels.size());
    for (const Layer &l : scene.layers) {
        if (!l.enabled || l.effect == nullptr) {
            continue;
        }
        l.effect->render(context, layer);
        for (size_t i = 0; i < frame.size(); ++i) {
            frame[i] = blend(frame[i], layer[i], l.blend, l.opacity);
        }
    }
    return frame;
}

Scene default_scene() {
    Scene scene;
    scene.name = "Default";
    scene.layers.push_back({make_plasma(), BlendMode::alpha, 1.0f, true});
    scene.layers.push_back({make_conveyor(), BlendMode::add, 1.0f, true});
    scene.layers.push_back({make_shimmer(), BlendMode::add, 1.0f, true});
    scene.layers.push_back({make_sparkle(), BlendMode::add, 0.8f, true});
    scene.layers.push_back({make_shockwave(), BlendMode::screen, 1.0f, true});
    return scene;
}

} // namespace psyight
