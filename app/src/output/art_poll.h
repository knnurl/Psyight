// Art-Net 4 discovery (SPEC section 8): ArtPoll, which a controller broadcasts, and ArtPollReply,
// which each node (or each bound port group of a node) answers with. The reply layout follows
// the Art-Net 4 specification's ArtPollReply table [Verify against the current revision]; the
// builder exists for tests and as a reference for the F767 node's firmware.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace psyight {

constexpr size_t kArtPollBytes = 14;
constexpr size_t kArtPollReplyBytes = 239;

size_t build_art_poll(std::vector<std::uint8_t> &out);
bool is_art_poll(std::span<const std::uint8_t> packet);

struct ArtNode {
    std::array<std::uint8_t, 4> ip{};
    std::string short_name;
    std::string long_name;
    std::string report;
    std::vector<std::uint16_t> output_universes; // As sACN numbers (Art-Net port address + 1).
    std::uint8_t bind_index = 0;                 // Which of a node's replies this is.
};

size_t build_art_poll_reply(const ArtNode &node, std::vector<std::uint8_t> &out);
std::optional<ArtNode> parse_art_poll_reply(std::span<const std::uint8_t> packet);

} // namespace psyight
