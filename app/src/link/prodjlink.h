// Pro DJ Link messages a passive listener receives (docs/PLAN_FOLLOW_MODE.md), decoded from the
// layouts in Deep Symmetry's protocol analysis (djl-analysis.deepsymmetry.org; our own code from
// the description, no text or tables copied). Every packet starts with the 10-byte magic
// "Qspt1WmJOL" and its type at 0x0a; numbers are big-endian.
//   UDP 50000, keep-alive (type 0x06): device name at 0x0c (20 bytes), number at 0x24, IP at 0x2c.
//   UDP 50001: device name at 0x0b (20 bytes), number at 0x21, and by type:
//     beat (0x28): ms to the next beat at 0x24 and to the next bar at 0x2c, pitch at 0x54
//       (0x100000 is 0%), BPM x 100 at 0x5a (2 bytes), the beat in the bar (1 to 4) at 0x5c;
//     on-air (0x03, from the mixer): a flag per channel at 0x24 to 0x27, channels 5 and 6 at
//       0x2e and 0x2f on six-channel mixers [Verify];
//     absolute position (0x0b, CDJ-3000, about every 30 ms): track length in s at 0x24, playhead
//       in ms at 0x28, pitch in % x 100 (signed) at 0x2c, BPM x 10 at 0x38 (0xffffffff when the
//       track has no grid) [Verify].
// Offsets confirmed by a capture lose their [Verify] (none yet: the logger's beat fields agree
// with the analysis but have not met a real CDJ). Encoders build the same layouts, for the
// simulator and the tests.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace psyight::prodjlink {

inline constexpr std::array<std::uint8_t, 10> kMagic = {'Q', 's', 'p', 't', '1',
                                                        'W', 'm', 'J', 'O', 'L'};
inline constexpr int kAnnouncePort = 50000;
inline constexpr int kBeatPort = 50001;
inline constexpr int kStatusPort = 50002;
inline constexpr int kMaxChannels = 6;

struct KeepAlive {
    std::string name;
    int device = 0;
    std::array<std::uint8_t, 4> address{};
};

struct Beat {
    std::string name;
    int device = 0;
    double bpm = 0.0;   // The track's BPM at 0% pitch; heard: bpm * (1 + pitch) [Verify].
    double pitch = 0.0; // Fraction: 0.06 is +6%.
    int beat_in_bar = 0;
    std::uint32_t next_beat_ms = 0;
    std::uint32_t next_bar_ms = 0;
};

struct OnAir {
    std::string name;
    int device = 0; // The mixer's number.
    int channels = 4;
    std::array<bool, kMaxChannels> on_air{};
};

struct Position {
    std::string name;
    int device = 0;
    double track_length_s = 0.0;
    double playhead_ms = 0.0;
    double pitch = 0.0; // Fraction.
    double bpm = 0.0;   // 0 when the track has no grid.
};

using Message = std::variant<KeepAlive, Beat, OnAir, Position>;

// A message from a datagram on `port`, or nullopt for anything else (another type, too short,
// not Pro DJ Link).
std::optional<Message> decode(int port, std::span<const std::uint8_t> bytes);

std::vector<std::uint8_t> encode(const KeepAlive &m);
std::vector<std::uint8_t> encode(const Beat &m);
std::vector<std::uint8_t> encode(const OnAir &m);
std::vector<std::uint8_t> encode(const Position &m);

} // namespace psyight::prodjlink
