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
#include "io/presets.h"
#include "io/user_settings.h"
#include "output/show.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

class PreviewView {
  public:
    PreviewView(Session &session, ProjectState &project);

    // The Show window, and the Preview window unless folded. stage (show mode with the editing
    // panels hidden): the rig drawn large in the "Stage" window, where the bass roll was, instead.
    void draw(bool stage = false);
    // The Perform view (View > Perform view, F11): one full-screen live page over the workspace:
    // the rig large with the beat and section, big scene buttons, the macros, Blackout and Hold.
    // Esc (or its button) clears *open.
    void draw_perform(bool *open);
    void draw_output(const ShowFrame &frame); // Also in the setup assistant.
    // The preset browser (View > Browser): search the scene presets, palettes and show packs,
    // built-in and the user's; hovering or selecting one auditions it on the rig preview only
    // (rendered here at the show's render time; the lights keep the show); the selected one can
    // be loaded. Clears the audition when closed.
    void draw_browser(bool *open);
    // Inside the main menu bar.
    void draw_top_bar();
    // B, H, 0 and 1 to 9 (blackout, hold, scenes); off while another mode owns the keys.
    void set_keys_enabled(bool on) { keys_enabled_ = on; }
    bool sending() const { return config_.send_to_network; }
    double measured_fps() const { return measured_fps_; }
    bool recording() const { return recorder_ != nullptr; }
    // React or Show on the top bar: the app may switch to its show look (main.cpp).
    bool performing() const { return ladder_ == Ladder::react || ladder_ == Ladder::show; }
    bool &preview_open() { return preview_open_; } // View > Rig preview.
    // Test patterns (Preflight tab): only in Preflight and Preview; stepping up to Ambient, React
    // or Show stops one.
    const TestPattern &test_pattern() const { return config_.test; }
    void set_test_pattern(const TestPattern &pattern);
    bool test_allowed() const { return ladder_ <= Ladder::preview; }
    std::shared_ptr<const ShowFrame> latest_frame() const { return show_.latest(); }
    // The strip under the mouse in the preview (-1: none), and the one to outline from elsewhere
    // (the Rig tab's hovered row).
    int hovered_strip() const { return hovered_strip_; }
    void set_highlighted_strip(int strip) { highlighted_strip_ = strip; }
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
    // A scene picked by hand (cards, keys 1 to 9): held, launched as the project's launch setting
    // says; React while held. -1 (key 0, Auto): back to the section rules (Show).
    void pick_scene(int scene);
    void draw_scene_editor(std::vector<Scene> &scenes, const ShowFrame &frame);
    // The scene's layers as a device chain, bottom layer first (left), as a rack's signal flows.
    void draw_rack(Scene &scene, const ShowFrame &frame);
    // One layer's device; route_values: the scene's live route values (it is playing) or null.
    void draw_layer_device(Scene &scene, size_t index, const std::vector<double> *route_values,
                           float height, int &remove, int &move_from, int &move_to);
    void draw_routes(Scene &scene, const ShowFrame &frame); // Modulation routes as cards.
    // Knob menus (right-click): modulate a layer's opacity or setting (a source, a macro mapped
    // from the knob's value to its top, or removing a route), and MIDI learn for a target. Route
    // changes are queued and applied after the device chain.
    void modulation_menu(const Scene &scene, int layer, ModTarget target, const char *param,
                         double value, double min, double max);
    void midi_menu(const std::string &target);
    void draw_macros(); // The 8 macro knobs (modulation sources, MIDI-learnable).
    void draw_macro_knobs(int columns);
    // Presets (engine/presets.h, io/presets.h). The user's are rescanned when a menu opens. A
    // scene preset is applied after the scene editor (which holds references into the scenes).
    void draw_library();         // Show packs: load (confirmed) and save.
    void draw_scene_presets();   // In the scene editor: replace this scene or add one; save.
    void draw_palette_library(); // In Palettes: add from the library; save one.
    void apply_pending_scene_preset();
    std::string source_label(ModSource source) const; // Macros by their project names.
    void draw_palettes();
    void draw_group_caps();
    void draw_rig(const ShowFrame &frame);
    void draw_preview(const ShowFrame *frame, const char *message);
    // The audition's frame as node bytes (rig order), or empty when nothing is auditioned.
    std::vector<std::uint8_t> audition_bytes(const ShowFrame &frame) const;

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
    int hovered_strip_ = -1;
    int highlighted_strip_ = -1;
    std::filesystem::path presets_dir_ = "presets"; // Beside the project (the working directory).
    UserPresets user_presets_;
    std::optional<ScenePreset> pending_scene_;
    std::vector<ModRoute> routes_to_add_; // From knob menus, applied after the device chain.
    // The preset browser.
    enum class BrowserTab { scenes, palettes, shows };
    struct Audition {
        BrowserTab kind = BrowserTab::scenes;
        ScenePreset scene;
        Palette palette;
        ShowPack show;
        std::string name;
    };
    std::optional<Audition> audition_;
    BrowserTab browser_tab_ = BrowserTab::scenes;
    std::string browser_search_;
    std::string browser_selected_; // "<tab>:<name>", or empty.
    bool browser_scanned_ = false;
    std::vector<int> routes_to_remove_;
    bool pending_scene_replace_ = true;
    std::optional<ShowPack> pending_pack_;
    std::string preset_name_;
    std::string preset_description_;
    std::string preset_status_;
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
    bool keys_enabled_ = true;
    ShowThread show_; // Last, so it is joined first on destruction.
};

} // namespace psyight
