// The synthetic evaluation set: named generator configurations covering the prototype patterns,
// gate ratios, decays, sidechain, hats and noise, melodic lines, slides and long notes, at 150 and
// 160 BPM. Bass tracker tuning and its metric checks run on this set.
#pragma once

#include "synth_generator.h"

#include <string>
#include <vector>

namespace psyight::synth {

struct SyntheticCase {
    std::string name;
    SynthParams params;
    std::vector<BassEvent> events;
};

std::vector<SyntheticCase> synthetic_set();

} // namespace psyight::synth
