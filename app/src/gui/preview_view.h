// Preview (SPEC section 9.4, M3): the rig's pixels drawn at their positions, as the virtual node
// received them from the show thread (render at the playhead plus the latency offset, limiters,
// patch, packets), and the estimated supply current, in the "Preview" window (pinned top right;
// it folds away). The show controls (live input, MIDI, scenes, layers, palettes, limiters,
// output) are in the "Show" window. In show mode the rig may be drawn large in the "Stage" window
// instead. File mode: the analysis is known ahead, so the render time may
// lead the playhead (SPEC 4.3). The panel owns the show thread: it publishes a new config when a
// setting, the track or the grid changes, and draws the latest frame; neither side waits for the
// other. It also owns the top bar's show controls (SPEC 9.1 and 9.3): the state ladder (Preflight
// and Preview keep the network off, Ambient sends the ambient scene only, React sends with scenes
// held by hand, Show runs the section automation) and Blackout (B) and Hold (H), fixed at the
// right and acting at once. The app starts in Preview. Record writes a session folder
// (output/recorder.h) under recordings/ in the working directory; it stops by itself when the
// track or its analysis changes.
#pragma once

#include "gui/project_state.h"
#include "gui/session.h"
#include "io/midi_input.h"
#include "io/user_settings.h"
#include "output/show.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace psyight {

class PreviewView {
  public:
    PreviewView(Session &session, ProjectState &project);

    // The Show window, and the Preview window unless folded. stage (show mode with the editing
    // panels hidden): the rig drawn large in the "Stage" window, where the bass roll was, instead.
    void draw(bool stage = false);
    // Inside the main menu bar.
    void draw_top_bar();
    bool sending() const { return config_.send_to_network; }
    // React or Show on the top bar: the app may switch to its show look (main.cpp).
    bool performing() const { return ladder_ == Ladder::react || ladder_ == Ladder::show; }
    bool &preview_open() { return preview_open_; } // View > Rig preview.
    void apply_settings(const UserSettings &settings) { preview_open_ = settings.preview_open; }
    void store_settings(UserSettings &settings) const { settings.preview_open = preview_open_; }

  private:
    enum class Ladder { preflight, preview, ambient, react, show };

    void set_ladder(Ladder step);
    void start_recording();
    void stop_recording(const std::string &why);
    void record_params();
    int ambient_scene() const;
    void sync_config(); // Publishes config_ when it or the track changed.
    void draw_live();   // Live input: source, start and stop, status, tap test (M5).
    void tap();         // Tap test: one tap now.
    void draw_midi();   // MIDI: port, learn and bindings (M5).
    void poll_midi();   // Applies the controller's messages; once a frame.
    void apply_midi(const MidiAction &action);
    void draw_controls(const ShowFrame &frame);
    void draw_scene_cards(const ShowFrame &frame);
    void draw_scene_editor(std::vector<Scene> &scenes);
    void draw_routes(Scene &scene);
    void draw_macros(); // The 8 macro knobs (modulation sources, MIDI-learnable).
    std::string source_label(ModSource source) const; // Macros by their project names.
    void draw_palettes();
    void draw_group_caps();
    void draw_output(const ShowFrame &frame);
    void draw_rig(const ShowFrame &frame);
    void draw_preview(const ShowFrame *frame, const char *message);

    Session &session_;
    ProjectState &project_;
    std::uint64_t project_version_ = 0; // The project version config_ was built from.
    ShowConfig config_;
    bool dirty_ = true;
    std::uint64_t version_ = 0;
    int edit_scene_ = 0;        // Whose layers and palette the controls show.
    bool scene_edited_ = false; // A scene changed this frame: the project is told once.
    Ladder ladder_ = Ladder::preview;
    bool preview_open_ = true;
    bool stage_ = false;    // The preview is drawn in the Stage window (draw).
    int playing_scene_ = 0; // The automation's current scene, from the latest frame.
    std::shared_ptr<Recorder> recorder_;
    std::string record_status_;
    int live_source_ = 0; // 0: loopback; 1 and on: input_devices_[i - 1].
    std::vector<std::string> input_devices_;
    std::string live_error_;
    std::vector<double> tap_errors_s_; // Tap test: taps minus the nearest predicted beat.
    MidiInput midi_in_;
    MidiMap midi_map_;
    bool midi_started_ = false; // The project's bindings and port were applied.
    std::vector<std::string> midi_ports_;
    std::string midi_error_;
    std::vector<MidiMessage> midi_messages_; // Reused each frame.
    std::string address_text_ = "127.0.0.1";
    bool address_active_ = false;
    // Measured frame rate, from the frame index over about a second.
    std::chrono::steady_clock::time_point rate_since_ = std::chrono::steady_clock::now();
    std::int64_t rate_index_ = -1;
    double measured_fps_ = 0.0;
    ShowThread show_; // Last, so it is joined first on destruction.
};

} // namespace psyight
