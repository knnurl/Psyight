// File > Settings: app-wide preferences in one window. Appearance (Desk or Show theme), interface
// depth (Operate, Tune, Engineer, as the top bar's selector), show mode (what React and Show on
// the top bar change in the app's look), the project file (where it is, save now, reload from
// disk) and the window layout (reset to the default). Show settings that belong to a rig or a
// scene stay in the Show, Rig and Preflight panels.
#pragma once

#include "gui/project_state.h"
#include "io/user_settings.h"

namespace psyight {

class SettingsView {
  public:
    explicit SettingsView(ProjectState &project) : project_(project) {}

    // reset_layout: set when the user asks for the default layout. prefs: the show mode options.
    void draw(bool *open, bool &reset_layout, UserSettings &prefs);

  private:
    ProjectState &project_;
};

} // namespace psyight
