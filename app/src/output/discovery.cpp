#include "output/discovery.h"

#include <algorithm>
#include <chrono>

namespace psyight {

Result<std::vector<ArtNode>> discover_art_nodes(UdpSocket &socket, const Ipv4Endpoint &to,
                                                int timeout_ms) {
    std::vector<std::uint8_t> poll;
    build_art_poll(poll);
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    // Polled every kRepollMs within the window: a single poll was sometimes lost on the way to a
    // node cabled straight to the computer.
    constexpr int kRepollMs = 250;
    auto next_poll = Clock::now();
    std::vector<ArtNode> nodes;
    std::vector<std::uint8_t> buffer(1500);
    while (true) {
        if (Clock::now() >= next_poll) {
            if (Result<void> sent = socket.send_to(poll, to); !sent) {
                return sent.error();
            }
            next_poll = Clock::now() + std::chrono::milliseconds(kRepollMs);
        }
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) {
            break;
        }
        const auto until_poll =
            std::chrono::duration_cast<std::chrono::milliseconds>(next_poll - Clock::now());
        const int wait_ms =
            static_cast<int>(std::max<long long>(1, std::min(left.count(), until_poll.count())));
        const Result<size_t> got = socket.receive(buffer, wait_ms);
        if (!got) {
            return got.error();
        }
        if (got.value() == 0) {
            continue; // Time to poll again, or the window is over.
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
