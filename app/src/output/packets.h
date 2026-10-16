// Output packets (SPEC section 8): sACN (ANSI E1.31-2018) data and universe sync, and Art-Net 4
// ArtDmx and ArtSync, built into caller-owned buffers. The same file parses them back, as the
// virtual node does, so tests can check a build-then-parse round trip as well as byte layouts.
// Assumptions: one universe per packet, start code 0, up to 512 slots. sACN universes are 1 to
// 63999; the Art-Net port address is the sACN universe minus 1 (Art-Net counts from 0).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace psyight {

constexpr std::uint16_t kSacnPort = 5568;
constexpr std::uint16_t kArtNetPort = 6454;
constexpr size_t kSacnHeaderBytes = 126;
constexpr size_t kSacnSyncBytes = 49;
constexpr size_t kArtDmxHeaderBytes = 18;
constexpr size_t kArtSyncBytes = 14;
constexpr size_t kMaxSlots = 512;

using Cid = std::array<std::uint8_t, 16>;

struct SacnSource {
    Cid cid{};
    std::string name = "Psyight";
    std::uint8_t priority = 100;
    std::uint16_t sync_universe = 0; // 0: no synchronisation.
};

// IPv4 multicast group for an sACN universe: 239.255.hi.lo, as four bytes.
std::array<std::uint8_t, 4> sacn_multicast_group(std::uint16_t universe);

// Return the packet length, or 0 when the arguments are out of range.
size_t build_sacn_data(const SacnSource &source, std::uint16_t universe, std::uint8_t sequence,
                       std::span<const std::uint8_t> slots, std::vector<std::uint8_t> &out);
size_t build_sacn_sync(const Cid &cid, std::uint16_t sync_universe, std::uint8_t sequence,
                       std::vector<std::uint8_t> &out);
size_t build_art_dmx(std::uint16_t port_address, std::uint8_t sequence,
                     std::span<const std::uint8_t> slots, std::vector<std::uint8_t> &out);
size_t build_art_sync(std::vector<std::uint8_t> &out);

// What the virtual node takes out of a packet.
struct ParsedPacket {
    enum class Kind { dmx, sync };
    Kind kind = Kind::dmx;
    std::uint16_t universe = 0; // sACN numbering for both protocols (Art-Net port address + 1).
    std::uint8_t sequence = 0;
    std::uint16_t sync_universe = 0;
    std::uint8_t priority = 0;
    std::vector<std::uint8_t> slots;
};

std::optional<ParsedPacket> parse_sacn(std::span<const std::uint8_t> packet);
std::optional<ParsedPacket> parse_art_net(std::span<const std::uint8_t> packet);

} // namespace psyight
