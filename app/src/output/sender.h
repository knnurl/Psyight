// Frame sender (SPEC section 8): every frame goes out in full, one packet per universe, then one
// sync packet so nodes latch together. Sequence numbers advance by one per frame for every
// universe (sACN wraps 0 to 255; Art-Net 1 to 255, since 0 means "unsequenced").
// Destinations: sACN multicast 239.255.hi.lo or a unicast address, port 5568; Art-Net to a unicast
// or directed-broadcast address, port 6454. ArtSync goes to the same address as the data
// [Verify: Art-Net 4 says ArtSync should be broadcast; nodes we target accept unicast].
#pragma once

#include "output/packets.h"
#include "output/patch.h"
#include "output/udp_socket.h"

#include <cstdint>
#include <vector>

namespace psyight {

enum class OutputProtocol { sacn, art_net };

struct OutputSettings {
    OutputProtocol protocol = OutputProtocol::sacn;
    bool multicast = true; // sACN only; otherwise unicast to `address`.
    std::array<std::uint8_t, 4> address = {127, 0, 0, 1};
    bool sync = true;
    std::uint16_t sync_universe = 1000; // sACN; must not carry data.
    std::uint8_t priority = 100;        // sACN, 0 to 200.
    Cid cid{};                          // sACN source id; fixed per installation.
};

// Where packets go: the network, the virtual node, or both.
class PacketSink {
  public:
    virtual ~PacketSink() = default;
    virtual void send(std::span<const std::uint8_t> packet, const Ipv4Endpoint &to) = 0;
};

class OutputSender {
  public:
    // Sends one frame: a packet per universe, then sync. Returns the packets sent.
    int send_frame(const OutputSettings &settings, const std::vector<UniverseData> &universes,
                   PacketSink &sink);

  private:
    std::uint32_t frame_ = 0;
    std::vector<std::uint8_t> buffer_;
};

// Fixed per-installation sACN CID derived from a name (a name-based id, not an RFC 4122 UUID
// generator; good enough to tell sources apart).
Cid cid_from_name(const std::string &name);

} // namespace psyight
