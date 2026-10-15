#include "core/pitch.h"
#include "io/label_editor.h"

#include <doctest/doctest.h>

using namespace psyight;

namespace {

BassNoteLabel note(double onset_s, double offset_s, int midi = 29) {
    return {onset_s, offset_s, midi, 0.0, false, false, 1.0};
}

std::vector<double> onsets(const LabelEditor &editor) {
    std::vector<double> result;
    for (const BassNoteLabel &n : editor.labels().bass_notes) {
        result.push_back(n.onset_s);
    }
    return result;
}

} // namespace

TEST_CASE("pitch names: MIDI 29 is F1 and names round trip over the bass range") {
    CHECK(midi_to_note_name(29) == "F1");
    CHECK(midi_to_note_name(60) == "C4");
    CHECK(midi_to_note_name(0) == "C-1");
    CHECK(midi_to_note_name(30) == "F#1");
    for (int midi = 0; midi <= 127; ++midi) {
        CAPTURE(midi);
        const Result<int> parsed = parse_pitch(midi_to_note_name(midi));
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == midi);
    }
}

TEST_CASE("pitch names: numbers, flats, lower case and whitespace parse; junk does not") {
    CHECK(parse_pitch("29").value() == 29);
    CHECK(parse_pitch(" Gb1 ").value() == 30);
    CHECK(parse_pitch("f#1").value() == 30);
    CHECK(parse_pitch("Bb0").value() == 22);
    CHECK(parse_pitch("Cb1").value() == 23); // Enharmonic B0.
    for (const char *bad : {"", "H1", "F", "F#", "F1.5", "128", "-1", "G9#", "G10", "x"}) {
        CAPTURE(bad);
        CHECK_FALSE(parse_pitch(bad).ok());
    }
}

TEST_CASE("label editor: notes stay sorted and edits return the moved note's index") {
    LabelEditor editor;
    CHECK(editor.add_note(note(2.0, 2.05)) == 0);
    CHECK(editor.add_note(note(1.0, 1.05)) == 0);
    CHECK(editor.add_note(note(3.0, 3.05)) == 2);
    CHECK(onsets(editor) == std::vector<double>{1.0, 2.0, 3.0});
    const int moved = editor.move_note(0, 2.5, 31);
    CHECK(moved == 1);
    CHECK(onsets(editor) == std::vector<double>{2.0, 2.5, 3.0});
    CHECK(editor.labels().bass_notes[1].offset_s == doctest::Approx(2.55)); // Duration kept.
    CHECK(editor.labels().bass_notes[1].midi == 31);
    CHECK(editor.dirty());
}

TEST_CASE("label editor: notes are kept valid") {
    LabelEditor editor;
    const int i = editor.add_note(note(-0.5, -0.4, 200));
    const BassNoteLabel &n = editor.labels().bass_notes[i];
    CHECK(n.onset_s == 0.0);
    CHECK(n.offset_s == doctest::Approx(0.1)); // Length kept.
    CHECK(n.midi == 127);
    const int j = editor.resize_note(i, 1.0, 0.9); // Offset before onset.
    CHECK(editor.labels().bass_notes[j].offset_s - editor.labels().bass_notes[j].onset_s ==
          doctest::Approx(LabelEditor::kMinNoteLengthS));
}

TEST_CASE("label editor: undo and redo restore exact states; a new edit clears redo") {
    LabelEditor editor;
    editor.add_note(note(1.0, 1.05));
    editor.add_note(note(2.0, 2.05));
    editor.delete_note(0);
    CHECK(onsets(editor) == std::vector<double>{2.0});
    CHECK(editor.undo());
    CHECK(onsets(editor) == std::vector<double>{1.0, 2.0});
    CHECK(editor.undo());
    CHECK(onsets(editor) == std::vector<double>{1.0});
    CHECK(editor.redo());
    CHECK(onsets(editor) == std::vector<double>{1.0, 2.0});
    editor.add_note(note(3.0, 3.05));
    CHECK_FALSE(editor.can_redo());
    CHECK(editor.undo());
    CHECK(editor.undo());
    CHECK(editor.undo());
    CHECK(onsets(editor).empty());
    CHECK_FALSE(editor.undo());
}

TEST_CASE("label editor: one drag gesture is one undo step") {
    LabelEditor editor;
    int i = editor.add_note(note(1.0, 1.05));
    editor.begin_gesture();
    for (int step = 1; step <= 20; ++step) {
        i = editor.move_note(i, 1.0 + 0.01 * step, 29);
    }
    editor.end_gesture();
    CHECK(editor.labels().bass_notes[0].onset_s == doctest::Approx(1.2));
    CHECK(editor.undo());
    CHECK(editor.labels().bass_notes[0].onset_s == 1.0);
    CHECK(editor.undo());
    CHECK(editor.labels().bass_notes.empty());
}

TEST_CASE("label editor: regions and grid edits are undoable; save clears dirty") {
    LabelEditor editor;
    CHECK_FALSE(editor.dirty());
    editor.add_region({60.0, 120.0, false});
    editor.set_region(0, {60.0, 120.0, true});
    editor.set_grid({160.0, 0.25});
    CHECK(editor.labels().regions[0].complete);
    editor.mark_saved();
    CHECK_FALSE(editor.dirty());
    editor.set_grid({160.0, 0.25}); // No change: no undo step, still clean.
    CHECK_FALSE(editor.dirty());
    CHECK(editor.undo());
    CHECK(editor.labels().grid.first_downbeat_s == 0.0);
    CHECK(editor.undo());
    CHECK_FALSE(editor.labels().regions[0].complete);
    editor.delete_region(0);
    CHECK(editor.labels().regions.empty());
}

TEST_CASE("label editor: a loaded file with unsorted notes is sorted on load") {
    Labels labels;
    labels.bass_notes = {note(3.0, 3.1), note(1.0, 1.1), note(2.0, 2.1)};
    const LabelEditor editor(labels);
    CHECK(onsets(editor) == std::vector<double>{1.0, 2.0, 3.0});
    CHECK_FALSE(editor.dirty());
}
