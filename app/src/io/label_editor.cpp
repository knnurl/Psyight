#include "io/label_editor.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

std::vector<int> LabelEditor::replace(const std::vector<int> &indices,
                                      const std::vector<BassNoteLabel> &notes) {
    // Tag each note with its position in notes (-1 for kept notes), sort, then read the tags back.
    // Kept notes come first, so like place(), an added note goes after equal onsets.
    std::vector<bool> removed(labels_.bass_notes.size(), false);
    for (const int i : indices) {
        removed[i] = true;
    }
    std::vector<std::pair<BassNoteLabel, int>> tagged;
    for (size_t i = 0; i < labels_.bass_notes.size(); ++i) {
        if (!removed[i]) {
            tagged.emplace_back(labels_.bass_notes[i], -1);
        }
    }
    for (size_t i = 0; i < notes.size(); ++i) {
        tagged.emplace_back(valid(notes[i]), static_cast<int>(i));
    }
    std::stable_sort(tagged.begin(), tagged.end(), [](const auto &a, const auto &b) {
        return a.first.onset_s < b.first.onset_s;
    });
    std::vector<int> new_indices(notes.size());
    labels_.bass_notes.clear();
    for (const auto &[note, tag] : tagged) {
        if (tag >= 0) {
            new_indices[tag] = static_cast<int>(labels_.bass_notes.size());
        }
        labels_.bass_notes.push_back(note);
    }
    return new_indices;
}

std::vector<int> LabelEditor::insert_notes(const std::vector<BassNoteLabel> &notes,
                                           double clear_from_s, double clear_to_s) {
    std::vector<int> cleared;
    for (int i = 0; i < static_cast<int>(labels_.bass_notes.size()); ++i) {
        const double onset_s = labels_.bass_notes[i].onset_s;
        if (onset_s >= clear_from_s && onset_s < clear_to_s) {
            cleared.push_back(i);
        }
    }
    if (notes.empty() && cleared.empty()) {
        return {};
    }
    before_edit();
    return replace(cleared, notes);
}

std::vector<int> LabelEditor::move_notes(const std::vector<int> &indices, double delta_s,
                                         int delta_midi) {
    if (indices.empty()) {
        return {};
    }
    double min_onset_s = labels_.bass_notes[indices.front()].onset_s;
    int min_midi = 127;
    int max_midi = 0;
    for (const int i : indices) {
        const BassNoteLabel &note = labels_.bass_notes[i];
        min_onset_s = std::min(min_onset_s, note.onset_s);
        min_midi = std::min(min_midi, note.midi);
        max_midi = std::max(max_midi, note.midi);
    }
    delta_s = std::max(delta_s, -min_onset_s);
    delta_midi = std::clamp(delta_midi, -min_midi, 127 - max_midi);
    if (delta_s == 0.0 && delta_midi == 0) {
        return indices;
    }
    std::vector<BassNoteLabel> moved;
    for (const int i : indices) {
        BassNoteLabel note = labels_.bass_notes[i];
        note.onset_s += delta_s;
        note.offset_s += delta_s;
        note.midi += delta_midi;
        moved.push_back(note);
    }
    before_edit();
    return replace(indices, moved);
}

void LabelEditor::delete_notes(const std::vector<int> &indices) {
    if (indices.empty()) {
        return;
    }
    before_edit();
    replace(indices, {});
}

void LabelEditor::set_grid(const LabelGrid &grid) {
    if (grid == labels_.grid) {
        return;
    }
    before_edit();
    labels_.grid = grid;
}

void LabelEditor::set_phrases(const LabelPhrases &phrases) {
    if (phrases == labels_.phrases) {
        return;
    }
    before_edit();
    labels_.phrases = phrases;
}

void LabelEditor::set_sections_done(bool done) {
    if (done == labels_.sections_done) {
        return;
    }
    before_edit();
    labels_.sections_done = done;
}

void LabelEditor::set_events(std::vector<EventLabel> events) {
    std::sort(events.begin(), events.end(),
              [](const EventLabel &a, const EventLabel &b) { return a.start_s < b.start_s; });
    if (events == labels_.events) {
        return;
    }
    before_edit();
    labels_.events = std::move(events);
}

void LabelEditor::set_sections(std::vector<SectionLabel> sections, bool edited) {
    std::sort(sections.begin(), sections.end(),
              [](const SectionLabel &a, const SectionLabel &b) { return a.start_s < b.start_s; });
    if (sections == labels_.sections && edited == labels_.sections_edited) {
        return;
    }
    before_edit();
    labels_.sections = std::move(sections);
    labels_.sections_edited = edited;
}

double LabelEditor::shift_all(double delta_s) {
    double earliest_s = std::numeric_limits<double>::infinity();
    for (const BassNoteLabel &n : labels_.bass_notes) {
        earliest_s = std::min(earliest_s, n.onset_s);
    }
    for (const std::vector<LabelEvent> *events :
         {&labels_.kicks, &labels_.beats, &labels_.downbeats}) {
        for (const LabelEvent &e : *events) {
            earliest_s = std::min(earliest_s, e.t_s);
        }
    }
    for (const SectionLabel &s : labels_.sections) {
        earliest_s = std::min(earliest_s, s.start_s);
    }
    for (const EventLabel &e : labels_.events) {
        earliest_s = std::min(earliest_s, e.start_s);
    }
    for (const LabelRegion &r : labels_.regions) {
        earliest_s = std::min(earliest_s, r.start_s);
    }
    if (labels_.phrases.set) {
        earliest_s = std::min(earliest_s, labels_.phrases.start_s);
    }
    if (std::isfinite(earliest_s)) {
        delta_s = std::max(delta_s, -earliest_s);
    }
    if (delta_s == 0.0 || !std::isfinite(delta_s)) {
        return 0.0;
    }
    before_edit();
    for (BassNoteLabel &n : labels_.bass_notes) {
        n.onset_s += delta_s;
        n.offset_s += delta_s;
    }
    for (std::vector<LabelEvent> *events : {&labels_.kicks, &labels_.beats, &labels_.downbeats}) {
        for (LabelEvent &e : *events) {
            e.t_s += delta_s;
        }
    }
    for (SectionLabel &s : labels_.sections) {
        s.start_s += delta_s;
    }
    for (EventLabel &e : labels_.events) {
        e.start_s += delta_s;
    }
    for (LabelRegion &r : labels_.regions) {
        r.start_s += delta_s;
        r.end_s += delta_s;
    }
    if (labels_.phrases.set) {
        labels_.phrases.start_s += delta_s;
    }
    labels_.grid.first_downbeat_s += delta_s;
    if (labels_.grid.first_downbeat_s < 0.0 && labels_.grid.bpm > 0.0) {
        const double bar_s = 4.0 * 60.0 / labels_.grid.bpm;
        labels_.grid.first_downbeat_s += std::ceil(-labels_.grid.first_downbeat_s / bar_s) * bar_s;
    }
    return delta_s;
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
    ++undo_redo_count_;
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
    ++undo_redo_count_;
    return true;
}

} // namespace psyight
