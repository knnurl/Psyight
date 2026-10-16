// Section detection (SPEC section 6.6), file mode: intro, groove, breakdown, build, drop and
// outro per bar, from the beat tracker's kicks per beat and the front end's high band.
//
// Rules (per bar of the grid; a bar has a kick when most of its beats do; one with exactly half
// takes the state of the nearest decisive bar, and a lone kick bar inside a breakdown is a blip):
// - intro and outro: the bars without kicks before the first and after the last kick bar;
// - breakdown: a run of at least min_breakdown_bars bars without kicks (a single kickless bar
//   is a fill: SPEC 6.6 said 4 beats, real tracks are full of one-bar fills);
// - build: the end of a breakdown where the high band (hats, 8 to 12 kHz) rises at least
//   build_min_rise_db above the breakdown's quietest bar, up to the kick's return;
// - drop: the first drop_bars bars after a breakdown of at least drop_min_breakdown_bars
//   (SPEC 18 A9: the kick returning after 8 bars or more is a drop);
// - groove: every other bar with kicks.
// Confidence is the share of beats in the section that agree with its kick state.
// Assumptions: 4/4 on the beat tracker's grid. Deterministic. Latency: non-causal (file mode).
#pragma once

#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "core/params.h"
#include "core/result.h"

#include <string>
#include <vector>

namespace psyight {

struct SectionParams {
    double min_breakdown_bars = 2.0;
    double build_min_rise_db = 6.0;
    double drop_min_breakdown_bars = 8.0;
    double drop_bars = 8.0;
};

struct DetectedSection {
    double start_s = 0.0;
    double end_s = 0.0;
    std::string label; // "intro", "groove", "breakdown", "build", "drop" or "outro" (SPEC 6.10).
    double confidence = 0.0;
};

// Contiguous sections in time order; empty when the beat tracker found no grid.
Result<std::vector<DetectedSection>> detect_sections(const FrontEndResult &features,
                                                     const BeatTrackResult &beats,
                                                     const SectionParams &params);

// Phrase starts (SPEC 6.5): every bars_per_phrase bars from the phrase offset (bars after the
// grid's first downbeat), restarting the count at every section start after the first. Times in
// seconds, ascending, within [0, duration_s).
std::vector<double> phrase_starts(const Grid &grid, const std::vector<DetectedSection> &sections,
                                  int bars_per_phrase, int phrase_bar_offset, double duration_s);

Result<void> declare_section_params(ParamRegistry &registry);
SectionParams section_params_from(const ParamValues &values);

} // namespace psyight
