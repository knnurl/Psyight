#include "output/art_poll.h"

#include <algorithm>
#include <cstring>

namespace psyight {

namespace {

constexpr std::array<std::uint8_t, 8> kArtNetId = {'A', 'r', 't', '-', 'N', 'e', 't', 0};
constexpr std::uint16_t kOpPoll = 0x2000;
constexpr std::uint16_t kOpPollReply = 0x2100;
constexpr std::uint16_t kArtNetPortNumber = 6454;
constexpr size_t kMaxPorts = 4;
// ArtPollReply field offsets.
constexpr size_t kIp = 10;
constexpr size_t kPort = 14;
constexpr size_t kNetSwitch = 18;
constexpr size_t kSubSwitch = 19;
constexpr size_t kShortName = 26;
constexpr size_t kShortNameBytes = 18;
constexpr size_t kLongName = 44;
constexpr size_t kLongNameBytes = 64;
constexpr size_t kNodeReport = 108;
constexpr size_t kNodeReportBytes = 64;
constexpr size_t kNumPorts = 172;
constexpr size_t kPortTypes = 174;
constexpr size_t kSwOut = 190;
constexpr size_t kBindIndex = 211;
constexpr std::uint8_t kPortCanOutput = 0x80;

std::uint16_t opcode(std::span<const std::uint8_t> p) {
    return static_cast<std::uint16_t>(p[8] | (p[9] << 8));
}

void put_header(std::vector<std::uint8_t> &out, std::uint16_t op) {
    std::copy(kArtNetId.begin(), kArtNetId.end(), out.begin());
    out[8] = static_cast<std::uint8_t>(op);
    out[9] = static_cast<std::uint8_t>(op >> 8);
}

std::string text_field(std::span<const std::uint8_t> p, size_t at, size_t bytes) {
    const auto *begin = reinterpret_cast<const char *>(p.data() + at);
    return std::string(begin, strnlen(begin, bytes));
}

void put_text(std::vector<std::uint8_t> &out, size_t at, size_t bytes, const std::string &text) {
    std::memcpy(out.data() + at, text.data(), std::min(text.size(), bytes - 1)); // Null-ended.
}

} // namespace

size_t build_art_poll(std::vector<std::uint8_t> &out) {
    out.assign(kArtPollBytes, 0);
    put_header(out, kOpPoll);
    out[10] = 0; // Protocol version, big-endian: 14.
    out[11] = 14;
    out[12] = 0; // Flags: reply to polls only, no diagnostics.
    out[13] = 0; // Diagnostics priority.
    return out.size();
}

bool is_art_poll(std::span<const std::uint8_t> p) {
    return p.size() >= kArtPollBytes && std::equal(kArtNetId.begin(), kArtNetId.end(), p.begin()) &&
           opcode(p) == kOpPoll;
}

size_t build_art_poll_reply(const ArtNode &node, std::vector<std::uint8_t> &out) {
    const size_t ports = std::min(node.output_universes.size(), kMaxPorts);
    if (ports == 0) {
        return 0;
    }
    // One reply has one Net and Sub-Net; the ports differ only in the low four bits.
    const auto first = static_cast<std::uint16_t>(node.output_universes[0] - 1);
    for (size_t i = 0; i < ports; ++i) {
        if (node.output_universes[i] < 1 ||
            ((node.output_universes[i] - 1) & 0x7ff0) != (first & 0x7ff0)) {
            return 0;
        }
    }
    out.assign(kArtPollReplyBytes, 0);
    put_header(out, kOpPollReply);
    std::copy(node.ip.begin(), node.ip.end(), out.begin() + kIp);
    out[kPort] = static_cast<std::uint8_t>(kArtNetPortNumber); // Little-endian.
    out[kPort + 1] = static_cast<std::uint8_t>(kArtNetPortNumber >> 8);
    out[kNetSwitch] = static_cast<std::uint8_t>((first >> 8) & 0x7f);
    out[kSubSwitch] = static_cast<std::uint8_t>((first >> 4) & 0x0f);
    put_text(out, kShortName, kShortNameBytes, node.short_name);
    put_text(out, kLongName, kLongNameBytes, node.long_name);
    put_text(out, kNodeReport, kNodeReportBytes, node.report);
    out[kNumPorts] = 0;
    out[kNumPorts + 1] = static_cast<std::uint8_t>(ports);
    for (size_t i = 0; i < ports; ++i) {
        out[kPortTypes + i] = kPortCanOutput; // DMX512 output.
        out[kSwOut + i] = static_cast<std::uint8_t>((node.output_universes[i] - 1) & 0x0f);
    }
    out[kBindIndex] = node.bind_index;
    return out.size();
}

std::optional<ArtNode> parse_art_poll_reply(std::span<const std::uint8_t> p) {
    // Nodes older than Art-Net 4 send shorter replies; everything read here is in the first 212.
    if (p.size() < kBindIndex + 1 || !std::equal(kArtNetId.begin(), kArtNetId.end(), p.begin()) ||
        opcode(p) != kOpPollReply) {
        return std::nullopt;
    }
    ArtNode node;
    std::copy(p.begin() + kIp, p.begin() + kIp + 4, node.ip.begin());
    node.short_name = text_field(p, kShortName, kShortNameBytes);
    node.long_name = text_field(p, kLongName, kLongNameBytes);
    node.report = text_field(p, kNodeReport, kNodeReportBytes);
    const size_t ports = std::min<size_t>(p[kNumPorts + 1], kMaxPorts);
    const auto base =
        static_cast<std::uint16_t>(((p[kNetSwitch] & 0x7f) << 8) | ((p[kSubSwitch] & 0x0f) << 4));
    for (size_t i = 0; i < ports; ++i) {
        if (p[kPortTypes + i] & kPortCanOutput) {
            node.output_universes.push_back(
                static_cast<std::uint16_t>((base | (p[kSwOut + i] & 0x0f)) + 1));
        }
    }
    node.bind_index = p[kBindIndex];
    return node;
}

} // namespace psyight
