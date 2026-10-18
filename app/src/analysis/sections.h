// Section detection (SPEC section 6.6), file mode: intro, groove, breakdown, build, drop and
// outro per bar, from the beat tracker's kicks per beat and the front end's high band.
//
// Rules (per bar of the grid; a bar has a kick when most of its beats do; one with exactly half
// takes the state of the nearest decisive bar, and a lone kick bar inside a breakdown is a blip):
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
};

// One bar of the grid as the section rules see it (also printed by psyight_analyse --bars).
struct SectionBar {
    double start_s = 0.0;
    bool kick = false;        // After the half-kicked and lone-kick-bar rules.
    int kick_beats = 0;       // Beats of the bar with a kick.
    double high_db = 0.0;     // The hats band's mean power.
    double flatness = 0.0;    // The residual's mean spectral flatness over sounding frames, 0 to 1.
    double centroid_hz = 0.0; // The residual's mean spectral centroid over sounding frames.
};

struct DetectedSection {
    double start_s = 0.0;
    double end_s = 0.0;
    std::string label; // "intro", "groove", "breakdown", "build", "drop" or "outro" (SPEC 6.10).
    double confidence = 0.0;
};

// The bars from the grid's first downbeat; empty when the beat tracker found no grid. Without
// the residual, flatness and centroid are 0.
Result<std::vector<SectionBar>> section_bars(const FrontEndResult &features,
                                             const BeatTrackResult &beats,
                                             const ResidualFeatures *residual);

// Contiguous sections in time order; empty when the beat tracker found no grid.
Result<std::vector<DetectedSection>> detect_sections(const FrontEndResult &features,
                                                     const BeatTrackResult &beats,
                                                     const SectionParams &params,
                                                     const ResidualFeatures *residual = nullptr);

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
