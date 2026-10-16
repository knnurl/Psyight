#include "output/discovery.h"

#include <algorithm>
#include <chrono>

namespace psyight {

Result<std::vector<ArtNode>> discover_art_nodes(UdpSocket &socket, const Ipv4Endpoint &to,
                                                int timeout_ms) {
    std::vector<std::uint8_t> poll;
    build_art_poll(poll);
    if (Result<void> sent = socket.send_to(poll, to); !sent) {
        return sent.error();
    }
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    std::vector<ArtNode> nodes;
    std::vector<std::uint8_t> buffer(1500);
    while (true) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) {
            break;
        }
        const Result<size_t> got = socket.receive(buffer, static_cast<int>(left.count()));
        if (!got) {
            return got.error();
        }
        if (got.value() == 0) {
            break; // Timed out.
        }
        const auto reply = parse_art_poll_reply(std::span(buffer.data(), got.value()));
        if (!reply) {
            continue; // Our own poll echoed back, or other Art-Net traffic.
        }
        const bool known = std::any_of(nodes.begin(), nodes.end(), [&](const ArtNode &n) {
            return n.ip == reply->ip && n.bind_index == reply->bind_index;
        });
        if (!known) {
            nodes.push_back(*reply);
        }
    }
    return nodes;
}

} // namespace psyight
