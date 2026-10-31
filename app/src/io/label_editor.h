// Editing model for ground-truth labels (M1 labelling tools), independent of the GUI.
//
// Bass notes stay sorted by onset; edits that reorder notes return the note's new index. Every
// edit is undoable. A gesture (begin_gesture to end_gesture, for example one mouse drag) is a
// single undo step however many edits it makes. Edits keep notes valid: onset >= 0 and at least
// kMinNoteLengthS long.
#pragma once

#include "io/labels.h"

#include <cstddef>
#include <vector>

namespace psyight {

class LabelEditor {
  public:
    static constexpr double kMinNoteLengthS = 0.002;
    static constexpr size_t kMaxUndoSteps = 500;

    explicit LabelEditor(Labels labels = {});

    const Labels &labels() const { return labels_; }
    bool dirty() const { return dirty_; }
    void mark_saved() { dirty_ = false; }
    void mark_unsaved() { dirty_ = true; } // Labels restored from a recovery copy.

    void begin_gesture();
    void end_gesture();

    // Returns the new note's index.
    int add_note(const BassNoteLabel &note);
    // Replaces every field of a note (inspector edits). Returns its new index.
    int set_note(int index, const BassNoteLabel &note);
    // Moves keeping the duration. Returns the new index.
    int move_note(int index, double onset_s, int midi);
    // Returns the new index.
    int resize_note(int index, double onset_s, double offset_s);
    void delete_note(int index);

    // Batch edits for multi-note selections, each one undo step. They return the new indices of
    // the edited notes in input order.
    // First deletes every note with an onset in [clear_from_s, clear_to_s), then adds notes.
    std::vector<int> insert_notes(const std::vector<BassNoteLabel> &notes,
                                  double clear_from_s = 0.0, double clear_to_s = 0.0);
    // Shifts the notes together, keeping their relative timing and intervals: the shift is limited
    // so every onset stays >= 0 and every pitch stays in MIDI 0 to 127.
    std::vector<int> move_notes(const std::vector<int> &indices, double delta_s, int delta_midi);
    void delete_notes(const std::vector<int> &indices);

    void set_grid(const LabelGrid &grid);
    void set_phrases(const LabelPhrases &phrases);
    void set_sections_done(bool done);
    // Replaces the sections (sorted by start) and the edited flag; one undo step (or part of a
    // gesture).
    void set_sections(std::vector<SectionLabel> sections, bool edited);
    void set_events(std::vector<EventLabel> events); // Breaks and fills, sorted; one undo step.
    // Moves every time in the labels by delta_s in one undo step: notes, kicks, beats, downbeats,
    // sections, regions and the grid (for labels made against audio with a constant offset). The
    // shift is limited so no time goes below 0; a grid downbeat that would is moved a whole number
    // of bars later instead, which leaves the grid lines where they were. Returns the shift made.
    double shift_all(double delta_s);
    void add_region(const LabelRegion &region);
    void set_region(int index, const LabelRegion &region);
    void delete_region(int index);

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    // Counts successful undo and redo calls, so views can drop note indices they hold.
    size_t undo_redo_count() const { return undo_redo_count_; }

  private:
    void before_edit();
    int place(BassNoteLabel note); // Validates, inserts in order, returns the index.
    // Removes the notes at indices, adds notes, keeps the order. Returns the added notes' indices.
    std::vector<int> replace(const std::vector<int> &indices,
                             const std::vector<BassNoteLabel> &notes);
    static BassNoteLabel valid(BassNoteLabel note);

    Labels labels_;
    std::vector<Labels> undo_;
    std::vector<Labels> redo_;
    size_t undo_redo_count_ = 0;
    bool dirty_ = false;
    bool in_gesture_ = false;
    bool gesture_saved_ = false;
};

} // namespace psyight
