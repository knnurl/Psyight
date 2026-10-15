#include "io/label_editor.h"

#include <algorithm>
#include <utility>

namespace psyight {

LabelEditor::LabelEditor(Labels labels) : labels_(std::move(labels)) {
    std::stable_sort(
        labels_.bass_notes.begin(), labels_.bass_notes.end(),
        [](const BassNoteLabel &a, const BassNoteLabel &b) { return a.onset_s < b.onset_s; });
}

void LabelEditor::begin_gesture() {
    in_gesture_ = true;
    gesture_saved_ = false;
}

void LabelEditor::end_gesture() { in_gesture_ = false; }

void LabelEditor::before_edit() {
    if (in_gesture_ && gesture_saved_) {
        return; // The gesture's first edit already saved the state to return to.
    }
    undo_.push_back(labels_);
    if (undo_.size() > kMaxUndoSteps) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    gesture_saved_ = in_gesture_;
    dirty_ = true;
}

BassNoteLabel LabelEditor::valid(BassNoteLabel note) {
    const double length_s = std::max(note.offset_s - note.onset_s, kMinNoteLengthS);
    note.onset_s = std::max(0.0, note.onset_s);
    note.offset_s = note.onset_s + length_s;
    note.midi = std::clamp(note.midi, 0, 127);
    return note;
}

int LabelEditor::place(BassNoteLabel note) {
    note = valid(note);
    std::vector<BassNoteLabel> &notes = labels_.bass_notes;
    const auto position = std::upper_bound(
        notes.begin(), notes.end(), note.onset_s,
        [](double onset_s, const BassNoteLabel &other) { return onset_s < other.onset_s; });
    return static_cast<int>(notes.insert(position, note) - notes.begin());
}

int LabelEditor::add_note(const BassNoteLabel &note) {
    before_edit();
    return place(note);
}

int LabelEditor::set_note(int index, const BassNoteLabel &note) {
    before_edit();
    labels_.bass_notes.erase(labels_.bass_notes.begin() + index);
    return place(note);
}

int LabelEditor::move_note(int index, double onset_s, int midi) {
    BassNoteLabel note = labels_.bass_notes[index];
    const double length_s = note.offset_s - note.onset_s;
    note.onset_s = onset_s;
    note.offset_s = onset_s + length_s;
    note.midi = midi;
    return set_note(index, note);
}

int LabelEditor::resize_note(int index, double onset_s, double offset_s) {
    BassNoteLabel note = labels_.bass_notes[index];
    note.onset_s = onset_s;
    note.offset_s = offset_s;
    return set_note(index, note);
}

void LabelEditor::delete_note(int index) {
    before_edit();
    labels_.bass_notes.erase(labels_.bass_notes.begin() + index);
}

void LabelEditor::set_grid(const LabelGrid &grid) {
    if (grid == labels_.grid) {
        return;
    }
    before_edit();
    labels_.grid = grid;
}

void LabelEditor::add_region(const LabelRegion &region) {
    before_edit();
    labels_.regions.push_back(region);
}

void LabelEditor::set_region(int index, const LabelRegion &region) {
    before_edit();
    labels_.regions[index] = region;
}

void LabelEditor::delete_region(int index) {
    before_edit();
    labels_.regions.erase(labels_.regions.begin() + index);
}

bool LabelEditor::undo() {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(std::move(labels_));
    labels_ = std::move(undo_.back());
    undo_.pop_back();
    dirty_ = true;
    return true;
}

bool LabelEditor::redo() {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(std::move(labels_));
    labels_ = std::move(redo_.back());
    redo_.pop_back();
    dirty_ = true;
    return true;
}

} // namespace psyight
