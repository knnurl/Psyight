// Scene automation (SPEC section 7.6): which scene plays at a render time, from the section
// structure, plus palette rotation by phrase. A pure function of the scenes, the analysis, the
// inputs and the grid, so file mode knows every scene change ahead and replays identically.
//
// Rules (v0):
// - Each section picks among the scenes tagged with its label, in rotation: the k-th groove gets
//   the k-th groove scene modulo their count. A drop with no drop scene falls back to groove
//   scenes; any other label with no scene falls back to the first scene.
// - Scenes may also be tagged with an energy, "low", "mid" or "high": among the scenes for a
//   label, a section picks those tagged with its energy, else those with no energy tag, else any
//   (a groove that steps up gets its high-energy look).
// - A section shorter than a phrase keeps the scene before it (it does not count in the
//   rotation either), unless it is a build or a drop: a blip in the structure is no scene change.
// - Long sections rotate: every rotate_every_phrases phrases into a section, the next scene with
//   its tag (and energy) takes over, on the phrase's downbeat, cut or crossfaded over
//   rotate_fade_beats. Builds do not rotate: their arc runs through. Phrases count from the
//   section's start (file mode: the analysis's phrase starts; live: phrase_bars bars each).
// - Phrase-end fills (grooves and drops): over the last fill_beats beats of every
//   fill_every_phrases-th phrase into the section, and of the phrase before a rotation, a fill
//   plays over the scene: a gap (dark), a stutter (16th gate), a sweep across the rig, an
//   implosion (a ring closing on the centre) or a pulse (pumping on the beats); mix picks one
//   per phrase. Not at a section's last phrase in file mode (the section change is its own
//   transition; live mode cannot know it), nor while a scene is held by hand.
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

// A phrase-end fill (above).
enum class PhraseFill { off, gap, stutter, sweep, implode, pulse, mix };
inline constexpr std::array<PhraseFill, 7> kPhraseFills = {
    PhraseFill::off,     PhraseFill::gap,   PhraseFill::stutter, PhraseFill::sweep,
    PhraseFill::implode, PhraseFill::pulse, PhraseFill::mix};
const char *phrase_fill_id(PhraseFill fill);   // "off", "gap", ... (files).
const char *phrase_fill_name(PhraseFill fill); // "Off", "Gap", ...
std::optional<PhraseFill> phrase_fill_from_id(const std::string &id);

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
    int rotate_every_phrases = 2;   // Long sections: the next scene this often; 0: one a section.
    double rotate_fade_beats = 0.0; // 0: a cut on the phrase's downbeat.
    PhraseFill fill = PhraseFill::mix;
    int fill_every_phrases = 2; // And before every rotation; 0: only before rotations.
    double fill_beats = 2.0;    // The fill's length, up to the phrase's end.
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
    double palette_weight = 1.0;       // 1: palette_shift alone.
    PhraseFill fill = PhraseFill::off; // A phrase-end fill playing (never mix: the one picked).
    double fill_s = 0.0;               // Its length...
    double fill_progress = 0.0;        // ...and how far through, 0 to 1.
};

// Energy tags, by DetectedSection::energy (0 low, 1 mid, 2 high).
inline constexpr std::array<const char *, 3> kEnergyTags = {"low", "mid", "high"};

// The scene for the occurrence-th section (from 0) with this label: the scenes tagged with the
// label in turn (a drop with no drop scene takes the groove scenes), narrowed by energy (0 to 2;
// -1 ignores it) as above; scene 0 when none is tagged. step: rotations within the section, each
// the next candidate on.
int scene_for_label(const std::vector<Scene> &scenes, const std::string &label, size_t occurrence,
                    int energy = -1, size_t step = 0);

// The scene for section `index` of the analysis (rotation among tagged scenes). Sections shorter
// than min_dwell_s, builds and drops aside, keep the scene of the section before them.
int scene_for_section(const std::vector<Scene> &scenes,
                      const std::vector<DetectedSection> &sections, size_t index,
                      double min_dwell_s = 0.0);

// timeline: hand-edited sections in place of the analysis's (null: its own), as inputs_at.
SceneMix automate(const std::vector<Scene> &scenes, const TrackAnalysis &analysis, const Grid &grid,
                  const EngineInputs &inputs, const AutomationParams &params, int manual_scene = -1,
                  const SectionTimeline *timeline = nullptr);

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
