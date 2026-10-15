// Bass roll and labelling tools (SPEC section 9.4, M1): ground-truth notes on the 16th grid with
// time on the shared view axis and one row per semitone, plus a note inspector with every field,
// labelled regions and the labels file controls.
//
// Mouse: double-click empty space to create a note; Ctrl+click to seek; drag a note to
// move it, or its edges to resize it; right-click a note to delete it. Times snap to 16ths unless
// snapping is off or Alt is held. Keys: Delete removes the selection, Up and Down move it a
// semitone (Shift: an octave). Undo, redo and save shortcuts are global (see main.cpp).
// Behind the notes, the pitch background shows how strongly each semitone sounds over time, so the
// bassline appears as bright segments on its rows.
#pragma once

#include "gui/session.h"
#include "gui/spectrogram_textures.h"

#include <array>
#include <string>

namespace psyight {

class BassRollView {
  public:
    explicit BassRollView(Session &session) : session_(session) {}

    void draw_roll();
    void draw_inspector();

  private:
    enum class Drag { none, move, resize_start, resize_end };

    void handle_mouse(const LoadedTrack &track);
    void handle_keys();
    double snapped(double t_s) const;
    int hovered_note(bool &near_start, bool &near_end) const;
    void draw_notes() const;
    void draw_regions() const;
    void draw_inspector_note();
    void draw_inspector_regions(const LoadedTrack &track);

    Session &session_;
    int selected_ = -1;
    Drag drag_ = Drag::none;
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
