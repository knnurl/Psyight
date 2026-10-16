// Art-Net node discovery (SPEC sections 8 and 11): one ArtPoll, then the replies that arrive
// within the timeout. Nodes answer to port 6454 of the poller, so in use the socket is bound to
// 0.0.0.0:6454. Blocking: run it off the GUI thread.
#pragma once

#include "core/result.h"
#include "output/art_poll.h"
#include "output/udp_socket.h"

#include <vector>

namespace psyight {

// Replies from the same node and bind index are kept once.
Result<std::vector<ArtNode>> discover_art_nodes(UdpSocket &socket, const Ipv4Endpoint &to,
                                                int timeout_ms);

} // namespace psyight
