// Post-effects (docs/PLAN_HEX_VISUALS.md section 9): stateful passes over a scene's frame after
// its layers (engine/scene.h PostSpec), each with the memory of the frames before:
// - afterglow: each pixel keeps max(now, glow * k), k = exp(-dt / (decay_beats * beat)), and
//   shows max(now, amount * glow);
// - radial echo: 64 bins over hex radius 0 to 1 carry the frame's mean colour per radius, moving
//   `speed` reaches a beat (out, or in when negative) and fading over decay_beats; the scene feeds
//   them (feed) and they are shown under it (mix).
// Both depend on the time step dt (clamped to 0.1 s), not the frame count, so 30 and 60 frames a
// second agree. Deterministic for the same sequence of render times. The state is reset when the
// rig's pixel count changes, and the show renderer clears it on a seek (as the routes' memory).
#pragma once

#include "engine/inputs.h"
#include "engine/rig.h"
#include "engine/scene.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

inline constexpr int kRadialEchoBins = 64;

struct PostState {
    Frame frame;           // Afterglow: the glow.
    std::vector<Rgb> bins; // Radial echo: colour per radius bin.
    double last_t_s = kNever;
};
using PostMemory = std::vector<std::vector<PostState>>; // Per scene, per post-effect.

// Applies the scene's enabled post-effects to its frame in order. state: one per post-effect
// (resized to fit); null does nothing (the pure path: audition, per-layer checks, tests of
// scenes). advance false shows the effect from the state without stepping it (a scene drawn a
// second time in a frame, as during a palette slew).
void apply_post(const Scene &scene, const Rig &rig, const EngineInputs &inputs, Frame &frame,
                std::vector<PostState> *state, bool advance = true);

const char *post_type_id(PostType type);   // "afterglow", "radial_echo" (files).
const char *post_type_name(PostType type); // "Afterglow", "Radial echo".
std::optional<PostType> post_type_from_id(const std::string &id);
PostSpec default_post(PostType type);
inline constexpr std::array<PostType, 2> kPostTypes = {PostType::afterglow, PostType::radial_echo};

} // namespace psyight
