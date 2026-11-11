#include "link/prodjlink.h"

#include <algorithm>
#include <cmath>

namespace psyight::prodjlink {

namespace {

// Message types (at 0x0a).
constexpr std::uint8_t kTypeKeepAlive = 0x06;
constexpr std::uint8_t kTypeBeat = 0x28;
constexpr std::uint8_t kTypeOnAir = 0x03;
constexpr std::uint8_t kTypePosition = 0x0b;

// Offsets (see the header).
constexpr size_t kType = 0x0a;
constexpr size_t kAnnounceName = 0x0c;
constexpr size_t kAnnounceDevice = 0x24;
constexpr size_t kAnnounceAddress = 0x2c;
constexpr size_t kName = 0x0b;
constexpr size_t kNameBytes = 20;
constexpr size_t kDevice = 0x21;
constexpr size_t kLength = 0x22; // The bytes after it (big-endian, 2 bytes).
constexpr size_t kBeatNextBeat = 0x24;
constexpr size_t kBeatNextBar = 0x2c;
constexpr size_t kBeatPitch = 0x54;
constexpr size_t kBeatBpm = 0x5a;
constexpr size_t kBeatInBar = 0x5c;
constexpr size_t kBeatSize = 0x60;
constexpr size_t kOnAirFlags = 0x24;
constexpr size_t kOnAirFlags56 = 0x2e;
constexpr size_t kOnAirSize = 0x2d;
constexpr size_t kOnAirSize6 = 0x35;
constexpr size_t kPositionLength = 0x24;
constexpr size_t kPositionPlayhead = 0x28;
constexpr size_t kPositionPitch = 0x2c;
constexpr size_t kPositionBpm = 0x38;
constexpr size_t kPositionSize = 0x3c;
constexpr size_t kKeepAliveSize = 0x36;
constexpr std::uint32_t kPitchUnity = 0x100000;

std::uint32_t be32(std::span<const std::uint8_t> b, size_t at) {
    return (std::uint32_t{b[at]} << 24) | (std::uint32_t{b[at + 1]} << 16) |
           (std::uint32_t{b[at + 2]} << 8) | std::uint32_t{b[at + 3]};
}

std::uint16_t be16(std::span<const std::uint8_t> b, size_t at) {
    return static_cast<std::uint16_t>((b[at] << 8) | b[at + 1]);
}

void put32(std::vector<std::uint8_t> &b, size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

void put16(std::vector<std::uint8_t> &b, size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}

std::string text_at(std::span<const std::uint8_t> b, size_t at) {
    std::string s;
    for (size_t i = at; i < at + kNameBytes && i < b.size() && b[i] != 0; ++i) {
        s.push_back(static_cast<char>(b[i]));
    }
    return s;
}

void put_text(std::vector<std::uint8_t> &b, size_t at, const std::string &s) {
    for (size_t i = 0; i < kNameBytes && i < s.size(); ++i) {
        b[at + i] = static_cast<std::uint8_t>(s[i]);
    }
}

// A packet of `size` bytes with the magic, type, name and device (port 50001 layout), and its
// length field.
std::vector<std::uint8_t> packet_50001(std::uint8_t type, size_t size, const std::string &name,
                                       int device) {
    std::vector<std::uint8_t> b(size, 0);
    std::copy(kMagic.begin(), kMagic.end(), b.begin());
    b[kType] = type;
    put_text(b, kName, name);
    b[0x1f] = 0x01;
    b[0x20] = 0x00;
    b[kDevice] = static_cast<std::uint8_t>(device);
    put16(b, kLength, static_cast<std::uint16_t>(size - kLength - 2));
    return b;
}

double pitch_from_unity(std::uint32_t raw) {
    return (static_cast<double>(raw) - kPitchUnity) / kPitchUnity;
}

} // namespace

std::optional<Message> decode(int port, std::span<const std::uint8_t> b) {
    if (b.size() <= kType || !std::equal(kMagic.begin(), kMagic.end(), b.begin())) {
        return std::nullopt;
    }
    const std::uint8_t type = b[kType];
    if (port == kAnnouncePort) {
        if (type != kTypeKeepAlive || b.size() < kAnnounceAddress + 4) {
            return std::nullopt;
        }
        KeepAlive m;
        m.name = text_at(b, kAnnounceName);
        m.device = b[kAnnounceDevice];
        std::copy_n(b.begin() + kAnnounceAddress, 4, m.address.begin());
        return m;
    }
    if (port != kBeatPort || b.size() <= kDevice) {
        return std::nullopt;
    }
    const std::string name = text_at(b, kName);
    const int device = b[kDevice];
    if (type == kTypeBeat && b.size() >= kBeatSize) {
        Beat m;
        m.name = name;
        m.device = device;
        m.next_beat_ms = be32(b, kBeatNextBeat);
        m.next_bar_ms = be32(b, kBeatNextBar);
        m.pitch = pitch_from_unity(be32(b, kBeatPitch));
        m.bpm = be16(b, kBeatBpm) / 100.0;
        m.beat_in_bar = b[kBeatInBar];
        return m;
    }
    if (type == kTypeOnAir && b.size() >= kOnAirSize) {
        OnAir m;
        m.name = name;
        m.device = device;
        for (size_t c = 0; c < 4; ++c) {
            m.on_air[c] = b[kOnAirFlags + c] != 0;
        }
        if (b.size() >= kOnAirSize6) {
            m.channels = 6;
            m.on_air[4] = b[kOnAirFlags56] != 0;
            m.on_air[5] = b[kOnAirFlags56 + 1] != 0;
        }
        return m;
    }
    if (type == kTypePosition && b.size() >= kPositionSize) {
        Position m;
        m.name = name;
        m.device = device;
        m.track_length_s = be32(b, kPositionLength);
        m.playhead_ms = be32(b, kPositionPlayhead);
        m.pitch = static_cast<std::int32_t>(be32(b, kPositionPitch)) / 10000.0;
        const std::uint32_t bpm = be32(b, kPositionBpm);
        m.bpm = bpm == 0xffffffffu ? 0.0 : bpm / 10.0;
        return m;
    }
    return std::nullopt;
}

std::vector<std::uint8_t> encode(const KeepAlive &m) {
    std::vector<std::uint8_t> b(kKeepAliveSize, 0);
    std::copy(kMagic.begin(), kMagic.end(), b.begin());
    b[kType] = kTypeKeepAlive;
    put_text(b, kAnnounceName, m.name);
    put16(b, 0x22, static_cast<std::uint16_t>(kKeepAliveSize));
    b[kAnnounceDevice] = static_cast<std::uint8_t>(m.device);
    std::copy(m.address.begin(), m.address.end(), b.begin() + kAnnounceAddress);
    return b;
}

std::vector<std::uint8_t> encode(const Beat &m) {
    std::vector<std::uint8_t> b = packet_50001(kTypeBeat, kBeatSize, m.name, m.device);
    put32(b, kBeatNextBeat, m.next_beat_ms);
    put32(b, kBeatNextBar, m.next_bar_ms);
    put32(b, kBeatPitch, static_cast<std::uint32_t>(std::lround((1.0 + m.pitch) * kPitchUnity)));
    put16(b, kBeatBpm, static_cast<std::uint16_t>(std::lround(m.bpm * 100.0)));
    b[kBeatInBar] = static_cast<std::uint8_t>(m.beat_in_bar);
    return b;
}

std::vector<std::uint8_t> encode(const OnAir &m) {
    std::vector<std::uint8_t> b =
        packet_50001(kTypeOnAir, m.channels > 4 ? kOnAirSize6 : kOnAirSize, m.name, m.device);
    for (size_t c = 0; c < 4; ++c) {
        b[kOnAirFlags + c] = m.on_air[c] ? 1 : 0;
    }
    if (m.channels > 4) {
        b[kOnAirFlags56] = m.on_air[4] ? 1 : 0;
        b[kOnAirFlags56 + 1] = m.on_air[5] ? 1 : 0;
    }
    return b;
}

std::vector<std::uint8_t> encode(const Position &m) {
    std::vector<std::uint8_t> b = packet_50001(kTypePosition, kPositionSize, m.name, m.device);
    put32(b, kPositionLength, static_cast<std::uint32_t>(std::lround(m.track_length_s)));
    put32(b, kPositionPlayhead, static_cast<std::uint32_t>(std::lround(m.playhead_ms)));
    put32(b, kPositionPitch,
          static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(m.pitch * 10000.0))));
    put32(b, kPositionBpm,
          m.bpm > 0.0 ? static_cast<std::uint32_t>(std::lround(m.bpm * 10.0)) : 0xffffffffu);
    return b;
}

} // namespace psyight::prodjlink
