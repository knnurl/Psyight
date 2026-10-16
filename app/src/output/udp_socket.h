// UDP socket behind an interface (SPEC section 8; CLAUDE.md: platform code behind interfaces).
// The Windows implementation is udp_socket_win32.cpp (Winsock). Sending never blocks for long: UDP
// sends return once the datagram is queued. Receiving waits at most timeout_ms.
#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace psyight {

struct Ipv4Endpoint {
    std::array<std::uint8_t, 4> address{};
    std::uint16_t port = 0;
};

// "a.b.c.d"; nullopt when malformed.
std::optional<std::array<std::uint8_t, 4>> parse_ipv4(const std::string &text);
std::string ipv4_text(const std::array<std::uint8_t, 4> &address);

struct NetworkInterface {
    std::string name;
    std::array<std::uint8_t, 4> address{};
    std::array<std::uint8_t, 4> mask{};
    // The directed broadcast address of its subnet.
    std::array<std::uint8_t, 4> broadcast() const;
    bool contains(const std::array<std::uint8_t, 4> &other) const; // Same subnet.
};

// IPv4 interfaces that are up, without loopback.
std::vector<NetworkInterface> list_ipv4_interfaces();

class UdpSocket {
  public:
    virtual ~UdpSocket() = default;
    virtual Result<void> send_to(std::span<const std::uint8_t> packet, const Ipv4Endpoint &to) = 0;
    // The datagram's size, 0 when none arrived in time, or an error. `from`, when given, gets
    // the sender.
    virtual Result<size_t> receive(std::span<std::uint8_t> buffer, int timeout_ms,
                                   Ipv4Endpoint *from = nullptr) = 0;
    // The port bound locally (useful after binding port 0).
    virtual std::uint16_t local_port() const = 0;
};

// Binds bind_address:bind_port (0.0.0.0 and 0 for any). Broadcast sends are allowed (Art-Net
// often uses directed broadcast). Multicast sends use a TTL of 1 and leave on the interface the
// routing table picks.
Result<std::unique_ptr<UdpSocket>> open_udp_socket(std::array<std::uint8_t, 4> bind_address = {},
                                                   std::uint16_t bind_port = 0);

} // namespace psyight
