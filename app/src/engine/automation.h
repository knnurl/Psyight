// Scene automation (SPEC section 7.6): which scene plays at a render time, from the section
// structure, plus palette rotation by phrase. A pure function of the scenes, the analysis, the
// inputs and the grid, so file mode knows every scene change ahead and replays identically.
//
// Rules (v0):
// - Each section picks among the scenes tagged with its label, in rotation: the k-th groove gets
//   the k-th groove scene modulo their count. A drop with no drop scene falls back to groove
//   scenes; any other label with no scene falls back to the first scene.
// - Entering a groove or drop cuts (sections start on downbeats, so the cut lands on one);
//   entering anything else crossfades over crossfade_beats, at most half the section.
// - The palette shifts by one every palette_every_phrases phrases (counted over the track),
//   slewed over palette_slew_beats as an RGB crossfade (not a hue-path slew).
// - A manual scene overrides the section rules until released (immediately in v0; SPEC asks for
//   a cut on the next downbeat or a crossfade).
#pragma once

#include "analysis/track_analysis.h"
#include "core/timebase.h"
#include "engine/inputs.h"
#include "engine/scene.h"

#include <cstdint>
#include <vector>

namespace psyight {

struct AutomationParams {
    double crossfade_beats = 4.0;
    int palette_every_phrases = 2; // 0 keeps each scene's own palette.
    double palette_slew_beats = 4.0;
};

struct SceneMix {
    int from = -1;          // The outgoing scene while crossfading, else -1.
    int to = 0;             // The incoming (or only) scene.
    double to_weight = 1.0; // 1: `to` alone.
    int palette_shift = 0;  // Added to each scene's palette index.
    int palette_shift_from = 0;
    double palette_weight = 1.0; // 1: palette_shift alone.
};

// The scene for section `index` of the analysis (rotation among tagged scenes).
int scene_for_section(const std::vector<Scene> &scenes,
                      const std::vector<DetectedSection> &sections, size_t index);

SceneMix automate(const std::vector<Scene> &scenes, const TrackAnalysis &analysis,
                  const Grid &grid, const EngineInputs &inputs, const AutomationParams &params,
                  int manual_scene = -1);

Frame render_mix(const std::vector<Scene> &scenes, const SceneMix &mix, const Rig &rig,
                 const EngineInputs &inputs, const std::vector<Palette> &palettes,
                 std::uint64_t seed);

} // namespace psyight
