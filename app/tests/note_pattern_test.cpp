// Copy, paste and duplicate rules for labelled bass notes.

#include "io/note_pattern.h"

#include <doctest/doctest.h>

#include <vector>

using namespace psyight;

namespace {

// 150 BPM at 48 kHz: a slot is 4800 samples, 0.1 s. First downbeat at 1 s.
const Grid kGrid{150.0, 48000, 48000};
constexpr double kSlotS = 0.1;

BassNoteLabel note(double onset_s, double length_s, int midi = 28) {
    return {onset_s, onset_s + length_s, midi, 0.0, false, false, 1.0};
}

// One bar of K-B-B-B from the downbeat at bar_s: bass on slots 1, 2, 3 of every beat.
std::vector<BassNoteLabel> rolling_bar(double bar_s) {
    std::vector<BassNoteLabel> notes;
    for (int beat = 0; beat < 4; ++beat) {
        for (int slot = 1; slot < 4; ++slot) {
            notes.push_back(note(bar_s + (4 * beat + slot) * kSlotS, kSlotS));
        }
    }
    return notes;
}

std::vector<int> all(const std::vector<BassNoteLabel> &notes) {
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        indices.push_back(i);
    }
    return indices;
}

} // namespace

TEST_CASE("note pattern: one bar of K-B-B-B repeats every bar, one beat of it every beat") {
    const std::vector<BassNoteLabel> bar = rolling_bar(1.0);
    const NotePattern pattern = copy_pattern(bar, all(bar), kGrid);
    CHECK(pattern.anchor_s == doctest::Approx(1.1)); // Slot 1: the first bass note.
    CHECK(pattern.period_slots == 16);               // Onsets cover 15 slots: a whole bar.
    CHECK(pattern.notes.front().onset_s == doctest::Approx(0.0));
    CHECK(pattern_period_s(pattern, kGrid) == doctest::Approx(1.6));
    CHECK(copy_pattern(bar, {0, 1, 2}, kGrid).period_slots == 4);      // One beat's bass notes.
    CHECK(copy_pattern(bar, {5}, kGrid).period_slots == 4);            // A single note: one beat.
    CHECK(copy_pattern(bar, {0, 1, 2}, kGrid, 16).period_slots == 16); // Explicit period.
    CHECK(copy_pattern(bar, {}, kGrid).empty());
}

TEST_CASE("note pattern: notes off the grid keep their offset; selection order does not matter") {
    // Labelled 4 ms late and 3 ms early.
    const std::vector<BassNoteLabel> notes = {note(1.104, 0.08, 28), note(1.197, 0.09, 40)};
    const NotePattern pattern = copy_pattern(notes, {1, 0}, kGrid);
    CHECK(pattern.anchor_s == doctest::Approx(1.1));
    REQUIRE(pattern.notes.size() == 2);
    CHECK(pattern.notes[0].onset_s == doctest::Approx(0.004)); // Sorted by onset.
    CHECK(pattern.notes[0].offset_s == doctest::Approx(0.084));
    CHECK(pattern.notes[1].onset_s == doctest::Approx(0.097));
    CHECK(pattern.notes[1].midi == 40);

    const PatternPlacement placed = place_pattern(pattern, kGrid, 5.0, 1);
    REQUIRE(placed.notes.size() == 2);
    CHECK(placed.notes[0].onset_s == doctest::Approx(5.004));
    CHECK(placed.notes[1].onset_s == doctest::Approx(5.097));
    CHECK(placed.notes[1].midi == 40); // Pitch kept.
}

TEST_CASE("note pattern: placement clears the covered slots with half-slot margins") {
    const std::vector<BassNoteLabel> bar = rolling_bar(1.0);
    const NotePattern pattern = copy_pattern(bar, all(bar), kGrid);
    const double next_bar_s = pattern.anchor_s + 1.6;
    const PatternPlacement placed = place_pattern(pattern, kGrid, next_bar_s, 3);
    CHECK(placed.notes.size() == 36);
    CHECK(placed.last_copy_first == 24);
    CHECK(placed.notes[24].onset_s == doctest::Approx(next_bar_s + 2 * 1.6));
    CHECK(placed.clear_from_s == doctest::Approx(next_bar_s - 0.05));
    CHECK(placed.clear_to_s == doctest::Approx(next_bar_s + 3 * 1.6 - 0.05));
    // Duplicating right after the source never clears the source itself.
    for (const BassNoteLabel &source : bar) {
        CHECK(source.onset_s < placed.clear_from_s);
    }
}

TEST_CASE("note pattern: a limit drops later notes and stops the cleared range") {
    const std::vector<BassNoteLabel> bar = rolling_bar(1.0);
    const NotePattern pattern = copy_pattern(bar, all(bar), kGrid);
    const PatternPlacement placed = place_pattern(pattern, kGrid, 2.7, 2, 3.95);
    // Copy 0 starts at 2.7; its onsets 0 to 1.2 s after that fall before 3.95 (10 of the 12
    // notes). Copy 1, from 4.3, is dropped entirely.
    CHECK(placed.notes.size() == 10);
    CHECK(placed.notes.back().onset_s == doctest::Approx(3.9));
    CHECK(placed.clear_to_s == 3.95);
}

TEST_CASE("note pattern: copies_until fills a region, exactly on bar lines too") {
    const std::vector<BassNoteLabel> bar = rolling_bar(1.0);
    const NotePattern pattern = copy_pattern(bar, all(bar), kGrid); // Anchor 1.1, period 1.6.
    CHECK(copies_until(pattern, kGrid, 1.1 + 4 * 1.6) == 3);        // Ends on a copy's start.
    CHECK(copies_until(pattern, kGrid, 1.1 + 4 * 1.6 + 0.01) == 4);
    CHECK(copies_until(pattern, kGrid, 2.0) == 0);
    CHECK(copies_until(pattern, kGrid, 0.5) == 0); // Region end before the pattern.
    CHECK(copies_until(NotePattern{}, kGrid, 100.0) == 0);

    // At 150 BPM over the owner's 61 s region, from a bar starting on a bar line.
    const Grid grid{150.0, seconds_to_samples(16.017, 48000), 48000};
    const std::vector<BassNoteLabel> first_bar = rolling_bar(16.017 + 48 * 1.6);
    const NotePattern owner = copy_pattern(first_bar, all(first_bar), grid);
    const int copies = copies_until(owner, grid, 154.44);
    const PatternPlacement fill = place_pattern(owner, grid, owner.anchor_s + 1.6, copies, 154.44);
    CHECK(fill.notes.back().onset_s < 154.44);
    CHECK(fill.notes.back().onset_s > 154.44 - 1.6);
}
