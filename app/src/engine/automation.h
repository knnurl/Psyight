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
// - A manual scene overrides the section rules until released. SceneLauncher (below) makes a
//   newly chosen scene, or the return to the rules, wait for the next bar or phrase (SPEC 7.6:
//   a cut on the next downbeat), as AutomationParams::launch says.
#pragma once

#include "analysis/track_analysis.h"
#include "core/timebase.h"
#include "engine/inputs.h"
#include "engine/scene.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

// When a scene chosen by hand (or the return to the section rules) takes over.
enum class SceneLaunch { now, bar, phrase };
inline constexpr std::array<SceneLaunch, 3> kSceneLaunches = {SceneLaunch::now, SceneLaunch::bar,
                                                              SceneLaunch::phrase};
const char *scene_launch_id(SceneLaunch launch);   // "now", "bar", "phrase" (files).
const char *scene_launch_name(SceneLaunch launch); // "Now", "Next bar", "Next phrase".
std::optional<SceneLaunch> scene_launch_from_id(const std::string &id);

struct AutomationParams {
    double crossfade_beats = 4.0;
    int palette_every_phrases = 2; // 0 keeps each scene's own palette.
    double palette_slew_beats = 4.0;
    SceneLaunch launch = SceneLaunch::bar;
};

// Launch quantisation for the manual scene, once a frame. A requested change (a scene, or -1 for
// the section rules) waits until the bar (or the phrase) changes, then takes over; with
// SceneLaunch::now, or while the render time stands still (the transport stopped), it takes over
// at once. Deterministic for the same sequence of requests and inputs.
class SceneLauncher {
  public:
    // The manual scene in effect this frame (-1: the section rules).
    int update(int requested, const EngineInputs &inputs, SceneLaunch launch);
    // The request still waiting: a scene, -1 for the rules, or kNoneQueued.
    int queued() const { return waiting_ ? target_ : kNoneQueued; }
    void reset() { started_ = false; } // After a seek: the next request takes over at once.

    static constexpr int kNoneQueued = -2;

  private:
    bool started_ = false;
    int active_ = -1;
    bool waiting_ = false;
    int target_ = -1;
    long long bar_ = 0; // When the request came.
    int phrase_ = -1;
    double t_s_ = 0.0; // The last frame's render time.
};

struct SceneMix {
    int from = -1;          // The outgoing scene while crossfading, else -1.
    int to = 0;             // The incoming (or only) scene.
    double to_weight = 1.0; // 1: `to` alone.
    int palette_shift = 0;  // Added to each scene's palette index.
    int palette_shift_from = 0;
    double palette_weight = 1.0; // 1: palette_shift alone.
};

// The scene for the occurrence-th section (from 0) with this label: the scenes tagged with the
// label in turn (a drop with no drop scene takes the groove scenes); scene 0 when none is tagged.
int scene_for_label(const std::vector<Scene> &scenes, const std::string &label, size_t occurrence);

// The scene for section `index` of the analysis (rotation among tagged scenes).
int scene_for_section(const std::vector<Scene> &scenes,
                      const std::vector<DetectedSection> &sections, size_t index);

SceneMix automate(const std::vector<Scene> &scenes, const TrackAnalysis &analysis, const Grid &grid,
                  const EngineInputs &inputs, const AutomationParams &params,
                  int manual_scene = -1);

// Live mode: the same rules from the live state's section history; the phrase start for the
// palette slew comes from the predicted beat.
SceneMix automate_live(const std::vector<Scene> &scenes, const LiveState &state,
                       const EngineInputs &inputs, const AutomationParams &params,
                       int manual_scene = -1);

// The routes' memory per scene, for render_mix (holding and smoothing; engine/modulation.h).
using ModMemory = std::vector<std::vector<ModRouteState>>;

// memory: null renders instantaneous route values; otherwise resized to the scenes.
Frame render_mix(const std::vector<Scene> &scenes, const SceneMix &mix, const Rig &rig,
                 const EngineInputs &inputs, const std::vector<Palette> &palettes,
                 std::uint64_t seed, ModMemory *memory = nullptr);

} // namespace psyight
