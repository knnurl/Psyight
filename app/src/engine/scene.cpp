#include "engine/scene.h"

#include "engine/effect_spec.h"

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
        const int segment = std::max(1, p_.segment_pixels);
        const int period = segment * std::max(2, p_.period_segments);
        const double lit = std::clamp(in.bass_gate_ratio, 0.1, 1.0) * segment;
        for (size_t i = 0; i < out.size(); ++i) {
            const Pixel &px = c.rig.pixels[i];
            // The pattern steps one segment per note; alternate strips run the other way.
            const long long shift = in.bass_notes_so_far * segment;
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
        const double fade = 1.0 - std::exp(-c.in.since_bass_offset_s / p_.fade_s);
        const double level = p_.level * c.in.residual_level * fade;
        for (size_t i = 0; i < out.size(); ++i) {
            if (hash01(c.seed, i, 11) >= p_.density) {
                out[i] = Rgb{};
                continue;
            }
            const double twinkle =
                0.5 + 0.5 * std::sin(2.0 * kPi * (p_.rate_hz * c.in.t_s + hash01(c.seed, i, 12)));
            out[i] = scaled(colour, level * twinkle * twinkle);
        }
    }

  private:
    ShimmerParams p_;
};

class Build final : public Effect {
  public:
    explicit Build(const BuildParams &p) : p_(p) {}
    const char *name() const override { return "Build compression"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        if (c.in.section != "build" || c.in.section_length_s <= 0.0) {
            std::fill(out.begin(), out.end(), Rgb{});
            return;
        }
        // Speed rises with the square of the progress p; the phase is its integral over time,
        // f0 * (t + ramp * L * p^3 / 3), so the motion is continuous.
        const double p = std::clamp(c.in.section_progress, 0.0, 1.0);
        const double phase =
            p_.start_speed_hz *
            (c.in.since_section_s + p_.speed_ramp * c.in.section_length_s * p * p * p / 3.0);
        const double level = p_.start_level + (p_.end_level - p_.start_level) * p * p;
        const Vec3 centre = centre_of(c.rig);
        const Rgb colour = c.palette.at(0.25 * p);
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double d = std::hypot(q.x - centre.x, q.y - centre.y, q.z - centre.z);
            // Constant-phase points sit at d = wavelength * (k - phase): they move inward.
            const double v = 0.5 + 0.5 * std::cos(2.0 * kPi * (d / p_.wavelength_m + phase));
            out[i] = scaled(colour, level * v * v * v);
        }
    }

  private:
    BuildParams p_;
};

class DropBurst final : public Effect {
  public:
    explicit DropBurst(const DropBurstParams &p) : p_(p) {}
    const char *name() const override { return "Drop burst"; }
    void render(const EffectContext &c, std::span<Rgb> out) const override {
        const double t = c.in.since_section_s;
        if (c.in.section != "drop" || t >= p_.duration_s) {
            std::fill(out.begin(), out.end(), Rgb{});
            return;
        }
        const double radius_m = p_.speed_m_per_s * t;
        const double level = std::exp(-t / p_.decay_s);
        const Vec3 centre = centre_of(c.rig);
        for (size_t i = 0; i < out.size(); ++i) {
            const Vec3 &q = c.rig.pixels[i].position;
            const double d = std::hypot(q.x - centre.x, q.y - centre.y, q.z - centre.z);
            // A filled disc with a soft 10 cm edge, brightest at its rim.
            const double inside = std::clamp((radius_m - d) / 0.1 + 1.0, 0.0, 1.0);
            const double rim = radius_m > 0.0 ? 0.5 + 0.5 * std::min(1.0, d / radius_m) : 1.0;
            out[i] = scaled({1.0f, 1.0f, 1.0f}, level * inside * rim);
        }
    }

  private:
    DropBurstParams p_;
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
std::unique_ptr<Effect> make_build(const BuildParams &params) {
    return std::make_unique<Build>(params);
}
std::unique_ptr<Effect> make_drop_burst(const DropBurstParams &params) {
    return std::make_unique<DropBurst>(params);
}

std::shared_ptr<const Effect> Layer::effect_for(const std::vector<ModRoute> &routes,
                                                const std::vector<double> &values,
                                                size_t index) const {
    std::vector<EffectParamInfo> infos;
    std::vector<double> offsets;
    for (size_t i = 0; i < routes.size() && i < values.size(); ++i) {
        const ModRoute &r = routes[i];
        if (r.target != ModTarget::setting || r.layer != static_cast<int>(index)) {
            continue;
        }
        if (infos.empty()) {
            infos = effect_param_infos(spec.type());
            offsets.assign(infos.size(), 0.0);
        }
        for (size_t p = 0; p < infos.size(); ++p) {
            if (r.param == infos[p].id) {
                offsets[p] += r.amount * (infos[p].max - infos[p].min) * values[i];
            }
        }
    }
    bool moved = false;
    for (const double offset : offsets) {
        moved |= offset != 0.0;
    }
    if (!moved) {
        return effect;
    }
    EffectSpec modulated = spec;
    for (size_t p = 0; p < offsets.size(); ++p) {
        if (offsets[p] != 0.0) {
            set_effect_param(modulated, p, effect_param(spec, p) + offsets[p]); // Clamped.
        }
    }
    return make_effect(modulated);
}

Frame render_scene(const Scene &scene, const Rig &rig, const EngineInputs &inputs,
                   const std::vector<Palette> &palettes, std::uint64_t seed,
                   std::vector<ModRouteState> *memory) {
    if (palettes.empty()) {
        return Frame(rig.pixels.size());
    }
    return render_scene(scene, rig, inputs,
                        palettes[static_cast<size_t>(
                            std::clamp(scene.palette, 0, static_cast<int>(palettes.size()) - 1))],
                        seed, memory);
}

Frame render_scene(const Scene &scene, const Rig &rig, const EngineInputs &inputs,
                   const Palette &palette, std::uint64_t seed, std::vector<ModRouteState> *memory) {
    Frame frame(rig.pixels.size());
    const EffectContext context{rig, inputs, palette, seed};
    Frame layer(rig.pixels.size());
    // Each route's value once per frame.
    std::vector<double> values(scene.routes.size());
    if (memory != nullptr) {
        memory->resize(scene.routes.size());
    }
    for (size_t i = 0; i < scene.routes.size(); ++i) {
        values[i] = memory != nullptr ? step_route(scene.routes[i], (*memory)[i], inputs, seed)
                                      : route_value(scene.routes[i], inputs, seed);
    }
    double brightness = 1.0;
    for (size_t i = 0; i < scene.routes.size(); ++i) {
        const ModRoute &r = scene.routes[i];
        if (r.target == ModTarget::brightness) {
            brightness += r.amount * (values[i] - 1.0);
        }
    }
    for (size_t k = 0; k < scene.layers.size(); ++k) {
        const Layer &l = scene.layers[k];
        if (!l.enabled || l.effect == nullptr) {
            continue;
        }
        double opacity = l.opacity;
        double hue_deg = 0.0;
        for (size_t i = 0; i < scene.routes.size(); ++i) {
            const ModRoute &r = scene.routes[i];
            if (r.layer != static_cast<int>(k)) {
                continue;
            }
            if (r.target == ModTarget::opacity) {
                opacity += r.amount * values[i];
            } else if (r.target == ModTarget::hue_shift) {
                hue_deg += r.amount * values[i];
            }
        }
        opacity = std::clamp(opacity, 0.0, 1.0);
        if (opacity <= 0.0) {
            continue;
        }
        l.effect_for(scene.routes, values, k)->render(context, layer);
        const bool rotate = std::abs(hue_deg) > 1e-9;
        std::vector<char> strip_in_group; // Per strip, when the layer has a group mask.
        if (!l.group.empty()) {
            for (const StripSpec &strip : rig.strips) {
                strip_in_group.push_back(std::find(strip.groups.begin(), strip.groups.end(),
                                                   l.group) != strip.groups.end());
            }
        }
        for (size_t i = 0; i < frame.size(); ++i) {
            if (!l.group.empty()) {
                const auto strip = static_cast<size_t>(rig.pixels[i].strip);
                if (strip >= strip_in_group.size() || !strip_in_group[strip]) {
                    continue; // Masked out.
                }
            }
            const Rgb c = rotate ? rotate_hue(layer[i], hue_deg) : layer[i];
            frame[i] = blend(frame[i], c, l.blend, static_cast<float>(opacity));
        }
    }
    brightness = std::clamp(brightness, 0.0, 1.0);
    if (brightness < 1.0) {
        for (Rgb &c : frame) {
            c = scaled(c, brightness);
        }
    }
    return frame;
}

void remove_layer(Scene &scene, size_t index) {
    if (index >= scene.layers.size()) {
        return;
    }
    scene.layers.erase(scene.layers.begin() + static_cast<std::ptrdiff_t>(index));
    const int removed = static_cast<int>(index);
    std::erase_if(scene.routes, [removed](const ModRoute &r) {
        return r.target != ModTarget::brightness && r.layer == removed;
    });
    for (ModRoute &r : scene.routes) {
        if (r.target != ModTarget::brightness && r.layer > removed) {
            --r.layer;
        }
    }
}

void swap_layers(Scene &scene, size_t a, size_t b) {
    if (a >= scene.layers.size() || b >= scene.layers.size() || a == b) {
        return;
    }
    std::swap(scene.layers[a], scene.layers[b]);
    for (ModRoute &r : scene.routes) {
        if (r.target == ModTarget::brightness) {
            continue;
        }
        if (r.layer == static_cast<int>(a)) {
            r.layer = static_cast<int>(b);
        } else if (r.layer == static_cast<int>(b)) {
            r.layer = static_cast<int>(a);
        }
    }
}

Scene default_scene() {
    Scene scene;
    scene.name = "Default";
    scene.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::alpha));
    scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add));
    scene.layers.push_back(make_layer({ShimmerParams{}}, BlendMode::add));
    scene.layers.push_back(make_layer({SparkleParams{}}, BlendMode::add, 0.8f));
    scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen));
    return scene;
}

std::vector<Scene> default_scenes() {
    std::vector<Scene> scenes;
    Scene groove = default_scene();
    groove.name = "Groove";
    groove.sections = {"groove"};
    scenes.push_back(groove);

    Scene staccato = default_scene();
    staccato.name = "Groove, staccato";
    staccato.sections = {"groove"};
    staccato.palette = 1;
    staccato.layers[1] = make_layer({ConveyorParams{6, 5, 28.0, 270.0, 0.02}}, BlendMode::add);
    scenes.push_back(staccato);

    Scene drop = default_scene();
    drop.name = "Drop";
    drop.sections = {"drop"};
    drop.layers.push_back(make_layer({DropBurstParams{}}, BlendMode::screen));
    scenes.push_back(drop);

    Scene ambient;
    ambient.name = "Ambient";
    ambient.sections = {"intro", "breakdown", "outro"};
    ambient.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::alpha));
    ambient.layers.push_back(make_layer({ShimmerParams{}}, BlendMode::add));
    ambient.layers.push_back(make_layer({SparkleParams{}}, BlendMode::add, 0.5f));
    ambient.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add, 0.4f));
    // The plasma's hue drifts 60 degrees and back over each phrase (SPEC 7.5).
    ambient.routes.push_back(
        {ModSource::lfo_phrase, ModCurve::s_curve, false, ModTarget::hue_shift, 0, 60.0});
    scenes.push_back(ambient);

    Scene build;
    build.name = "Build";
    build.sections = {"build"};
    build.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::alpha, 0.5f));
    build.layers.push_back(make_layer({BuildParams{}}, BlendMode::add));
    build.layers.push_back(make_layer({SparkleParams{}}, BlendMode::add, 0.8f));
    build.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen));
    scenes.push_back(build);
    return scenes;
}

} // namespace psyight
