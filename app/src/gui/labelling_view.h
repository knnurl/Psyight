// The labelling mode (View > Label sections, F9): a full-window layout for labelling a track's
// sections and phrase grid quickly, for the section metrics and tuning
// (docs/PLAN_SECTIONS_EFFECTS.md).
//
// Left: the track list, from music folders the operator adds (remembered in the settings), with
// each track's status from its labels file (not started, in progress, done), filtered by name and
// status. Right: the loaded track: an overview of the whole track and a zoomed view following the
// playhead, each with the labelled sections (editable), the detection (a thin strip), the bar and
// phrase grid, and per-bar lanes (kick, level, riser) to see where the music changes.
//
// It starts from the detection: the first edit copies the detected sections, snapped to bars,
// into the labels. Keys (when no text field has the focus):
//   Space play or pause; Left and Right a bar (Shift: a phrase); [ and ] the previous or next
//   boundary; Home the start.
//   1 to 6 mark intro, groove, breakdown, build, drop, outro from the playhead, snapped to the
//   nearest bar (Shift: phrase; Alt: exactly); a boundary within half a bar takes the label.
//   Delete removes the boundary nearest the playhead (within a bar).
//   P: a phrase starts at the nearest bar. D: the nearest beat becomes a downbeat.
//   Enter marks the track done (or not); Ctrl+S saves; N saves and opens the next unfinished
//   track; Esc leaves the mode. Ctrl+Z and Ctrl+Y undo and redo (the labels).
// Boundaries drag on either view (snapped to bars, Alt free); right-click a section to relabel
// it or remove its boundary. Labels are saved only on save, as everywhere in the app.
#pragma once

#include "gui/session.h"
#include "io/section_marks.h"
#include "io/track_library.h"
#include "io/user_settings.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct ImDrawList;

namespace psyight {

class LabellingView {
  public:
    explicit LabellingView(Session &session) : session_(session) {}

    void draw(bool *open); // Over the whole work area while open.
    // Runs a requested folder dialog after the frame is shown (owner: the native window).
    void after_frame(void *owner);
    void apply_settings(const UserSettings &settings);
    void store_settings(UserSettings &settings) const;

  private:
    struct Span {
        double from_s;
        double to_s;
    };

    void draw_library(float width);
    void draw_track();
    void draw_header();
    void draw_grid_controls();
    // Moves the grid (and the labelled phrase start and, with grid_moves_sections_, the sections)
    // by by_s; part of the current undo gesture when one is open.
    void shift_grid(double by_s);
    // A time strip: the sections (labelled, then detected), the grid and the lanes over `span`.
    // Clicks seek; boundaries drag. `lanes`: draw the per-bar kick, level and riser lanes.
    void draw_timeline(const char *id, Span span, float height, bool lanes);
    void draw_waveform(ImDrawList &draw, const LoadedTrack &track, float y, float h, float x0,
                       float width, const std::function<double(float)> &t_of) const;
    void draw_section_table();
    void handle_keys();
    void rescan();
    void open_track(const std::filesystem::path &path);

    // The labelled sections when edited, else the detection snapped to bars (what an edit
    // starts from).
    std::vector<SectionLabel> working_sections() const;
    bool sections_labelled() const;
    void commit(std::vector<SectionLabel> sections); // One undo step; marks them edited.
    // The labelled phrase grid, or the detection's when none is labelled.
    LabelPhrases effective_phrases() const;
    double playhead_s() const;
    void seek(double t_s);
    double snapped(double t_s) const; // By the held modifier: bar, phrase (Shift) or free (Alt).
    void mark(int label);             // Keys 1 to 6.
    Result<void> save();

    Session &session_;
    std::vector<std::filesystem::path> folders_;
    std::vector<TrackEntry> tracks_;
    bool scanned_ = false;
    bool folder_requested_ = false;
    std::string search_;
    std::string filter_ = "all"; // all, none, started, done.
    std::string message_;        // The last save or scan outcome.
    float zoom_bars_ = 32.0f;    // The zoomed view's width.
    int dragging_ = -1;          // The boundary being dragged (index into the sections).
    // Panning the zoomed view by dragging its waveform and lanes (the view follows the playhead,
    // so a pan moves the playhead); a press that does not move seeks to where it was.
    bool panning_ = false;
    bool grid_dragging_ = false; // Ctrl and a drag on the waveform: the grid follows the mouse.
    bool grid_moves_sections_ = true;
    float pan_start_x_ = 0.0f;
    float pan_last_x_ = 0.0f;
    int menu_section_ = -1; // The section whose right-click menu is open.
    double menu_time_s_ = 0.0;
    const LoadedTrack *seen_track_ = nullptr; // Rescans the list's status after a track change.
    int last_frame_ = -10;                    // The last frame drawn: detects opening.
};

} // namespace psyight
