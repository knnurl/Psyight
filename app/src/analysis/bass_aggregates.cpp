#include "analysis/bass_aggregates.h"

#include <algorithm>

namespace psyight {

namespace {

// Aggregate of the notes with onsets in [start_s, end_s), counting rests over its 16th slots.
BassAggregate aggregate(const std::vector<BassNote> &notes, double start_s, double end_s,
                        double slot_s) {
    BassAggregate a;
    a.start_s = start_s;
    a.end_s = end_s;
    double gate_sum = 0.0;
    double gate_sq = 0.0;
    for (const BassNote &note : notes) {
        if (note.onset_s < start_s || note.onset_s >= end_s) {
            continue;
        }
        if (a.notes == 0) {
            a.pitch_low = a.pitch_high = note.pitch_midi;
        }
        ++a.notes;
        gate_sum += note.gate_ratio;
        gate_sq += note.gate_ratio * note.gate_ratio;
        a.slides += note.slide_in;
        a.pitch_low = std::min(a.pitch_low, note.pitch_midi);
        a.pitch_high = std::max(a.pitch_high, note.pitch_midi);
    }
    if (a.notes > 0) {
        a.gate_mean = gate_sum / a.notes;
        a.gate_variance = std::max(0.0, gate_sq / a.notes - a.gate_mean * a.gate_mean);
    }
    // Rests: notes are sorted by onset, so only those starting before each slot can cover it.
    for (double middle = start_s + 0.5 * slot_s; middle < end_s; middle += slot_s) {
        bool covered = false;
        for (const BassNote &note : notes) {
            if (note.onset_s > middle) {
                break;
            }
            covered = covered || note.offset_s > middle;
        }
        a.rests += !covered;
    }
    return a;
}

} // namespace

BassAggregates aggregate_bass(const std::vector<BassNote> &notes, const Grid &grid,
                              double duration_s, int bars_per_phrase, int phrase_bar_offset) {
    BassAggregates result;
    const double slot_s = grid.slot_length_samples() / grid.sample_rate_hz;
    const double bar_s = kSlotsPerBar * slot_s;
    const double first_s = samples_to_seconds(grid.first_downbeat, grid.sample_rate_hz);
    if (slot_s <= 0.0 || bars_per_phrase < 1) {
        return result;
    }
    int bars = 0;
    while (first_s + (bars + 1) * bar_s <= duration_s) {
        ++bars;
    }
    for (int m = 0; m < bars; ++m) {
        result.bars.push_back(
            aggregate(notes, first_s + m * bar_s, first_s + (m + 1) * bar_s, slot_s));
    }
    const int offset = ((phrase_bar_offset % bars_per_phrase) + bars_per_phrase) % bars_per_phrase;
    for (int m = 0; m < bars;) {
        int next = m + 1;
        while (next < bars && (next - offset) % bars_per_phrase != 0) {
            ++next;
        }
        result.phrases.push_back(
            aggregate(notes, first_s + m * bar_s, first_s + next * bar_s, slot_s));
        m = next;
    }
    return result;
}

} // namespace psyight
