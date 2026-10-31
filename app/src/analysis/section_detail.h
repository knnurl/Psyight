// Finer sections for psytrance (SPEC 6.6.1), detected after the sections: each needs the bars
// (analysis/sections.h), the bass aggregates, the intensity curve and the phrase starts.
// - Phases: grooves and drops are described by their bars' mean texture (band levels, bass and
//   hats density) and bass (notes, pitch range, gate), each scaled over the track's bars; in time
//   order a section joins the nearest phase so far within phase_distance (its distance from the
//   phase's mean, as texture_step measures a change), else opens the next letter.
// - Roles: a groove is peak when its mean intensity reaches the top peak_share of the grooves'
//   and drops' bars, else lead when most of its bars have a tonal, bright residual (flatness
//   under lead_flatness_ratio and centroid over lead_centroid_ratio of the grooves' medians),
//   else main; the drop of the highest mean intensity is climax, the others main.
// - Pre-drop: before a drop, the build or breakdown's last bars (up to predrop_max_bars) whose
//   level is predrop_drop_db or more under its median: the gap before the kick returns.
// - Events: a break is 1 to break_max_bars bars with at most one kick inside a groove or drop; a
//   fill is a phrase's last bar whose texture is fill_distance or more from the rest of the
//   phrase (and not a break).
// First guesses throughout, to be scored against labels (SPEC 6.6.1) before tuning.
#pragma once

#include "analysis/bass_aggregates.h"
#include "analysis/sections.h"

#include <vector>

namespace psyight {

// Sets phase, role and predrop_s on the sections; returns the breaks and fills in time order.
std::vector<DetectedEvent> detect_section_detail(std::vector<DetectedSection> &sections,
                                                 const std::vector<SectionBar> &bars,
                                                 const std::vector<BassAggregate> &bass_bars,
                                                 const std::vector<float> &intensity,
                                                 const std::vector<double> &phrase_starts,
                                                 double bar_s, const SectionParams &params = {});

} // namespace psyight
