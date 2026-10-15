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

    void set_grid(const LabelGrid &grid);
    void add_region(const LabelRegion &region);
    void set_region(int index, const LabelRegion &region);
    void delete_region(int index);

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo();
    bool redo();

  private:
    void before_edit();
    int place(BassNoteLabel note); // Validates, inserts in order, returns the index.
    static BassNoteLabel valid(BassNoteLabel note);

    Labels labels_;
    std::vector<Labels> undo_;
    std::vector<Labels> redo_;
    bool dirty_ = false;
    bool in_gesture_ = false;
    bool gesture_saved_ = false;
};

} // namespace psyight
