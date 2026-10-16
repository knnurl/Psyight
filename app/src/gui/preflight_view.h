// Preflight panel (SPEC section 11): the checks of output/preflight.h, run every frame on the
// current project, track, audio device and network, coloured by level, with Art-Net node
// discovery on request (it broadcasts an ArtPoll on every interface, so it is never automatic)
// and a button that copies the report as text.
#pragma once

#include "gui/preview_view.h"
#include "gui/project_state.h"
#include "gui/session.h"
#include "output/art_poll.h"
#include "output/udp_socket.h"

#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

class PreflightView {
  public:
    PreflightView(Session &session, ProjectState &project, const PreviewView &preview)
        : session_(session), project_(project), preview_(preview) {}

    void draw();

  private:
    Session &session_;
    ProjectState &project_;
    const PreviewView &preview_;
    std::vector<NetworkInterface> interfaces_;
    std::chrono::steady_clock::time_point interfaces_at_{};
    std::optional<std::vector<ArtNode>> nodes_;
    std::future<Result<std::vector<ArtNode>>> discovery_;
    std::string discovery_error_;
};

} // namespace psyight
