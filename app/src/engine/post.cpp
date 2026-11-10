#include "engine/post.h"

#include "engine/shape.h"
#include "engine/space.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kMaxStepS = 0.1;

Rgb max_of(const Rgb &a, const Rgb &b) {
    return {std::max(a.r, b.r), std::max(a.g, b.g), std::max(a.b, b.b)};
}

// bins sampled at fractional position x (bin b's centre at b), linear, 0 outside.
Rgb sample(const std::vector<Rgb> &bins, double x) {
    const auto n = static_cast<long long>(bins.size());
    const double f = std::floor(x);
    const auto i = static_cast<long long>(f);
    const auto w = static_cast<float>(x - f);
    const auto at = [&](long long k) {
        return k >= 0 && k < n ? bins[static_cast<size_t>(k)] : Rgb{};
    };
    const Rgb a = at(i);
    const Rgb b = at(i + 1);
    return {a.r + (b.r - a.r) * w, a.g + (b.g - a.g) * w, a.b + (b.b - a.b) * w};
}

void afterglow(const AfterglowParams &p, double dt, double beat_s, Frame &frame, PostState &s,
               bool advance) {
    if (advance) {
        const auto k = static_cast<float>(std::exp(-dt / std::max(1e-3, p.decay_beats * beat_s)));
        for (size_t i = 0; i < frame.size(); ++i) {
            s.frame[i] = max_of(frame[i], scaled(s.frame[i], k));
        }
    }
    const auto amount = static_cast<float>(std::clamp(p.amount, 0.0, 1.0));
    for (size_t i = 0; i < frame.size(); ++i) {
        frame[i] = max_of(frame[i], scaled(s.frame[i], amount));
    }
}

void radial_echo(const RadialEchoParams &p, const Rig &rig, double dt, double beat_s, Frame &frame,
                 PostState &s, bool advance) {
    const std::vector<double> radius = hex_radii(rig, hex_frame(rig));
    constexpr int kBins = kRadialEchoBins;
    const auto bin_of = [&](size_t i) {
        return std::clamp(static_cast<int>(radius[i] * kBins), 0, kBins - 1);
    };
    if (advance) {
        const double shift = p.speed * dt / beat_s * kBins;
        const auto fade =
            static_cast<float>(std::exp(-dt / std::max(1e-3, p.decay_beats * beat_s)));
        std::vector<Rgb> moved(kBins);
        for (int b = 0; b < kBins; ++b) {
            moved[static_cast<size_t>(b)] = scaled(sample(s.bins, b - shift), fade);
        }
        // The scene's mean colour per bin feeds in.
        std::vector<Rgb> sum(kBins);
        std::vector<int> count(kBins, 0);
        for (size_t i = 0; i < frame.size(); ++i) {
            const auto b = static_cast<size_t>(bin_of(i));
            sum[b] = {sum[b].r + frame[i].r, sum[b].g + frame[i].g, sum[b].b + frame[i].b};
            ++count[b];
        }
        const auto feed = static_cast<float>(std::clamp(p.feed, 0.0, 1.0));
        for (size_t b = 0; b < moved.size(); ++b) {
            if (count[b] > 0) {
                moved[b] = max_of(moved[b], scaled(sum[b], feed / static_cast<float>(count[b])));
            }
        }
        s.bins = std::move(moved);
    }
    const auto mix = static_cast<float>(std::clamp(p.mix, 0.0, 1.0));
    for (size_t i = 0; i < frame.size(); ++i) {
        frame[i] = max_of(frame[i], scaled(sample(s.bins, radius[i] * kBins - 0.5), mix));
    }
}

} // namespace

void apply_post(const Scene &scene, const Rig &rig, const EngineInputs &inputs, Frame &frame,
                std::vector<PostState> *state, bool advance) {
    if (state == nullptr || scene.post.empty()) {
        return;
    }
    state->resize(scene.post.size());
    const double beat_s = inputs.beat_s > 0.0 ? inputs.beat_s : kReferenceBeatS;
    for (size_t k = 0; k < scene.post.size(); ++k) {
        const PostSpec &spec = scene.post[k];
        PostState &s = (*state)[k];
        if (s.frame.size() != frame.size()) { // New, or another rig: from black.
            s = PostState{};
            s.frame.assign(frame.size(), Rgb{});
            s.bins.assign(kRadialEchoBins, Rgb{});
        }
        const double dt =
            std::isfinite(s.last_t_s) ? std::clamp(inputs.t_s - s.last_t_s, 0.0, kMaxStepS) : 0.0;
        if (advance) {
            s.last_t_s = inputs.t_s;
        }
        if (!spec.enabled) {
            continue;
        }
        if (const auto *a = std::get_if<AfterglowParams>(&spec.params)) {
            afterglow(*a, dt, beat_s, frame, s, advance);
        } else if (const auto *e = std::get_if<RadialEchoParams>(&spec.params)) {
            radial_echo(*e, rig, dt, beat_s, frame, s, advance);
        }
    }
    for (Rgb &c : frame) {
        c = {std::min(c.r, 1.0f), std::min(c.g, 1.0f), std::min(c.b, 1.0f)};
    }
}

const char *post_type_id(PostType type) {
    return type == PostType::radial_echo ? "radial_echo" : "afterglow";
}

const char *post_type_name(PostType type) {
    return type == PostType::radial_echo ? "Radial echo" : "Afterglow";
}

std::optional<PostType> post_type_from_id(const std::string &id) {
    for (const PostType t : kPostTypes) {
        if (id == post_type_id(t)) {
            return t;
        }
    }
    return std::nullopt;
}

PostSpec default_post(PostType type) {
    if (type == PostType::radial_echo) {
        return {RadialEchoParams{}, true};
    }
    return {AfterglowParams{}, true};
}

} // namespace psyight
