// Scene variations (SPEC 7.6, long sections): the same look, changed a little, so a scene that
// plays for minutes develops instead of repeating. Variant 0 is the scene itself; each other
// variant changes one or two layer settings, seeded, from the kinds allowed:
// - reverse: a signed rate runs the other way, a direction (out, in) flips;
// - remap: a pattern's mapping moves to another (along the strips, across, radial, hex rings...);
// - retime: a rate or drift doubles, or halves when doubling would leave its range;
// - mirror: a pattern's wings (mirrored copies) change;
// - mute: one layer other than the first goes quiet (scenes of three layers or more).
// The automation picks the variant per phrase (engine/automation.h); the show renderer builds the
// variants once per configuration. Pure: a function of the scene, the kinds and the seed.
#pragma once

#include "engine/scene.h"

#include <cstdint>
#include <vector>

namespace psyight {

inline constexpr int kSceneVariants = 4; // The scene itself and three variations.

struct VariationKinds {
    bool reverse = true;
    bool remap = true;
    bool retime = true;
    bool mirror = true;
    bool mute = true;

    bool operator==(const VariationKinds &) const = default;
};

// kSceneVariants scenes: [0] the scene, [1..] its variations (a variation with nothing it may
// change is the scene again).
std::vector<Scene> scene_variations(const Scene &scene, const VariationKinds &kinds,
                                    std::uint64_t seed);

// Per scene, its variations: what render_mix draws for SceneMix::variant.
using SceneVariants = std::vector<std::vector<Scene>>;
SceneVariants all_variations(const std::vector<Scene> &scenes, const VariationKinds &kinds,
                             std::uint64_t seed);

} // namespace psyight
