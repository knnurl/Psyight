// Helpers shared by the effect tests (hexagon effects, units, the hexagon-visuals effects): the
// hexagon mandala preset (rings at hex distance 1, 2/3 and 1/3, spokes at every corner), a
// render with a two-colour palette and seed 7, and grid inputs at 150 BPM.
#pragma once

#include "engine/effect_spec.h"
#include "engine/inputs.h"
#include "engine/rig.h"
#include "engine/rig_templates.h"
#include "engine/scene.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

namespace psyight::test {

inline Rig mandala() {
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name == "Hexagon mandala") {
            return build_rig(p.strips);
        }
    }
    FAIL("no hexagon mandala");
    return {};
}

inline float level(const Rgb &c) { return std::max({c.r, c.g, c.b}); }

inline Frame render(const EffectSpec &spec, const Rig &rig, const EngineInputs &in,
                    std::uint64_t seed = 7) {
    const Palette palette{"test", {{1, 0, 0}, {0, 0, 1}}};
    Frame out(rig.pixels.size());
    make_effect(spec)->render({rig, in, palette, seed}, out);
    return out;
}

inline std::vector<size_t> pixels_of(const Rig &rig, int strip) {
    std::vector<size_t> out;
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        if (rig.pixels[i].strip == strip) {
            out.push_back(i);
        }
    }
    return out;
}

inline EngineInputs grid_at(long long beat, double phase) {
    EngineInputs in;
    in.beat_s = 0.4;
    in.beat_index = beat;
    in.beat_phase = phase;
    in.beat_in_bar = static_cast<int>(beat % 4);
    in.bar_index = beat / 4;
    in.bar_phase = (static_cast<double>(beat % 4) + phase) / 4.0;
    return in;
}

} // namespace psyight::test
