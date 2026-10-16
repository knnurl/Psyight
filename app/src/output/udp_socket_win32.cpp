#include "output/udp_socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <charconv>
#include <cstring>
#include <string>

namespace psyight {

namespace {

// Winsock is started once per process and left running until exit.
bool start_winsock() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}

Error socket_error(const char *what) {
    return Error{std::string(what) + " failed (Winsock error " + std::to_string(WSAGetLastError()) +
                 ")"};
}

sockaddr_in to_sockaddr(const std::array<std::uint8_t, 4> &address, std::uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    std::memcpy(&a.sin_addr, address.data(), 4);
    return a;
}

class Win32UdpSocket final : public UdpSocket {
  public:
    explicit Win32UdpSocket(SOCKET s) : socket_(s) {}
    ~Win32UdpSocket() override { closesocket(socket_); }
    Win32UdpSocket(const Win32UdpSocket &) = delete;
    Win32UdpSocket &operator=(const Win32UdpSocket &) = delete;

    Result<void> send_to(std::span<const std::uint8_t> packet, const Ipv4Endpoint &to) override {
        const sockaddr_in a = to_sockaddr(to.address, to.port);
        const int sent = sendto(socket_, reinterpret_cast<const char *>(packet.data()),
                                static_cast<int>(packet.size()), 0,
                                reinterpret_cast<const sockaddr *>(&a), sizeof(a));
        if (sent != static_cast<int>(packet.size())) {
            return socket_error("sendto");
        }
        return {};
    }

    Result<size_t> receive(std::span<std::uint8_t> buffer, int timeout_ms) override {
        WSAPOLLFD poll{};
        poll.fd = socket_;
        poll.events = POLLRDNORM;
        const int ready = WSAPoll(&poll, 1, timeout_ms);
        if (ready < 0) {
            return socket_error("WSAPoll");
        }
        if (ready == 0) {
            return size_t{0};
        }
        const int got = recv(socket_, reinterpret_cast<char *>(buffer.data()),
                             static_cast<int>(buffer.size()), 0);
        if (got < 0) {
            return socket_error("recv");
        }
        return static_cast<size_t>(got);
    }

    std::uint16_t local_port() const override {
        sockaddr_in a{};
        int length = sizeof(a);
        if (getsockname(socket_, reinterpret_cast<sockaddr *>(&a), &length) != 0) {
            return 0;
        }
        return ntohs(a.sin_port);
    }

  private:
    SOCKET socket_;
};

} // namespace

std::optional<std::array<std::uint8_t, 4>> parse_ipv4(const std::string &text) {
    std::array<std::uint8_t, 4> out{};
    const char *p = text.data();
    const char *end = text.data() + text.size();
    for (size_t i = 0; i < 4; ++i) {
        int value = -1;
        const auto [next, ec] = std::from_chars(p, end, value);
        if (ec != std::errc() || next == p || value < 0 || value > 255) {
            return std::nullopt;
        }
        out[i] = static_cast<std::uint8_t>(value);
        p = next;
        if (i < 3) {
            if (p == end || *p != '.') {
                return std::nullopt;
            }
            ++p;
        }
    }
    if (p != end) {
        return std::nullopt;
    }
    return out;
}

Result<std::unique_ptr<UdpSocket>> open_udp_socket(std::array<std::uint8_t, 4> bind_address,
                                                   std::uint16_t bind_port) {
    if (!start_winsock()) {
        return Error{"WSAStartup failed"};
    }
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        return socket_error("socket");
    }
    auto owned = std::make_unique<Win32UdpSocket>(s);
    const DWORD ttl = 1; // Multicast stays on the local network.
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char *>(&ttl), sizeof(ttl));
    const BOOL reuse = TRUE; // Lets the virtual node share a port with other receivers.
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
    const sockaddr_in a = to_sockaddr(bind_address, bind_port);
    if (bind(s, reinterpret_cast<const sockaddr *>(&a), sizeof(a)) != 0) {
        return socket_error("bind");
    }
    return std::unique_ptr<UdpSocket>(std::move(owned));
}

} // namespace psyight
