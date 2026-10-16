// Bass aggregates (SPEC section 6.2): per bar and per phrase of the grid, the character of the
// bassline: note count, gate-ratio mean and variance, slides, rests and pitch range. They drive
// visuals that follow the bar (staccato against legato looks, SPEC 7.3) rather than each note.
//
// Bars start at the grid's first downbeat. A note belongs to the bar holding its onset. A rest is
// a 16th slot whose middle no note covers (SPEC 6.3: a slot with no bass sounding). Phrases group
// bars_per_phrase bars from the phrase offset; the bars before the first phrase start form a
// shorter first phrase. Pure functions of the notes and the grid.
#pragma once

#include "analysis/bass_tracker.h"
#include "core/timebase.h"

#include <vector>

namespace psyight {

struct BassAggregate {
    double start_s = 0.0;
    double end_s = 0.0;
    int notes = 0;
    double gate_mean = 0.0;     // 0 without notes.
    double gate_variance = 0.0; // Population variance; 0 without notes.
    int slides = 0;             // Notes that slide in.
    int rests = 0;              // 16th slots with no bass sounding.
    int pitch_low = 0;          // MIDI; 0 without notes.
    int pitch_high = 0;
};

struct BassAggregates {
    std::vector<BassAggregate> bars;
    std::vector<BassAggregate> phrases;
};

// duration_s bounds the last bar. bars_per_phrase >= 1; phrase_bar_offset counts bars from the
// first downbeat to the first phrase start.
BassAggregates aggregate_bass(const std::vector<BassNote> &notes, const Grid &grid,
                              double duration_s, int bars_per_phrase, int phrase_bar_offset);

} // namespace psyight
