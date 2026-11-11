#include "link/tcnet.h"

#include <algorithm>

namespace psyight::tcnet {

namespace {

constexpr size_t kHeaderSize = 24;
constexpr size_t kTimes = 24;
constexpr size_t kTotals = 56;
constexpr size_t kMarkers = 88;
constexpr size_t kStates = 96;
constexpr size_t kTimeSize = 104;

std::uint32_t le32(std::span<const std::uint8_t> b, size_t at) {
    return std::uint32_t{b[at]} | (std::uint32_t{b[at + 1]} << 8) |
           (std::uint32_t{b[at + 2]} << 16) | (std::uint32_t{b[at + 3]} << 24);
}

void put32(std::vector<std::uint8_t> &b, size_t at, std::uint32_t v) {
    for (size_t i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

} // namespace

bool advancing(LayerState state) {
    return state == LayerState::playing || state == LayerState::looping ||
           state == LayerState::fast_forward;
}

std::optional<Header> decode_header(std::span<const std::uint8_t> b) {
    if (b.size() < kHeaderSize || b[4] != 'T' || b[5] != 'C' || b[6] != 'N') {
        return std::nullopt;
    }
    Header h;
    h.node_id = b[0] | (b[1] << 8);
    h.version_major = b[2];
    h.version_minor = b[3];
    h.type = b[7];
    for (size_t i = 8; i < 16 && b[i] != 0; ++i) {
        h.node_name.push_back(static_cast<char>(b[i]));
    }
    h.sequence = b[16];
    h.node_type = b[17];
    h.timestamp_us = le32(b, 20);
    return h;
}

std::optional<Time> decode_time(std::span<const std::uint8_t> b) {
    const std::optional<Header> h = decode_header(b);
    if (!h || h->type != kTypeTime || b.size() < kTotals) {
        return std::nullopt;
    }
    Time t;
    t.header = *h;
    for (size_t l = 0; l < kLayers; ++l) {
        t.playhead_ms[l] = le32(b, kTimes + 4 * l);
    }
    if (b.size() >= kMarkers) {
        t.has_totals = true;
        for (size_t l = 0; l < kLayers; ++l) {
            t.total_ms[l] = le32(b, kTotals + 4 * l);
        }
    }
    if (b.size() >= kTimeSize) {
        t.has_states = true;
        for (size_t l = 0; l < kLayers; ++l) {
            t.beat_marker[l] = b[kMarkers + l];
            t.state[l] = static_cast<LayerState>(b[kStates + l]);
        }
    }
    return t;
}

std::vector<std::uint8_t> encode_time(const Time &t) {
    std::vector<std::uint8_t> b(kTimeSize, 0);
    b[0] = static_cast<std::uint8_t>(t.header.node_id);
    b[1] = static_cast<std::uint8_t>(t.header.node_id >> 8);
    b[2] = static_cast<std::uint8_t>(t.header.version_major);
    b[3] = static_cast<std::uint8_t>(t.header.version_minor);
    b[4] = 'T';
    b[5] = 'C';
    b[6] = 'N';
    b[7] = kTypeTime;
    for (size_t i = 0; i < 8 && i < t.header.node_name.size(); ++i) {
        b[8 + i] = static_cast<std::uint8_t>(t.header.node_name[i]);
    }
    b[16] = static_cast<std::uint8_t>(t.header.sequence);
    b[17] = static_cast<std::uint8_t>(t.header.node_type);
    put32(b, 20, t.header.timestamp_us);
    for (size_t l = 0; l < kLayers; ++l) {
        put32(b, kTimes + 4 * l, t.playhead_ms[l]);
        put32(b, kTotals + 4 * l, t.total_ms[l]);
        b[kMarkers + l] = t.beat_marker[l];
        b[kStates + l] = static_cast<std::uint8_t>(t.state[l]);
    }
    return b;
}

} // namespace psyight::tcnet
