// TCNet packets a passive listener receives from AlphaTheta's PRO DJ LINK Bridge
// (docs/PLAN_FOLLOW_MODE.md), from the TCNet specification V3.5.1B (Event Imagineering Group,
// "open and free to be used"; our own code). Little-endian.
// Every packet starts with a 24-byte management header: node ID at 0 (2 bytes), protocol version
// at 2 (major) and 3 (minor), "TCN" at 4, message type at 7, node name at 8 (8 bytes), sequence
// at 16, node type at 17, node options at 18 (2 bytes), timestamp in microseconds at 20.
// Time packets (type 254, about every 1 to 40 ms): layers 1 to 8's playhead in ms at 24 (4 bytes
// each), total times at 56, beat markers at 88 (1 byte each), states at 96 [Verify the totals,
// markers and states: the logger has only read the playheads].
// Layers 1 to 4 are taken as players 1 to 4 [Verify with the Bridge].
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace psyight::tcnet {

inline constexpr int kManagementPort = 60000;
inline constexpr int kTimePort = 60001;
inline constexpr int kDataPort = 60002;
inline constexpr int kLayers = 8;
inline constexpr std::uint8_t kTypeStatus = 5;
inline constexpr std::uint8_t kTypeTime = 254;

// Layer states (the specification's values).
enum class LayerState : std::uint8_t {
    idle = 0,
    playing = 3,
    looping = 4,
    paused = 5,
    stopped = 6,
    cue_down = 7,
    platter_down = 8,
    fast_forward = 9,
    fast_reverse = 10,
    hold = 11,
};
bool advancing(LayerState state); // Playing, looping or a platter moving forward.

struct Header {
    int node_id = 0;
    int version_major = 0;
    int version_minor = 0;
    std::uint8_t type = 0;
    std::string node_name;
    int sequence = 0;
    int node_type = 0;
    std::uint32_t timestamp_us = 0;
};

struct Time {
    Header header;
    std::array<std::uint32_t, kLayers> playhead_ms{};
    std::array<std::uint32_t, kLayers> total_ms{}; // 0 when the packet is too short to say.
    std::array<std::uint8_t, kLayers> beat_marker{};
    std::array<LayerState, kLayers> state{};
    bool has_totals = false;
    bool has_states = false;
};

std::optional<Header> decode_header(std::span<const std::uint8_t> bytes);
// A time packet, or nullopt for anything else.
std::optional<Time> decode_time(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_time(const Time &t);

} // namespace psyight::tcnet
