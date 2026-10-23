// Setting the F767 pixel node (firmware/node-f767, SPEC 12) up for the current rig. The node keeps
// its own settings (the universes it receives, its lanes), set over its text protocol on UDP port
// 6455; this works them out from the project and speaks the protocol:
// - the plan: the universes the patch uses, and the lanes: strips chained in patch order, each lane
//   one data line (PD0 to PD7) of at most kNodeLanePixels pixels, at most kNodeLanes of them;
// - the commands for it ("set ...": lanes, outputs, universes, each lane's colour order, the
//   signal-loss policy and the fallback show's settings and palette, then "apply");
// - the node's "get" reply, read back and compared with the plan and the project's settings.
// Firmware speaking an older protocol skips what it does not know (commands_for_protocol).
// The limits mirror the firmware's (node_core.h); a test checks they agree.
#pragma once

#include "core/result.h"
#include "engine/colour.h"
#include "io/project.h"
#include "output/udp_socket.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

inline constexpr int kNodeLanes = 8;          // NODE_LANES
inline constexpr int kNodeLanePixels = 680;   // NODE_MAX_PIXELS_PER_LANE
inline constexpr int kNodeMaxUniverses = 24;  // NODE_MAX_UNIVERSES
inline constexpr int kNodePaletteColours = 4; // node_settings::palette
inline constexpr std::uint16_t kNodeConfigPort = 6455;
inline constexpr int kNodeProtocol = 3; // Applies live, takes a palette and lane colour orders.
// Lanes longer than this cannot refresh at the show's 60 fps (SPEC 12.3: 30 us a pixel plus a
// 300 us reset).
inline constexpr int kNodeLanePixelsAt60Fps = 545;

struct NodeLane {
    int first_pixel = 0; // In patch order, from the first universe's first pixel.
    int pixels = 0;
    int first_strip = 0; // The strips chained on this data line, in patch order.
    int strips = 0;
    ColourOrder order = ColourOrder::grb; // Its first strip's: the node's fallback show uses it.
};

struct NodePlan {
    int first_universe = 1;
    int universes = 0;
    int pixels_per_universe = 170;
    int pixels = 0;
    std::vector<NodeLane> lanes;
    std::vector<std::string> problems; // Empty: the node can take this rig.
    std::vector<std::string> notes;    // Works, but worth knowing (frame rate, mixed orders).
    NodeFallback fallback;             // The project's signal-loss settings.
    int active_mask() const;
};

// A lane's highest frame rate on the node (SPEC 12.3).
double node_lane_max_fps(int pixels);

NodePlan plan_node(const Project &project);

// The commands that set the node up for the plan: every lane (unused ones cleared) and its colour
// order, the outputs, the universes, the signal-loss settings, then the palette's first colours
// (when given) and "apply".
std::vector<std::string> node_commands(const NodePlan &plan, const Palette *palette);

// The commands a node speaking `protocol` understands: before 3, without the lane orders.
std::vector<std::string> commands_for_protocol(std::vector<std::string> commands, int protocol);

// A "get" reply as key and value; "protocol" is absent on firmware before protocol 2.
std::map<std::string, std::string> parse_node_settings(std::string_view reply);
int node_protocol(const std::map<std::string, std::string> &settings); // 1 when absent.
// Where the node's settings differ from the plan, one line each; empty when they match.
std::vector<std::string> node_differences(const NodePlan &plan,
                                          const std::map<std::string, std::string> &settings);

// Sends one command and waits for the reply (blocking, at most timeout_ms: off the GUI thread).
Result<std::string> node_request(UdpSocket &socket, const std::array<std::uint8_t, 4> &node,
                                 const std::string &command, int timeout_ms);

} // namespace psyight
