#include "io/note_pattern.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

double slot_length_s(const Grid &grid) { return grid.slot_length_samples() / grid.sample_rate_hz; }

} // namespace

NotePattern copy_pattern(const std::vector<BassNoteLabel> &notes, const std::vector<int> &indices,
                         const Grid &grid, int period_slots) {
    NotePattern pattern;
    if (indices.empty()) {
        return pattern;
    }
    double first_onset_s = notes[indices.front()].onset_s;
    for (const int i : indices) {
        first_onset_s = std::min(first_onset_s, notes[i].onset_s);
    }
    pattern.anchor_s = samples_to_seconds(
        grid.snap_to_slot(seconds_to_samples(first_onset_s, grid.sample_rate_hz)),
        grid.sample_rate_hz);
    const double slot_s = slot_length_s(grid);
    int covered_slots = 1;
    for (const int i : indices) {
        BassNoteLabel note = notes[i];
        note.onset_s -= pattern.anchor_s;
        note.offset_s -= pattern.anchor_s;
        covered_slots =
            std::max(covered_slots, static_cast<int>(std::lround(note.onset_s / slot_s)) + 1);
        pattern.notes.push_back(note);
    }
    std::stable_sort(
        pattern.notes.begin(), pattern.notes.end(),
        [](const BassNoteLabel &a, const BassNoteLabel &b) { return a.onset_s < b.onset_s; });
    pattern.period_slots = period_slots > 0 ? period_slots
                                            : (covered_slots + kSlotsPerBeat - 1) / kSlotsPerBeat *
                                                  kSlotsPerBeat; // Whole beats.
    return pattern;
}

double pattern_period_s(const NotePattern &pattern, const Grid &grid) {
    return pattern.period_slots * slot_length_s(grid);
}

PatternPlacement place_pattern(const NotePattern &pattern, const Grid &grid, double anchor_s,
                               int count, double limit_s) {
    PatternPlacement placement;
    const double period_s = pattern_period_s(pattern, grid);
    const double half_slot_s = 0.5 * slot_length_s(grid);
    for (int copy = 0; copy < count; ++copy) {
        const double copy_anchor_s = anchor_s + copy * period_s;
        placement.last_copy_first = placement.notes.size();
        for (BassNoteLabel note : pattern.notes) {
            note.onset_s += copy_anchor_s;
            note.offset_s += copy_anchor_s;
            if (note.onset_s < limit_s) {
                placement.notes.push_back(note);
            }
        }
    }
    // A note belongs to the slot nearest its onset; the pattern's onsets all round to its slots.
    placement.clear_from_s = anchor_s - half_slot_s;
    placement.clear_to_s =
        std::min(anchor_s + std::max(count, 0) * period_s - half_slot_s, limit_s);
    return placement;
}

int copies_until(const NotePattern &pattern, const Grid &grid, double end_s) {
    const double period_s = pattern_period_s(pattern, grid);
    if (pattern.empty() || period_s <= 0.0 || end_s <= pattern.anchor_s) {
        return 0;
    }
    // Copy k starts at anchor + k * period; count those that start before end_s. The tolerance
    // keeps a region that ends on a bar line from gaining a copy through rounding.
    constexpr double kTolerancePeriods = 1e-9;
    return std::max(
        0,
        static_cast<int>(std::ceil((end_s - pattern.anchor_s) / period_s - kTolerancePeriods)) - 1);
}

} // namespace psyight
