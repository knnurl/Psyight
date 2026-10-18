// The setup assistant (File > Setup assistant; opened by itself when no project file exists yet):
// one guided pass through what a first setup needs, reusing the panels' own controls:
// 1 Rig (a template, or keep the strips), 2 Output (protocol, address, universes, node
// discovery), 3 Test patterns (direction, colour order), 4 Latency (speaker distance, or the
// tap test in live mode), 5 Check (what preflight still flags). Steps can be visited in any order.
#pragma once

#include "gui/preflight_view.h"
#include "gui/preview_view.h"
#include "gui/project_state.h"
#include "gui/rig_view.h"

namespace psyight {

class SetupAssistant {
  public:
    SetupAssistant(ProjectState &project, RigView &rig, PreviewView &preview,
                   PreflightView &preflight)
        : project_(project), rig_(rig), preview_(preview), preflight_(preflight) {}

    void open() {
        open_ = true;
        step_ = 0;
    }
    void draw();

  private:
    void draw_latency();
    void draw_check();

    ProjectState &project_;
    RigView &rig_;
    PreviewView &preview_;
    PreflightView &preflight_;
    bool open_ = false;
    int step_ = 0;
    double distance_m_ = 10.0; // Speakers to the audience.
};

} // namespace psyight
