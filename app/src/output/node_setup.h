// Setting the F767 pixel node (firmware/node-f767, SPEC 12) up for the current rig. The node keeps
// its own settings (the universes it receives, its lanes), set over its text protocol on UDP port
// 6455; this works them out from the project and speaks the protocol:
// - the plan: the universes the patch uses, and the lanes: strips chained in patch order, each lane
//   one data line (PE0 to PE7) of at most kNodeLanePixels pixels, at most kNodeLanes of them;
// - the commands for it ("set ...", the palette for the node's fallback show, then "apply");
// - the node's "get" reply, read back and compared with the plan.
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
inline constexpr int kNodeProtocol = 2; // The firmware that applies live and takes a palette.

struct NodeLane {
    int first_pixel = 0; // In patch order, from the first universe's first pixel.
    int pixels = 0;
    int first_strip = 0; // The strips chained on this data line, in patch order.
    int strips = 0;
};

struct NodePlan {
    int first_universe = 1;
    int universes = 0;
    int pixels_per_universe = 170;
    int pixels = 0;
    std::vector<NodeLane> lanes;
    std::vector<std::string> problems; // Empty: the node can take this rig.
    int active_mask() const;
};

NodePlan plan_node(const Project &project);

// The commands that set the node up for the plan: every lane (unused ones cleared), the outputs,
// the universes, then the palette's first colours (when given) and "apply".
std::vector<std::string> node_commands(const NodePlan &plan, const Palette *palette);

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
