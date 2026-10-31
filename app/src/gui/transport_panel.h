// Source and transport panel (SPEC section 9.4, M1): waveform overview, play and pause (Space),
// seek by clicking the overview, loop region with draggable bounds, volume.
#pragma once

#include "gui/player.h"
#include "gui/session.h"
#include "io/user_settings.h"

namespace psyight {

class TransportPanel {
  public:
    explicit TransportPanel(Session &session) : session_(session) {}

    void draw();
    void apply_settings(const UserSettings &settings); // The volume.
    void store_settings(UserSettings &settings) const { settings.volume_db = volume_db_; }
    void set_keys_enabled(bool on) { keys_enabled_ = on; } // Space plays and pauses.
    void set_player(Player *player) { player_ = player; }  // Previous and next.

  private:
    void draw_overview(const LoadedTrack &track);

    Session &session_;
    const LoadedTrack *shown_track_ = nullptr; // Detects a newly loaded track.
    float volume_db_ = 0.0f;
    bool scrubbing_ = false; // The mouse button went down on the overview and is still down.
    float scrub_x_ = -1.0f;  // Where the last scrub seek was (screen x).
    bool keys_enabled_ = true;
    Player *player_ = nullptr;
};

} // namespace psyight
