// Bass roll and labelling tools (SPEC section 9.4, M1): ground-truth notes on the 16th grid with
// time on the shared view axis and one row per semitone, plus a note inspector with every field,
// labelled regions and the labels file controls.
//
// Mouse: double-click empty space to create a note; Ctrl+click to seek; drag on empty space to
// select the notes in a box (Shift adds to the selection); click a note to select it, Shift+click
// to add or remove it; drag a selected note to move the whole selection, or a note's edges to
// resize it; right-click a note to delete it. Times snap to 16ths unless snapping is off or Alt is
// held. Keys: Delete removes the selection, Up and Down move it a semitone (Shift: an octave),
// Ctrl+A selects the notes in view, Escape clears the selection, Ctrl+C copies, Ctrl+V pastes at
// the mouse (or the playhead when the mouse is elsewhere) and Ctrl+D repeats the selection right
// after itself (see io/note_pattern.h for the rules). Undo, redo and save shortcuts are global
// (see main.cpp).
// Behind the notes, the pitch background shows how strongly each semitone sounds over time, so the
// bassline appears as bright segments on its rows. Under the labels, the bass tracker's detected
// notes (amber, more opaque when more confident) and kicks (faint lines); hovering one shows its
// fields, a plain click on one selects it for the inspector, which can copy it to the labels.
#pragma once

#include "gui/session.h"
#include "gui/spectrogram_textures.h"
#include "io/note_pattern.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

class BassRollView {
  public:
    explicit BassRollView(Session &session) : session_(session) {}

    void draw_roll();
    void draw_inspector();

  private:
    enum class Drag { none, move, resize_start, resize_end };

    void handle_mouse(const LoadedTrack &track);
    void handle_box_select(double mouse_s, double mouse_row);
    void handle_keys();
    double snapped(double t_s) const;
    int hovered_note(bool &near_start, bool &near_end) const;
    void draw_notes() const;
    void draw_detections() const;
    const std::vector<BassNote> *detections() const; // Null before an analysis exists.
    int detection_under_mouse() const;               // Index into detections(), or -1.
    void draw_inspector_detection();
    void draw_regions() const;
    void draw_inspector_note();
    void draw_inspector_selection();
    void draw_inspector_regions(const LoadedTrack &track);

    // Selection: indices into the labels' notes, ascending. Undo, redo and loading a track
    // invalidate indices, so they clear it.
    void sync_selection();
    bool is_selected(int index) const;
    void set_selection(std::vector<int> indices);
    void select_in_view();
    void move_selection(double delta_s, int delta_midi);
    // Copy, paste and duplicate: see io/note_pattern.h.
    NotePattern selection_pattern() const;
    void paste(double anchor_s);
    void duplicate(int count, double limit_s);

    Session &session_;
    std::vector<int> selection_;
    const LoadedTrack *selection_track_ = nullptr;
    size_t selection_undo_redo_count_ = 0;
    Drag drag_ = Drag::none;
    int grabbed_ = -1;        // The note a drag started on.
    bool drag_moved_ = false; // A click on a selected note that never moved selects only it.
    bool boxing_ = false;
    bool box_dragged_ = false;
    int press_detection_ = -1; // Detection under a press on empty space: a plain click selects it.
    int selected_detection_ = -1;
    bool show_detections_ = true;
    double box_start_s_ = 0.0;
    double box_start_row_ = 0.0;         // Plot y: MIDI row, fractional.
    std::vector<int> box_base_;          // The selection a Shift box adds to.
    std::optional<double> mouse_time_s_; // Over the roll this frame: where Ctrl+V pastes.
    NotePattern clipboard_;
    int repeat_period_choice_ = 0; // Index into kRepeatPeriods (bass_roll_view.cpp).
    int repeat_count_ = 1;
    double grab_offset_s_ = 0.0;              // Mouse time minus onset when a move started.
    double default_length_s_ = 0.5 * 0.09375; // The last edited length; half a slot at first.
    bool snap_ = true;
    std::array<char, 16> pitch_text_{};
    bool editing_pitch_ = false; // While typing, the field is not refilled from the note.
    std::string pitch_error_;
    // Pitch background: harmonic-sum salience on the semitone rows
    // (FrontEndResult::pitch_salience).
    SpectrogramTextures salience_textures_;
    bool show_pitch_ = true;
    float pitch_min_db_ = -80.0f; // Set from the track's own values when it loads.
    float pitch_max_db_ = -20.0f;
    bool pitch_range_changed_ = false;
};

} // namespace psyight
