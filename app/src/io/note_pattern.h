// Copy, paste and duplicate rules for labelled bass notes (bass roll labelling tools), kept free of
// the GUI so they are unit-tested.
//
// A pattern is anchored at the 16th slot nearest its first onset and keeps every note's offset
// from that anchor, so a note labelled a few ms off the grid stays off it by the same amount. It
// repeats every period_slots slots: by default the slots its onsets cover, rounded up to whole
// beats, so one bar of a K-B-B-B line (onsets in 15 slots) repeats every bar. Placing a pattern
// replaces the notes in the slots it covers, because the bass is monophonic: two overlapping notes
// would be wrong ground truth.
#pragma once

#include "core/timebase.h"
#include "io/labels.h"

#include <cstddef>
#include <limits>
#include <vector>

namespace psyight {

struct NotePattern {
    std::vector<BassNoteLabel> notes; // Onsets and offsets relative to anchor_s.
    double anchor_s = 0.0;            // Where the pattern was copied from.
    int period_slots = 0;

    bool empty() const { return notes.empty(); }
};

// The notes at indices (any order) as a pattern. period_slots 0 picks the default period. An empty
// selection gives an empty pattern.
NotePattern copy_pattern(const std::vector<BassNoteLabel> &notes, const std::vector<int> &indices,
                         const Grid &grid, int period_slots = 0);

double pattern_period_s(const NotePattern &pattern, const Grid &grid);

struct PatternPlacement {
    std::vector<BassNoteLabel> notes; // Copy by copy, in pattern order.
    size_t last_copy_first = 0;       // Index in notes where the last copy starts.
    double clear_from_s = 0.0;        // Existing notes with onsets in [from, to) are replaced.
    double clear_to_s = 0.0;
};

// count copies of the pattern, the first anchored at anchor_s, then one per period. Notes with
// onsets at or after limit_s are left out, and the cleared range stops there too.
PatternPlacement place_pattern(const NotePattern &pattern, const Grid &grid, double anchor_s,
                               int count, double limit_s = std::numeric_limits<double>::infinity());

// How many copies follow the pattern's own position before end_s: the count that fills a region.
int copies_until(const NotePattern &pattern, const Grid &grid, double end_s);

} // namespace psyight
