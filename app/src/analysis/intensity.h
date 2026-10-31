// The music's intensity per bar (SPEC 7.6, long sections), 0 to 1, for file mode: how full the
// bar is against the rest of the track. Each of five bar features is ranked among all the
// track's bars (0 the lowest, 1 the highest), so the curve does not depend on the master's level,
// then weighted and smoothed over the neighbouring bars:
//   loudness (all bands' level) 0.35, hats (the hats band's level) 0.25, the residual's brightness
//   (spectral centroid: a filter opening) 0.15, bass notes in the bar 0.15, kicks in the bar 0.1.
// The weights are a first guess, to be checked against the labelling view's intensity line.
#pragma once

#include "analysis/bass_aggregates.h"
#include "analysis/sections.h"

#include <vector>

namespace psyight {

// One value per section bar (bars.size()); bass_bars are the grid's bars (BassAggregates::bars),
// matched by time.
std::vector<float> intensity_curve(const std::vector<SectionBar> &bars,
                                   const std::vector<BassAggregate> &bass_bars);

// The curve at t_s: linear between bar centres, the end values beyond; 0.5 without bars.
double intensity_at(const std::vector<SectionBar> &bars, const std::vector<float> &intensity,
                    double t_s);

} // namespace psyight
