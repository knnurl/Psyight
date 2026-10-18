// File > Settings: app-wide preferences in one window. Appearance (Desk or Show theme), interface
// depth (Operate, Tune, Engineer, as the top bar's selector), the project file (where it is, save
// now, reload from disk) and the window layout (reset to the default). Show settings that belong
// to a rig or a scene stay in the Preview, Rig and Preflight panels.
#pragma once

#include "gui/project_state.h"

namespace psyight {

class SettingsView {
  public:
    explicit SettingsView(ProjectState &project) : project_(project) {}

    // reset_layout: set when the user asks for the default layout.
    void draw(bool *open, bool &reset_layout);

  private:
    ProjectState &project_;
};

} // namespace psyight
