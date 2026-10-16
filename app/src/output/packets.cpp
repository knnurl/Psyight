#include "output/packets.h"

#include <algorithm>
#include <cstring>

namespace psyight {

namespace {

constexpr std::array<std::uint8_t, 12> kAcnIdentifier = {'A', 'S', 'C', '-', 'E', '1',
                                                         '.', '1', '7', 0,   0,   0};
constexpr std::array<std::uint8_t, 8> kArtNetId = {'A', 'r', 't', '-', 'N', 'e', 't', 0};
constexpr std::uint32_t kVectorRootData = 0x00000004;
constexpr std::uint32_t kVectorRootExtended = 0x00000008;
constexpr std::uint32_t kVectorFramingData = 0x00000002;
constexpr std::uint32_t kVectorFramingSync = 0x00000001;
constexpr std::uint8_t kVectorDmpSetProperty = 0x02;
constexpr std::uint8_t kDmpAddressDataType = 0xa1;
constexpr std::uint16_t kOpDmx = 0x5000;
constexpr std::uint16_t kOpSync = 0x5200;
constexpr std::uint8_t kArtNetProtocolVersion = 14;
constexpr size_t kSourceNameBytes = 64;
constexpr std::uint16_t kMaxSacnUniverse = 63999;
constexpr std::uint16_t kMaxPortAddress = 0x7fff;

void put16(std::vector<std::uint8_t> &out, size_t at, std::uint16_t v) { // Big-endian.
    out[at] = static_cast<std::uint8_t>(v >> 8);
    out[at + 1] = static_cast<std::uint8_t>(v);
}

void put32(std::vector<std::uint8_t> &out, size_t at, std::uint32_t v) {
    put16(out, at, static_cast<std::uint16_t>(v >> 16));
    put16(out, at + 2, static_cast<std::uint16_t>(v));
}

std::uint16_t get16(std::span<const std::uint8_t> in, size_t at) {
    return static_cast<std::uint16_t>((in[at] << 8) | in[at + 1]);
}

std::uint32_t get32(std::span<const std::uint8_t> in, size_t at) {
    return (static_cast<std::uint32_t>(get16(in, at)) << 16) | get16(in, at + 2);
}

size_t pdu_length(std::span<const std::uint8_t> in, size_t at) {
    return static_cast<size_t>(get16(in, at) & 0x0fff);
}

std::uint16_t flags_length(size_t length) {
    return static_cast<std::uint16_t>(0x7000 | (length & 0x0fff));
}

// Root layer up to and including the CID: bytes 0 to 37 of both sACN packet kinds.
void put_root(std::vector<std::uint8_t> &out, std::uint32_t vector, const Cid &cid) {
    put16(out, 0, 0x0010); // Preamble size.
    put16(out, 2, 0x0000); // Postamble size.
    std::copy(kAcnIdentifier.begin(), kAcnIdentifier.end(), out.begin() + 4);
    put16(out, 16, flags_length(out.size() - 16));
    put32(out, 18, vector);
    std::copy(cid.begin(), cid.end(), out.begin() + 22);
}

bool has_root(std::span<const std::uint8_t> in, std::uint32_t vector) {
    return get16(in, 0) == 0x0010 && get16(in, 2) == 0 &&
           std::equal(kAcnIdentifier.begin(), kAcnIdentifier.end(), in.begin() + 4) &&
           pdu_length(in, 16) == in.size() - 16 && get32(in, 18) == vector;
}

void put_art_header(std::vector<std::uint8_t> &out, std::uint16_t opcode) {
    std::copy(kArtNetId.begin(), kArtNetId.end(), out.begin());
    out[8] = static_cast<std::uint8_t>(opcode); // Opcode is little-endian.
    out[9] = static_cast<std::uint8_t>(opcode >> 8);
    out[10] = 0; // Protocol version, big-endian.
    out[11] = kArtNetProtocolVersion;
}

} // namespace

std::array<std::uint8_t, 4> sacn_multicast_group(std::uint16_t universe) {
    return {239, 255, static_cast<std::uint8_t>(universe >> 8),
            static_cast<std::uint8_t>(universe)};
}

size_t build_sacn_data(const SacnSource &source, std::uint16_t universe, std::uint8_t sequence,
                       std::span<const std::uint8_t> slots, std::vector<std::uint8_t> &out) {
    if (universe < 1 || universe > kMaxSacnUniverse || slots.size() > kMaxSlots ||
        source.priority > 200 || source.sync_universe > kMaxSacnUniverse) {
        return 0;
    }
    const size_t n = slots.size();
    out.assign(kSacnHeaderBytes + n, 0);
    put_root(out, kVectorRootData, source.cid);
    // Framing layer.
    put16(out, 38, flags_length(out.size() - 38));
    put32(out, 40, kVectorFramingData);
    std::memcpy(out.data() + 44, source.name.data(),
                std::min(source.name.size(), kSourceNameBytes - 1)); // Null-terminated.
    out[108] = source.priority;
    put16(out, 109, source.sync_universe);
    out[111] = sequence;
    out[112] = 0; // Options: no preview, no stream termination, no force sync.
    put16(out, 113, universe);
    // DMP layer.
    put16(out, 115, flags_length(out.size() - 115));
    out[117] = kVectorDmpSetProperty;
    out[118] = kDmpAddressDataType;
    put16(out, 119, 0x0000); // First property address.
    put16(out, 121, 0x0001); // Address increment.
    put16(out, 123, static_cast<std::uint16_t>(n + 1)); // Property count, with the start code.
    out[125] = 0;                                       // DMX512 start code.
    std::copy(slots.begin(), slots.end(), out.begin() + kSacnHeaderBytes);
    return out.size();
}

size_t build_sacn_sync(const Cid &cid, std::uint16_t sync_universe, std::uint8_t sequence,
                       std::vector<std::uint8_t> &out) {
    if (sync_universe < 1 || sync_universe > kMaxSacnUniverse) {
        return 0;
    }
    out.assign(kSacnSyncBytes, 0);
    put_root(out, kVectorRootExtended, cid);
    put16(out, 38, flags_length(out.size() - 38));
    put32(out, 40, kVectorFramingSync);
    out[44] = sequence;
    put16(out, 45, sync_universe);
    return out.size(); // Bytes 47 and 48 are reserved, zero.
}

size_t build_art_dmx(std::uint16_t port_address, std::uint8_t sequence,
                     std::span<const std::uint8_t> slots, std::vector<std::uint8_t> &out) {
    if (port_address > kMaxPortAddress || slots.empty() || slots.size() > kMaxSlots) {
        return 0;
    }
    const size_t length = slots.size() + (slots.size() % 2); // Must be even.
    out.assign(kArtDmxHeaderBytes + length, 0);
    put_art_header(out, kOpDmx);
    out[12] = sequence;
    out[13] = 0;                                           // Physical input port.
    out[14] = static_cast<std::uint8_t>(port_address);     // SubUni: sub-net and universe.
    out[15] = static_cast<std::uint8_t>(port_address >> 8); // Net.
    put16(out, 16, static_cast<std::uint16_t>(length));
    std::copy(slots.begin(), slots.end(), out.begin() + kArtDmxHeaderBytes);
    return out.size();
}

size_t build_art_sync(std::vector<std::uint8_t> &out) {
    out.assign(kArtSyncBytes, 0);
    put_art_header(out, kOpSync);
    return out.size(); // Aux1 and Aux2 are zero.
}

std::optional<ParsedPacket> parse_sacn(std::span<const std::uint8_t> in) {
    if (in.size() == kSacnSyncBytes && has_root(in, kVectorRootExtended)) {
        if (pdu_length(in, 38) != in.size() - 38 || get32(in, 40) != kVectorFramingSync) {
            return std::nullopt;
        }
        ParsedPacket p;
        p.kind = ParsedPacket::Kind::sync;
        p.sequence = in[44];
        p.sync_universe = get16(in, 45);
        return p;
    }
    if (in.size() < kSacnHeaderBytes || in.size() > kSacnHeaderBytes + kMaxSlots ||
        !has_root(in, kVectorRootData)) {
        return std::nullopt;
    }
    const size_t n = in.size() - kSacnHeaderBytes;
    if (pdu_length(in, 38) != in.size() - 38 || get32(in, 40) != kVectorFramingData ||
        pdu_length(in, 115) != in.size() - 115 || in[117] != kVectorDmpSetProperty ||
        in[118] != kDmpAddressDataType || get16(in, 123) != static_cast<std::uint16_t>(n + 1) || in[125] != 0) {
        return std::nullopt;
    }
    ParsedPacket p;
    p.priority = in[108];
    p.sync_universe = get16(in, 109);
    p.sequence = in[111];
    p.universe = get16(in, 113);
    p.slots.assign(in.begin() + kSacnHeaderBytes, in.end());
    return p;
}

std::optional<ParsedPacket> parse_art_net(std::span<const std::uint8_t> in) {
    if (in.size() < kArtSyncBytes || !std::equal(kArtNetId.begin(), kArtNetId.end(), in.begin()) ||
        in[11] < kArtNetProtocolVersion) {
        return std::nullopt;
    }
    const auto opcode = static_cast<std::uint16_t>(in[8] | (in[9] << 8));
    if (opcode == kOpSync) {
        ParsedPacket p;
        p.kind = ParsedPacket::Kind::sync;
        return p;
    }
    if (opcode != kOpDmx || in.size() < kArtDmxHeaderBytes + 2) {
        return std::nullopt;
    }
    const size_t length = get16(in, 16);
    if (length < 2 || length > kMaxSlots || in.size() < kArtDmxHeaderBytes + length) {
        return std::nullopt;
    }
    ParsedPacket p;
    p.sequence = in[12];
    p.universe = static_cast<std::uint16_t>(((in[15] & 0x7f) << 8 | in[14]) + 1);
    p.slots.assign(in.begin() + kArtDmxHeaderBytes,
                   in.begin() + static_cast<std::ptrdiff_t>(kArtDmxHeaderBytes + length));
    return p;
}

} // namespace psyight
