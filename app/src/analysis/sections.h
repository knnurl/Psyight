// Section detection (SPEC section 6.6), file mode: intro, groove, breakdown, build, drop and
// outro per bar, from the beat tracker's kicks per beat and the front end's high band.
//
// Rules (per bar of the grid). The kick state per bar is smoothed by a two-state Viterbi: each
// bar costs the beats that disagree with its state, and every change of state costs
// kick_switch_penalty beats, so a dropout (a fill, missed kicks) stays a groove and a stray kick
// stays inside a breakdown. With the default 6 beats, a breakdown inside the track needs four
// kickless bars (it saves 4 beats a bar and costs two changes); at either end, two.
// - intro and outro: the bars without kicks before the first and after the last kick bar;
// - breakdown: a run of at least min_breakdown_bars bars without kicks (a single kickless bar
//   is a fill: SPEC 6.6 said 4 beats, real tracks are full of one-bar fills);
// - build, from a riser (with the residual, breakdowns of at least 4 bars): the residual's
//   spectral flatness peaks in the breakdown's last build_search_bars bars at least
//   build_flatness_ratio times its lower quartile there and at least build_min_flatness (noise
//   risers are flat; pads and leads are tonal). The build starts where flatness first passes
//   the quartile times the ratio's square root on the way up to the peak, and runs to the kick's
//   return, so a quiet gap (up to 4 bars) before the drop stays in it. Flatness already high at
//   the breakdown's first bar is texture, not a riser;
// - build, otherwise: the end of a breakdown where the high band (hats, 8 to 12 kHz) rises at
//   least build_min_rise_db above the breakdown's quietest bar, up to the kick's return;
// - drop: the first drop_bars bars after a breakdown of at least drop_min_breakdown_bars
//   (SPEC 18 A9: the kick returning after 8 bars or more is a drop);
// - groove: every other bar with kicks.
// - energy: grooves split where the level changes by energy_step_db or more between phrases (a
//   groove that steps up); every section then gets an energy level, low, mid or high, from its
//   level against the track's bar levels (terciles);
// - texture: a groove or drop also splits on a 4-bar line of the phrase grid where the bar's
//   texture (the five bands' levels and the bass and hats onset density, each scaled over the
//   track) differs from the bar before by texture_step or more (the kick runs on: a layer comes
//   in, the bass changes; on the owner's labelled tracks most changes are of this kind);
// - build starts within build_snap_bars of a 4-bar line of the phrase grid move onto it.
// Confidence is the share of beats in the section that agree with its kick state.
// Assumptions: 4/4 on the beat tracker's grid. Deterministic. Latency: non-causal (file mode).
#pragma once

#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "analysis/residual.h"
#include "core/params.h"
#include "core/result.h"

#include <string>
#include <utility>
#include <vector>

namespace psyight {

struct SectionParams {
    double min_breakdown_bars = 2.0;
    double build_min_rise_db = 6.0;
    double build_flatness_ratio = 2.5; // Riser peak over the breakdown's lower-quartile flatness.
    double build_min_flatness = 0.03;  // Riser peak at least this flat.
    double build_search_bars = 8.0;    // The riser peaks within the breakdown's last bars.
    double drop_min_breakdown_bars = 8.0;
    double drop_bars = 8.0;
    double kick_switch_penalty = 6.0; // Beats of disagreement a change of kick state costs.
    double energy_step_db = 3.0;      // A level change between phrases that splits a groove.
    double build_snap_bars = 1.0;     // A build start this close to a 4-bar line moves onto it.
    double texture_step = 1.5; // Bar-to-bar texture change (track-scaled) splitting on 4-bar lines.
    // Finer detail (analysis/section_detail.h): first guesses, to be scored against labels.
    double phase_distance = 1.5; // Texture and bass distance within which sections share a phase.
    double peak_share = 0.2;     // Grooves in the top fifth of intensity are peaks.
    double lead_flatness_ratio = 0.8; // A lead's residual: flatter than this share of the median...
    double lead_centroid_ratio = 1.1; // ...and brighter than this.
    double lead_bars_share = 0.6;     // Of a groove's bars, for it to be a lead.
    double predrop_drop_db = 6.0;     // A pre-drop bar is this far under its section's median.
    double predrop_max_bars = 4.0;
    double break_max_bars = 4.0;
    double fill_distance = 2.5; // A phrase's last bar this far from the rest of it is a fill.
};

// One bar of the grid as the section rules see it (also printed by psyight_analyse --bars).
struct SectionBar {
    double start_s = 0.0;
    bool kick = false;        // After the Viterbi smoothing.
    int kick_beats = 0;       // Beats of the bar with a kick.
    double high_db = 0.0;     // The hats band's mean power.
    double level_db = 0.0;    // All bands' mean power: the bar's loudness.
    double flatness = 0.0;    // The residual's mean spectral flatness over sounding frames, 0 to 1.
    double centroid_hz = 0.0; // The residual's mean spectral centroid over sounding frames.
    // The front end's band levels (dB) in band order, then the bass and hats bands' onset
    // density (log of the summed flux).
    std::vector<double> texture;
};

struct DetectedSection {
    double start_s = 0.0;
    double end_s = 0.0;
    std::string label; // "intro", "groove", "breakdown", "build", "drop" or "outro" (SPEC 6.10).
    double confidence = 0.0;
    int energy = 1; // 0 low, 1 mid, 2 high: the section's level against the track's.
    // Finer detail (SPEC 6.6.1): grooves and drops get a phase (0 for A, 1 for B...; -1 none),
    // sections whose bassline and texture are close sharing one; grooves a role (main, lead,
    // peak), drops climax or main; a build or breakdown before a drop where its pre-drop starts
    // (-1: none). The engine reads none of them yet.
    int phase = -1;
    std::string role;
    double predrop_s = -1.0;
};

// Short events inside sections (SPEC 6.6.1): a break (the kick out for 1 to 4 bars in a groove or
// drop) or a fill (a phrase's last bar departing from the phrase).
struct DetectedEvent {
    std::string kind; // "break" or "fill".
    double start_s = 0.0;
    double length_s = 0.0;

    bool operator==(const DetectedEvent &) const = default;
};

// "A" for 0, "B" for 1... ("" for none); and back (-1 for anything else).
std::string phase_name(int phase);
int phase_from_name(const std::string &name);

// The bars from the grid's first downbeat; empty when the beat tracker found no grid. Without
// the residual, flatness and centroid are 0.
Result<std::vector<SectionBar>> section_bars(const FrontEndResult &features,
                                             const BeatTrackResult &beats,
                                             const ResidualFeatures *residual,
                                             const SectionParams &params = {});

// Contiguous sections in time order; empty when the beat tracker found no grid.
Result<std::vector<DetectedSection>> detect_sections(const FrontEndResult &features,
                                                     const BeatTrackResult &beats,
                                                     const SectionParams &params,
                                                     const ResidualFeatures *residual = nullptr);

// Each section's energy, low (0), mid (1) or high (2): its bars' mean level against the terciles
// of all bars' levels. Sections without bars keep mid.
void assign_energy(std::vector<DetectedSection> &sections, const std::vector<SectionBar> &bars);

// The riser curve, 0 to 1 per residual frame (frame_s apart): through each build it follows the
// residual's flatness (smoothed over 1 s) from the build's start to its peak, never falling, and
// holds there to the build's end; 0 elsewhere. A build without a flatness rise ramps linearly.
std::vector<float> riser_curve(const ResidualFeatures &residual,
                               const std::vector<DetectedSection> &sections, double frame_s);

// Sections from their starts (hand-edited ones): sorted by start, each running to the next start
// and the last to duration_s, with full confidence. Starts at or past duration_s are dropped; the
// first section is stretched back to 0 s.
std::vector<DetectedSection>
sections_from_starts(std::vector<std::pair<double, std::string>> starts, double duration_s);

// Phrase starts (SPEC 6.5): every bars_per_phrase bars from the phrase offset (bars after the
// grid's first downbeat), restarting the count at every section start after the first. Times in
// seconds, ascending, within [0, duration_s).
std::vector<double> phrase_starts(const Grid &grid, const std::vector<DetectedSection> &sections,
                                  int bars_per_phrase, int phrase_bar_offset, double duration_s);

Result<void> declare_section_params(ParamRegistry &registry);
SectionParams section_params_from(const ParamValues &values);

} // namespace psyight
