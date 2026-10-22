// Preflight panel (SPEC section 11): the checks of output/preflight.h, run every frame on the
// current project, track, audio device and network, coloured by level, with Art-Net node
// discovery on request (it broadcasts an ArtPoll on every interface, so it is never automatic),
// a button that copies the report as text, test patterns (output/test_patterns.h: chase,
// colour order, identify universe, all on) for checking the wiring, and node setup
// (output/node_setup.h): the data lines and universes this rig needs from the F767 node, sent to
// it, applied, read back and compared, and saved on it.
#pragma once

#include "gui/preview_view.h"
#include "gui/project_state.h"
#include "gui/session.h"
#include "output/art_poll.h"
#include "output/preflight.h"
#include "output/udp_socket.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace psyight {

// One conversation with the node (off the GUI thread): the commands that went through, the first
// that failed, and the settings read back afterwards.
struct NodeExchange {
    int done = 0;
    std::string failed_command;
    std::string error;
    bool read = false;
    bool restarting = false; // Ended with a reboot: nothing to read yet.
    std::map<std::string, std::string> settings;
};

class PreflightView {
  public:
    PreflightView(Session &session, ProjectState &project, PreviewView &preview)
        : session_(session), project_(project), preview_(preview) {}

    void update(); // Once a frame, before the status bar: the checks (shown or not).
    void draw();
    // Also in the setup assistant.
    void draw_test_patterns();
    void draw_discovery(); // The discovery button and the nodes that answered.
    void draw_node_setup();
    const std::vector<PreflightCheck> &checks() const { return checks_; }
    const std::optional<std::vector<ArtNode>> &nodes() const { return nodes_; }

  private:
    Session &session_;
    ProjectState &project_;
    PreviewView &preview_;
    std::vector<NetworkInterface> interfaces_;
    std::chrono::steady_clock::time_point interfaces_at_{};
    std::optional<std::vector<ArtNode>> nodes_;
    std::future<Result<std::vector<ArtNode>>> discovery_;
    std::string discovery_error_;
    std::vector<PreflightCheck> checks_;
    // Node setup.
    void start_node_job(std::vector<std::string> commands, const char *what);
    std::string node_address_;
    bool node_send_palette_ = true;
    int node_palette_ = 0;
    std::future<NodeExchange> node_job_;
    std::optional<NodeExchange> node_result_;
    std::string node_job_what_; // "read", "set up" or "save".
    std::string node_status_;
};

} // namespace psyight
