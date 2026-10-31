#include "engine/variations.h"

#include "engine/effect_spec.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace psyight {

namespace {

enum class Kind { reverse, remap, retime, mirror, mute };

struct Change {
    Kind kind;
    size_t layer;
    size_t param; // Unused for mute.
};

std::uint64_t mix(std::uint64_t x) { // splitmix64's finaliser.
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

bool allowed(Kind kind, const VariationKinds &k) {
    switch (kind) {
    case Kind::reverse:
        return k.reverse;
    case Kind::remap:
        return k.remap;
    case Kind::retime:
        return k.retime;
    case Kind::mirror:
        return k.mirror;
    case Kind::mute:
        return k.mute;
    }
    return false;
}

// What a scene's layers allow.
std::vector<Change> changes_of(const Scene &scene, const VariationKinds &kinds) {
    std::vector<Change> out;
    for (size_t l = 0; l < scene.layers.size(); ++l) {
        const Layer &layer = scene.layers[l];
        if (!layer.enabled || layer.blend == BlendMode::multiply) {
            continue; // Gates are left as they are.
        }
        const std::vector<EffectParamInfo> infos = effect_param_infos(layer.spec.type());
        for (size_t i = 0; i < infos.size(); ++i) {
            const std::string id = infos[i].id;
            const double v = effect_param(layer.spec, i);
            if ((id == "rate" && infos[i].min < 0.0 && v != 0.0) || id == "direction") {
                out.push_back({Kind::reverse, l, i});
            }
            if (id == "mapping") {
                out.push_back({Kind::remap, l, i});
            }
            if ((id == "rate" || id == "speed_hz") && v != 0.0) {
                out.push_back({Kind::retime, l, i});
            }
            if (id == "wings") {
                out.push_back({Kind::mirror, l, i});
            }
        }
        if (l > 0 && scene.layers.size() >= 3) {
            out.push_back({Kind::mute, l, 0});
        }
    }
    std::erase_if(out, [&](const Change &c) { return !allowed(c.kind, kinds); });
    return out;
}

void apply(Scene &scene, const Change &c, std::uint64_t pick) {
    Layer &layer = scene.layers[c.layer];
    if (c.kind == Kind::mute) {
        layer.opacity = 0.0f;
        return;
    }
    const std::vector<EffectParamInfo> infos = effect_param_infos(layer.spec.type());
    const EffectParamInfo &info = infos[c.param];
    const double v = effect_param(layer.spec, c.param);
    double next = v;
    switch (c.kind) {
    case Kind::reverse:
        next = std::string(info.id) == "direction" ? 1.0 - v : -v;
        break;
    case Kind::remap: { // Another mapping.
        const auto count = static_cast<std::uint64_t>(info.max - info.min) + 1;
        if (count > 1) {
            next = info.min +
                   static_cast<double>(
                       (static_cast<std::uint64_t>(v - info.min) + 1 + pick % (count - 1)) % count);
        }
        break;
    }
    case Kind::retime:
        next = 2.0 * v >= info.min && 2.0 * v <= info.max ? 2.0 * v : 0.5 * v;
        break;
    case Kind::mirror: { // Another number of wings.
        const auto count = static_cast<std::uint64_t>(info.max - info.min) + 1;
        if (count > 1) {
            next = info.min +
                   static_cast<double>(
                       (static_cast<std::uint64_t>(v - info.min) + 1 + pick % (count - 1)) % count);
        }
        break;
    }
    case Kind::mute:
        break;
    }
    set_effect_param(layer.spec, c.param, next);
    layer.effect = make_effect(layer.spec);
}

} // namespace

std::vector<Scene> scene_variations(const Scene &scene, const VariationKinds &kinds,
                                    std::uint64_t seed) {
    std::vector<Scene> out(kSceneVariants, scene);
    const std::vector<Change> changes = changes_of(scene, kinds);
    if (changes.empty()) {
        return out;
    }
    for (int k = 1; k < kSceneVariants; ++k) {
        std::uint64_t h = mix(seed ^ mix(static_cast<std::uint64_t>(k)));
        // One change, or two different ones.
        const size_t first = h % changes.size();
        h = mix(h);
        apply(out[static_cast<size_t>(k)], changes[first], h);
        if (changes.size() > 1 && (h >> 40) % 2 == 0) {
            h = mix(h);
            const size_t second = (first + 1 + h % (changes.size() - 1)) % changes.size();
            apply(out[static_cast<size_t>(k)], changes[second], mix(h));
        }
    }
    return out;
}

SceneVariants all_variations(const std::vector<Scene> &scenes, const VariationKinds &kinds,
                             std::uint64_t seed) {
    SceneVariants out;
    out.reserve(scenes.size());
    for (size_t i = 0; i < scenes.size(); ++i) {
        out.push_back(scene_variations(scenes[i], kinds, mix(seed + i)));
    }
    return out;
}

} // namespace psyight
