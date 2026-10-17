// The F767 node firmware's portable core (firmware/node-f767/src/core) against the app's output:
// packets the app builds must parse on the node, frames the app sends must reach the right lanes,
// and the node's ArtPollReply must parse in the app's discovery.

#include "engine/rig.h"
#include "output/art_poll.h"
#include "output/packets.h"
#include "output/patch.h"
#include "output/sender.h"

#include "node_core.h"
#include "protocol.h"

#include <doctest/doctest.h>

#include <memory>
#include <random>
#include <vector>

using namespace psyight;

namespace {

// Feeds every packet the app sends into the node core.
struct NodeSink final : PacketSink {
    node_state *node;
    std::uint32_t now_ms = 0;
    int bad = 0;
    explicit NodeSink(node_state *n) : node(n) {}
    void send(std::span<const std::uint8_t> packet, const Ipv4Endpoint &to) override {
        psy_packet p{};
        const bool art = to.port == PSY_ARTNET_PORT;
        const bool ok = art ? psy_parse_art_net(packet.data(), packet.size(), &p)
                            : psy_parse_sacn(packet.data(), packet.size(), &p);
        if (!ok) {
            ++bad;
            return;
        }
        node_on_packet(node, &p, art, now_ms);
    }
};

std::vector<std::uint8_t> rig_bytes(int frame, size_t pixels) {
    std::vector<std::uint8_t> v(3 * pixels);
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::uint8_t>(i * 7 + static_cast<size_t>(frame) * 31 + 1);
    }
    return v;
}

struct LaneBuffer { // About 16 KB: on the heap, not the test's stack.
    std::uint8_t lanes[NODE_LANES][NODE_LANE_BYTES];
};

// The bytes the node should put on lane l: the strip's pixels in GRB (the patch's default).
std::vector<std::uint8_t> expected_lane(const Rig &rig, const std::vector<std::uint8_t> &rgb,
                                        int strip) {
    std::vector<std::uint8_t> out;
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        if (rig.pixels[i].strip == strip) {
            out.push_back(rgb[3 * i + 1]); // G
            out.push_back(rgb[3 * i + 0]); // R
            out.push_back(rgb[3 * i + 2]); // B
        }
    }
    return out;
}

} // namespace

TEST_CASE("node firmware: the app's packets parse on the node; corrupt ones do not") {
    std::vector<std::uint8_t> slots(510);
    for (size_t i = 0; i < slots.size(); ++i) {
        slots[i] = static_cast<std::uint8_t>(i * 3);
    }
    SacnSource source;
    source.sync_universe = 1000;
    source.priority = 120;
    std::vector<std::uint8_t> p;
    build_sacn_data(source, 7, 42, slots, p);
    psy_packet out{};
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &out));
    CHECK(out.kind == PSY_PACKET_DMX);
    CHECK(out.universe == 7);
    CHECK(out.sequence == 42);
    CHECK(out.sync_universe == 1000);
    CHECK(out.priority == 120);
    REQUIRE(out.slot_count == 510);
    CHECK(std::equal(slots.begin(), slots.end(), out.slots));
    p[125] = 1; // Not the DMX start code.
    CHECK_FALSE(psy_parse_sacn(p.data(), p.size(), &out));

    build_sacn_sync(source.cid, 1000, 9, p);
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &out));
    CHECK(out.kind == PSY_PACKET_SYNC);
    CHECK(out.sync_universe == 1000);

    build_art_dmx(0x0123, 17, slots, p);
    REQUIRE(psy_parse_art_net(p.data(), p.size(), &out));
    CHECK(out.kind == PSY_PACKET_DMX);
    CHECK(out.universe == 0x0124);
    CHECK(out.sequence == 17);
    CHECK(out.slot_count == 510);
    CHECK_FALSE(psy_parse_sacn(p.data(), p.size(), &out));
    build_art_sync(p);
    REQUIRE(psy_parse_art_net(p.data(), p.size(), &out));
    CHECK(out.kind == PSY_PACKET_SYNC);
    build_art_poll(p);
    REQUIRE(psy_parse_art_net(p.data(), p.size(), &out));
    CHECK(out.kind == PSY_PACKET_POLL);
    p.resize(10); // Truncated.
    CHECK_FALSE(psy_parse_art_net(p.data(), p.size(), &out));

    std::uint8_t group[4];
    psy_sacn_group(0x0102, group);
    CHECK(sacn_multicast_group(0x0102) ==
          std::array<std::uint8_t, 4>{group[0], group[1], group[2], group[3]});
}

TEST_CASE("node firmware: the node's ArtPollReply parses in the app's discovery") {
    psy_poll_reply r{};
    const std::uint8_t ip[4] = {2, 0, 0, 10};
    std::copy(ip, ip + 4, r.ip);
    r.short_name = "Psyight node";
    r.long_name = "Psyight STM32F767 pixel node";
    r.report = "#0001 [0042] OK";
    r.universes[0] = 1;
    r.universes[1] = 2;
    r.universes[2] = 3;
    r.universe_count = 3;
    r.bind_index = 1;
    std::uint8_t buffer[PSY_ART_POLL_REPLY_BYTES];
    REQUIRE(psy_build_art_poll_reply(&r, buffer, sizeof(buffer)) == PSY_ART_POLL_REPLY_BYTES);
    const auto node = parse_art_poll_reply(std::span<const std::uint8_t>(buffer, sizeof(buffer)));
    REQUIRE(node.has_value());
    CHECK(node->ip == std::array<std::uint8_t, 4>{2, 0, 0, 10});
    CHECK(node->short_name == "Psyight node");
    CHECK(node->long_name == "Psyight STM32F767 pixel node");
    CHECK(node->output_universes == std::vector<std::uint16_t>{1, 2, 3});
    CHECK(node->bind_index == 1);
    r.universes[1] = 17; // Another Sub-Net: needs its own reply.
    CHECK(psy_build_art_poll_reply(&r, buffer, sizeof(buffer)) == 0);
}

TEST_CASE("node firmware: frames from the app reach the right lanes, latched on sync") {
    const Rig rig = default_rig();
    const Patch patch = build_patch(rig, 1);
    for (const OutputProtocol protocol : {OutputProtocol::sacn, OutputProtocol::art_net}) {
        CAPTURE(static_cast<int>(protocol));
        auto node = std::make_unique<node_state>();
        const node_settings settings = node_default_settings();
        node_init(node.get(), &settings);
        OutputSettings out;
        out.protocol = protocol;
        OutputSender sender;
        NodeSink sink(node.get());
        auto lane_buffer = std::make_unique<LaneBuffer>();
        auto &lanes = lane_buffer->lanes;
        for (int f = 0; f < 5; ++f) {
            sink.now_ms = 100 + 17 * static_cast<std::uint32_t>(f);
            const std::vector<std::uint8_t> rgb = rig_bytes(f, rig.pixels.size());
            sender.send_frame(out, patch_frame(patch, rgb), sink);
            REQUIRE(node_frame(node.get(), sink.now_ms, lanes));
            for (int strip = 0; strip < 4; ++strip) {
                CAPTURE(strip);
                const std::vector<std::uint8_t> want = expected_lane(rig, rgb, strip);
                CHECK(std::equal(want.begin(), want.end(), lanes[strip]));
            }
            CHECK_FALSE(node_frame(node.get(), sink.now_ms + 1, lanes)); // Nothing new.
        }
        CHECK(sink.bad == 0);
        CHECK(node->sequence_gaps == 0);
        CHECK(node->ignored == 0);
    }
}

TEST_CASE("node firmware: sync holds data until the sync packet, with timeouts") {
    const node_settings settings = node_default_settings();
    auto node = std::make_unique<node_state>();
    node_init(node.get(), &settings);
    auto lane_buffer = std::make_unique<LaneBuffer>();
    auto &lanes = lane_buffer->lanes;
    std::vector<std::uint8_t> slots(510, 200);
    SacnSource source;
    source.sync_universe = 1000;
    std::vector<std::uint8_t> p;
    psy_packet in{};
    build_sacn_data(source, 1, 0, slots, p);
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &in));
    node_on_packet(node.get(), &in, false, 10);
    CHECK_FALSE(node_frame(node.get(), 10, lanes)); // Same millisecond: still held (no wrap).
    CHECK_FALSE(node_frame(node.get(), 20, lanes)); // Held for the sync.
    build_sacn_sync(source.cid, 999, 0, p);         // Another sync group's.
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &in));
    node_on_packet(node.get(), &in, false, 25);
    CHECK_FALSE(node_frame(node.get(), 26, lanes));
    build_sacn_sync(source.cid, 1000, 0, p);
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &in));
    node_on_packet(node.get(), &in, false, 30);
    REQUIRE(node_frame(node.get(), 31, lanes));
    CHECK(lanes[0][0] == 200);

    // A sync that never comes: latched after 1 s anyway.
    slots.assign(510, 50);
    build_sacn_data(source, 1, 1, slots, p);
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &in));
    node_on_packet(node.get(), &in, false, 100);
    CHECK_FALSE(node_frame(node.get(), 900, lanes));
    node_on_packet(node.get(), &in, false, 1050); // Data keeps coming, so no signal loss.
    REQUIRE(node_frame(node.get(), 1150, lanes));
    CHECK(lanes[0][0] == 50);

    // Art-Net: sync mode starts at the first ArtSync and ends 4 s after the last.
    auto art = std::make_unique<node_state>();
    node_init(art.get(), &settings);
    build_art_sync(p);
    REQUIRE(psy_parse_art_net(p.data(), p.size(), &in));
    node_on_packet(art.get(), &in, true, 0);
    slots.assign(510, 77);
    build_art_dmx(0, 1, slots, p);
    REQUIRE(psy_parse_art_net(p.data(), p.size(), &in));
    node_on_packet(art.get(), &in, true, 10);
    CHECK_FALSE(node_frame(art.get(), 11, lanes)); // Held.
    node_on_packet(art.get(), &in, true, 1500);
    node_on_packet(art.get(), &in, true, 3000);
    node_on_packet(art.get(), &in, true, 4100);
    REQUIRE(node_frame(art.get(), 4100, lanes)); // No ArtSync for 4 s: latched on data again.
    CHECK(lanes[0][0] == 77);
}

TEST_CASE("node firmware: signal loss runs the chosen policy") {
    for (const node_loss_policy policy :
         {NODE_LOSS_HOLD, NODE_LOSS_BLACKOUT, NODE_LOSS_FADE, NODE_LOSS_FALLBACK}) {
        CAPTURE(static_cast<int>(policy));
        node_settings settings = node_default_settings();
        settings.loss_policy = policy;
        settings.loss_timeout_ms = 1000;
        settings.fade_ms = 1000;
        auto node = std::make_unique<node_state>();
        node_init(node.get(), &settings);
        auto lane_buffer = std::make_unique<LaneBuffer>();
        auto &lanes = lane_buffer->lanes;
        std::vector<std::uint8_t> slots(510, 160);
        std::vector<std::uint8_t> p;
        build_art_dmx(0, 1, slots, p);
        psy_packet in{};
        REQUIRE(psy_parse_art_net(p.data(), p.size(), &in));
        node_on_packet(node.get(), &in, true, 100);
        REQUIRE(node_frame(node.get(), 101, lanes));
        CHECK_FALSE(node_signal_lost(node.get(), 1000));
        CHECK(node_signal_lost(node.get(), 1200));
        const bool changed = node_frame(node.get(), 1200, lanes);
        const std::uint8_t first = lanes[0][0];
        switch (policy) {
        case NODE_LOSS_HOLD:
            CHECK(changed);
            CHECK(first == 160);
            CHECK_FALSE(node_frame(node.get(), 2000, lanes));
            break;
        case NODE_LOSS_BLACKOUT:
            CHECK(first == 0);
            CHECK_FALSE(node_frame(node.get(), 2000, lanes));
            break;
        case NODE_LOSS_FADE:
            CHECK(first == 160); // Just started fading.
            REQUIRE(node_frame(node.get(), 1700, lanes));
            CHECK(lanes[0][0] == doctest::Approx(80).epsilon(0.05));
            REQUIRE(node_frame(node.get(), 2300, lanes));
            CHECK(lanes[0][0] == 0);
            CHECK_FALSE(node_frame(node.get(), 2400, lanes));
            break;
        case NODE_LOSS_FALLBACK: {
            CHECK(changed);
            int lit = 0;
            int brightest = 0;
            for (int i = 0; i < 150 * 3; ++i) {
                lit += lanes[0][i] > 0;
                brightest = std::max(brightest, static_cast<int>(lanes[0][i]));
            }
            CHECK(lit > 100);       // A visible plasma...
            CHECK(brightest < 128); // ...that stays dim (brightness 0.25 before gamma).
            CHECK(node_frame(node.get(), 1300, lanes)); // It animates.
            break;
        }
        }
        // Data again: live output resumes.
        node_on_packet(node.get(), &in, true, 3000);
        REQUIRE(node_frame(node.get(), 3001, lanes));
        CHECK(lanes[0][0] == 160);
    }
}

TEST_CASE("node firmware: the 8-lane transpose matches a bit-by-bit reference") {
    auto lane_buffer = std::make_unique<LaneBuffer>();
    auto &lanes = lane_buffer->lanes;
    std::mt19937 rng(7);
    for (auto &lane : lanes) {
        for (auto &b : lane) {
            b = static_cast<std::uint8_t>(rng());
        }
    }
    const std::uint16_t pixels = 37;
    const std::uint8_t active = 0x5f; // Lanes 0 to 4 and 6.
    std::vector<std::uint32_t> words(24u * pixels);
    node_transpose(lanes, pixels, active, words.data());
    for (std::uint32_t byte = 0; byte < 3u * pixels; ++byte) {
        for (int bit = 0; bit < 8; ++bit) { // MSB first.
            std::uint32_t zeros = 0;
            for (int lane = 0; lane < 8; ++lane) {
                const bool one = (lanes[lane][byte] >> (7 - bit)) & 1;
                if (!one && (active & (1u << lane))) {
                    zeros |= 1u << lane;
                }
            }
            REQUIRE(words[byte * 8u + static_cast<std::uint32_t>(bit)] == zeros << 16);
        }
    }
}
