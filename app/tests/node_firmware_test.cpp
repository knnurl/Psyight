// The F767 node firmware's portable core (firmware/node-f767/src/core) against the app's output:
// packets the app builds must parse on the node, frames the app sends must reach the right lanes,
// and the node's ArtPollReply must parse in the app's discovery.

#include "engine/rig.h"
#include "engine/rig_templates.h"
#include "io/project.h"
#include "output/art_poll.h"
#include "output/node_setup.h"
#include "output/packets.h"
#include "output/patch.h"
#include "output/sender.h"

#include "node_config.h"
#include "node_core.h"
#include "protocol.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

TEST_CASE("node firmware: packets stamped after the frame's time are not a lost signal") {
    // The main loop reads the time once, then polls the network, whose packets are stamped as
    // they arrive: a millisecond later when the tick moves on mid-poll. That time must not wrap
    // round to "4 294 967 s since the last packet" (one frame of the fallback: the owner's
    // hexagon pulsed purple at random) nor latch a universe before its sync.
    const node_settings settings = node_default_settings(); // Fallback on loss.
    auto node = std::make_unique<node_state>();
    node_init(node.get(), &settings);
    auto lane_buffer = std::make_unique<LaneBuffer>();
    auto &lanes = lane_buffer->lanes;
    std::vector<std::uint8_t> slots(510, 0); // Black: a fallback frame would show.
    SacnSource source;
    source.sync_universe = 1000;
    std::vector<std::uint8_t> p;
    psy_packet in{};
    build_sacn_data(source, 1, 0, slots, p);
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &in));
    node_on_packet(node.get(), &in, false, 5000);
    build_sacn_sync(source.cid, 1000, 0, p);
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &in));
    node_on_packet(node.get(), &in, false, 5000);
    REQUIRE(node_frame(node.get(), 5000, lanes));

    // The next frame's data, stamped 5017 while the loop still says 5016.
    build_sacn_data(source, 1, 1, slots, p);
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &in));
    node_on_packet(node.get(), &in, false, 5017);
    CHECK_FALSE(node_signal_lost(node.get(), 5016));
    CHECK_FALSE(node_frame(node.get(), 5016, lanes)); // Held for its sync, not latched early.
    build_sacn_sync(source.cid, 1000, 1, p);
    REQUIRE(psy_parse_sacn(p.data(), p.size(), &in));
    node_on_packet(node.get(), &in, false, 5017);
    REQUIRE(node_frame(node.get(), 5016, lanes));
    CHECK(lanes[0][0] == 0); // The data (black), not the fallback's violet.
    CHECK(lanes[0][1] == 0);
    CHECK(lanes[0][2] == 0);
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
            CHECK(lit > 30); // An aurora, lit in places...
            // ...and dim: brightness 0.1 before gamma is at most 0.1^2.2 of full drive.
            CHECK(brightest <= static_cast<int>(std::pow(0.1, 2.2) * 255.0 + 1.0));
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

TEST_CASE("node firmware: the configuration text protocol") {
    const std::uint8_t mac[6] = {0x02, 0x50, 0x12, 0x34, 0x56, 0x78};
    node_config c{};
    node_config_defaults(&c, mac);
    CHECK(node_config_valid(&c));
    CHECK(c.ip[0] == 2);
    CHECK(c.ip[3] == 0x78);
    char reply[2048];
    const auto run = [&](const char *command) {
        return node_config_command(&c, mac, command, std::strlen(command), reply, sizeof(reply));
    };
    CHECK(run("get\r\n") == NODE_CMD_NONE);
    CHECK(std::string(reply).find("lane1 150 72\n") != std::string::npos);
    CHECK(std::string(reply).find("policy fallback\n") != std::string::npos);
    CHECK(run("set ip 192.168.1.50") == NODE_CMD_NONE);
    CHECK(std::string(reply).rfind("ok ip", 0) == 0);
    CHECK(c.ip[0] == 192);
    CHECK(c.ip[3] == 50);
    CHECK(node_config_valid(&c)); // The CRC follows each change.
    run("set lane5 444 60");
    CHECK(c.node.lanes[5].first_pixel == 444);
    CHECK(c.node.lanes[5].pixels == 60);
    run("set policy blackout");
    CHECK(c.node.loss_policy == NODE_LOSS_BLACKOUT);
    run("set fallback_brightness 125");
    CHECK(c.node.fallback_brightness == doctest::Approx(0.125f));
    run("set name Booth left");
    CHECK(std::string(c.short_name) == "Booth left");
    for (const char *bad : {"set ip 300.1.1.1", "set lane9 0 1", "set lane0 0 5000",
                            "set universes 99", "set colour red", "set name", "frobnicate",
                            "set name this name is far too long to fit"}) {
        CAPTURE(bad);
        run(bad);
        CHECK(std::string(reply).rfind("error", 0) == 0);
    }
    CHECK(run("save") == NODE_CMD_SAVE);
    CHECK(run("reboot") == NODE_CMD_REBOOT);
    c.node.fade_ms = 1; // Changed without updating the CRC: no longer valid.
    CHECK_FALSE(node_config_valid(&c));
    run("defaults");
    CHECK(node_config_valid(&c));
    CHECK(c.ip[0] == 2);
}

TEST_CASE("node firmware: protocol 2 and 3: apply, the fallback palette, the version line") {
    const std::uint8_t mac[6] = {0x02, 0x50, 0x12, 0x34, 0x56, 0x78};
    node_config c{};
    node_config_defaults(&c, mac);
    char reply[2048];
    const auto run = [&](const char *command) {
        return node_config_command(&c, mac, command, std::strlen(command), reply, sizeof(reply));
    };
    run("get");
    CHECK(std::string(reply).rfind("protocol 3\n", 0) == 0); // First, so old and new tell apart.
    // The palette: RGB in, RGB stored (each lane orders it), RGB out again.
    CHECK(run("set palette FF0080 00ff00") == NODE_CMD_NONE);
    CHECK(std::string(reply).rfind("ok palette", 0) == 0);
    REQUIRE(c.node.palette_size == 2);
    CHECK(c.node.palette[0][0] == 0xFF); // R
    CHECK(c.node.palette[0][1] == 0x00); // G
    CHECK(c.node.palette[0][2] == 0x80); // B
    CHECK(c.node.palette[1][1] == 0xFF);
    run("get");
    CHECK(std::string(reply).find("palette FF0080 00FF00\n") != std::string::npos);
    CHECK(node_config_valid(&c));
    for (const char *bad :
         {"set palette", "set palette FF00", "set palette GG0000",
          "set palette 111111 222222 333333 444444 555555", "set palette FF00801"}) {
        CAPTURE(bad);
        run(bad);
        CHECK(std::string(reply).rfind("error", 0) == 0);
        CHECK(c.node.palette_size == 2); // Unchanged by a bad value.
    }
    // Rig settings say apply; network settings still need a restart; names act at once.
    run("set universes 9");
    CHECK(std::string(reply).find("apply") != std::string::npos);
    run("set dhcp 0");
    CHECK(std::string(reply).find("reboot") != std::string::npos);
    run("set name Stage left");
    CHECK(std::string(reply) == "ok name\n");
    CHECK(run("apply") == NODE_CMD_APPLY);
    CHECK(std::string(reply).rfind("ok applied", 0) == 0);
}

TEST_CASE("node setup: the app's limits are the firmware's, and the default frame's plan is its "
          "defaults") {
    CHECK(kNodeLanes == NODE_LANES);
    CHECK(kNodeLanePixels == NODE_MAX_PIXELS_PER_LANE);
    CHECK(kNodeMaxUniverses == NODE_MAX_UNIVERSES);
    CHECK(kNodeConfigPort == NODE_CONFIG_PORT);
    CHECK(kNodeProtocol == static_cast<int>(NODE_PROTOCOL_VERSION));
    const NodePlan plan = plan_node(default_project());
    CHECK(plan.problems.empty());
    const node_settings defaults = node_default_settings();
    CHECK(plan.universes == defaults.universe_count);
    CHECK(plan.first_universe == defaults.first_universe);
    REQUIRE(plan.lanes.size() == 1); // 444 pixels chain on one data line...
    // ...which the firmware's defaults spread over four (one per side of the frame): both work, and
    // the comparison says where they differ.
    const std::uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
    node_config c{};
    node_config_defaults(&c, mac);
    char reply[2048];
    node_config_command(&c, mac, "get", 3, reply, sizeof(reply));
    const auto settings = parse_node_settings(reply);
    CHECK(node_protocol(settings) == 3);
    CHECK(settings.at("universes") == "3");
    CHECK_FALSE(node_differences(plan, settings).empty());
    CHECK(node_protocol(parse_node_settings("ip 2.0.0.1\nuniverses 3\n")) == 1); // Old firmware.
}

TEST_CASE("node setup: every built-in rig, set up by the app's commands, reaches the right lanes") {
    for (const RigPreset &preset : builtin_rig_presets()) {
        CAPTURE(preset.name);
        Project project = default_project();
        project.strips.clear();
        for (const StripSpec &s : preset.strips) {
            project.strips.push_back({s, ColourOrder::grb});
        }
        const NodePlan plan = plan_node(project);
        REQUIRE(plan.problems.empty());
        CHECK(plan.lanes.size() <= static_cast<size_t>(NODE_LANES));
        // The node takes the app's commands, every one answered "ok".
        const std::uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
        node_config c{};
        node_config_defaults(&c, mac);
        char reply[2048];
        const Palette palette = default_palettes().front();
        for (const std::string &command : node_commands(plan, &palette)) {
            CAPTURE(command);
            node_config_command(&c, mac, command.c_str(), command.size(), reply, sizeof(reply));
            CHECK(std::string(reply).rfind("ok", 0) == 0);
        }
        node_config_command(&c, mac, "get", 3, reply, sizeof(reply));
        CHECK(node_differences(plan, parse_node_settings(reply)).empty());
        CHECK(c.node.palette_size == std::min<size_t>(palette.colours.size(), 4));
        // Frames from the app land on the planned lanes: each lane, its strips' pixels in turn.
        const Rig rig = rig_of(project);
        const Patch patch = patch_of(project);
        auto node = std::make_unique<node_state>();
        node_init(node.get(), &c.node);
        OutputSettings out;
        OutputSender sender;
        NodeSink sink(node.get());
        auto lane_buffer = std::make_unique<LaneBuffer>();
        const std::vector<std::uint8_t> rgb = rig_bytes(1, rig.pixels.size());
        sink.now_ms = 100;
        sender.send_frame(out, patch_frame(patch, rgb), sink);
        REQUIRE(node_frame(node.get(), sink.now_ms, lane_buffer->lanes));
        for (size_t l = 0; l < plan.lanes.size(); ++l) {
            CAPTURE(l);
            std::vector<std::uint8_t> want;
            for (int s = 0; s < plan.lanes[l].strips; ++s) {
                const std::vector<std::uint8_t> strip =
                    expected_lane(rig, rgb, plan.lanes[l].first_strip + s);
                want.insert(want.end(), strip.begin(), strip.end());
            }
            CHECK(want.size() == static_cast<size_t>(plan.lanes[l].pixels) * 3);
            CHECK(std::equal(want.begin(), want.end(), lane_buffer->lanes[l]));
        }
        CHECK(sink.bad == 0);
        CHECK(node->ignored == 0);
    }
}

TEST_CASE("node firmware: lane colour orders: set, reported, and used by the fallback show") {
    const std::uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
    node_config c{};
    node_config_defaults(&c, mac);
    char reply[2048];
    const auto run = [&](const char *command) {
        node_config_command(&c, mac, command, std::strlen(command), reply, sizeof(reply));
        return std::string(reply);
    };
    CHECK(run("get").find("order0 grb\n") != std::string::npos); // WS2812 by default.
    CHECK(run("set order1 rgb").rfind("ok order1", 0) == 0);
    CHECK(run("set order2 bgr").rfind("ok", 0) == 0);
    CHECK(run("set order0 xyz").rfind("error", 0) == 0);
    CHECK(run("set order8 rgb").rfind("error", 0) == 0);
    CHECK(c.node.lane_order[0] == NODE_ORDER_GRB);
    const std::string got = run("get");
    CHECK(got.find("order1 rgb\n") != std::string::npos);
    CHECK(got.find("order2 bgr\n") != std::string::npos);
    // One pure red colour: lane 0 (grb) puts it second, lane 1 (rgb) first, lane 2 (bgr) third.
    run("set palette FF0000");
    node_settings s = c.node;
    s.loss_policy = NODE_LOSS_FALLBACK;
    s.loss_timeout_ms = 100;
    s.fallback_brightness = 1.0f;
    for (int l = 0; l < 3; ++l) {
        s.lanes[l] = {0, 10};
    }
    auto node = std::make_unique<node_state>();
    node_init(node.get(), &s);
    auto lane_buffer = std::make_unique<LaneBuffer>();
    auto &lanes = lane_buffer->lanes;
    REQUIRE(node_frame(node.get(), 1000, lanes));
    // The aurora lights some pixels; red goes to the lane's red byte and nowhere else.
    const auto red_at = [&](int lane, int place) {
        bool lit = false;
        for (int i = 0; i < 10; ++i) {
            for (int c = 0; c < 3; ++c) {
                const std::uint8_t v = lanes[lane][i * 3 + c];
                CHECK((c == place || v == 0));
                lit |= c == place && v > 0;
            }
        }
        return lit;
    };
    CHECK(red_at(0, 1));
    CHECK(red_at(1, 0));
    CHECK(red_at(2, 2));
}

TEST_CASE("node firmware: the fallback show is an aurora: drifting waves with dark between") {
    node_settings s = node_default_settings();
    s.loss_timeout_ms = 100;
    s.lanes[0] = {0, 408}; // The owner's hexagon on one line.
    for (int l = 1; l < NODE_LANES; ++l) {
        s.lanes[l] = {0, 0};
    }
    auto node = std::make_unique<node_state>();
    node_init(node.get(), &s);
    auto a = std::make_unique<LaneBuffer>();
    auto b = std::make_unique<LaneBuffer>();
    // Brightest drive the default brightness allows: 0.1 perceptual.
    const auto most = static_cast<int>(std::pow(0.1, 2.2) * 255.0 + 1.0);
    int lit = 0;
    int pixels = 0;
    // From a minute up to 40 days of uptime (the clock's float precision).
    for (const std::uint32_t t : {60000u, 61000u, 3600000u, 3456000000u}) {
        CAPTURE(t);
        REQUIRE(node_frame(node.get(), t, a->lanes));
        REQUIRE(node_frame(node.get(), t + 500, b->lanes));
        CHECK(std::memcmp(a->lanes[0], b->lanes[0], 408 * 3) != 0); // Drifting.
        for (int i = 0; i < 408; ++i) {
            const std::uint8_t *p = &a->lanes[0][i * 3]; // GRB.
            CHECK(std::max({p[0], p[1], p[2]}) <= most);
            lit += std::max({p[0], p[1], p[2]}) > 0 ? 1 : 0;
            ++pixels;
        }
    }
    CAPTURE(lit);
    CHECK(lit > pixels / 5);      // Waves...
    CHECK(lit < pixels * 9 / 10); // ...with dark between them.
    // Deterministic: the same time, the same frame.
    auto again = std::make_unique<node_state>();
    node_init(again.get(), &s);
    REQUIRE(node_frame(again.get(), 3600000u, b->lanes));
    REQUIRE(node_frame(node.get(), 3600000u, a->lanes));
    CHECK(std::memcmp(a->lanes[0], b->lanes[0], 408 * 3) == 0);
}

namespace {

std::uint32_t test_crc32(const std::uint8_t *p, size_t n) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

} // namespace

TEST_CASE("node firmware: a config saved by protocol 2 firmware upgrades, keeping its settings") {
    // Version 1's layout: the current one up to the palette size, without the lane orders, the
    // palette as GRB, padded, then its CRC.
    const std::uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
    node_config current{};
    node_config_defaults(&current, mac);
    current.node.universe_count = 7;
    current.node.lanes[3] = {100, 200};
    current.node.loss_policy = NODE_LOSS_BLACKOUT;
    std::snprintf(current.short_name, sizeof(current.short_name), "Stage left");
    const std::uint8_t grb[3] = {0x00, 0xFF, 0x80}; // R FF, G 00, B 80.
    std::memcpy(current.node.palette[0], grb, 3);
    current.node.palette_size = 1;
    const size_t prefix = offsetof(node_config, node) + offsetof(node_settings, lane_order);
    std::vector<std::uint8_t> v1(node_config_v1_size(), 0);
    REQUIRE(v1.size() > prefix + 4);
    std::memcpy(v1.data(), &current, prefix);
    const std::uint32_t version = 1;
    std::memcpy(v1.data() + offsetof(node_config, version), &version, 4);
    const std::uint32_t crc = test_crc32(v1.data(), v1.size() - 4);
    std::memcpy(v1.data() + v1.size() - 4, &crc, 4);

    node_config upgraded{};
    REQUIRE(node_config_upgrade(v1.data(), &upgraded));
    CHECK(node_config_valid(&upgraded));
    CHECK(upgraded.version == NODE_CONFIG_VERSION);
    CHECK(upgraded.node.universe_count == 7);
    CHECK(upgraded.node.lanes[3].first_pixel == 100);
    CHECK(upgraded.node.lanes[3].pixels == 200);
    CHECK(upgraded.node.loss_policy == NODE_LOSS_BLACKOUT);
    CHECK(std::string(upgraded.short_name) == "Stage left");
    CHECK(upgraded.node.palette[0][0] == 0xFF); // Now RGB.
    CHECK(upgraded.node.palette[0][1] == 0x00);
    CHECK(upgraded.node.palette[0][2] == 0x80);
    for (int l = 0; l < NODE_LANES; ++l) {
        CHECK(upgraded.node.lane_order[l] == NODE_ORDER_GRB);
    }
    // Not version 1, or corrupt: left alone.
    v1[v1.size() - 1] ^= 0xFF;
    node_config untouched{};
    CHECK_FALSE(node_config_upgrade(v1.data(), &untouched));
    std::vector<std::uint8_t> newer(sizeof(node_config) + 16, 0);
    std::memcpy(newer.data(), &current, sizeof(node_config));
    CHECK_FALSE(node_config_upgrade(newer.data(), &untouched)); // Version 2: not an upgrade.
}

TEST_CASE("node setup: lane orders, signal-loss settings, notes, and older firmware") {
    Project project = default_project();
    project.strips[1].order = ColourOrder::rgb; // Mixed on the one line the frame chains onto.
    project.node.policy = NodeLossPolicy::fade;
    project.node.loss_ms = 1500;
    project.node.fade_ms = 4000;
    project.node.brightness = 0.4;
    project.node.speed_hz = 0.1;
    NodePlan plan = plan_node(project);
    REQUIRE(plan.lanes.size() == 1);
    CHECK(plan.lanes[0].order == ColourOrder::grb);
    REQUIRE(plan.notes.size() == 1);
    CHECK(plan.notes[0].find("rgb") != std::string::npos);
    const std::vector<std::string> commands = node_commands(plan, nullptr);
    const auto has = [&](const std::vector<std::string> &list, const std::string &c) {
        return std::find(list.begin(), list.end(), c) != list.end();
    };
    CHECK(has(commands, "set order0 grb"));
    CHECK(has(commands, "set policy fade"));
    CHECK(has(commands, "set loss_ms 1500"));
    CHECK(has(commands, "set fade_ms 4000"));
    CHECK(has(commands, "set fallback_brightness 400"));
    CHECK(has(commands, "set fallback_speed 100"));
    CHECK(commands.back() == "apply");
    // Older firmware: the same without the orders.
    const std::vector<std::string> old = commands_for_protocol(commands, 2);
    CHECK_FALSE(has(old, "set order0 grb"));
    CHECK(old.size() == commands.size() - 1);
    CHECK(commands_for_protocol(commands, 3) == commands);
    // The node takes them all, and reads back matching.
    const std::uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
    node_config c{};
    node_config_defaults(&c, mac);
    char reply[2048];
    for (const std::string &command : commands) {
        CAPTURE(command);
        node_config_command(&c, mac, command.c_str(), command.size(), reply, sizeof(reply));
        CHECK(std::string(reply).rfind("ok", 0) == 0);
    }
    node_config_command(&c, mac, "get", 3, reply, sizeof(reply));
    CHECK(node_differences(plan, parse_node_settings(reply)).empty());
    CHECK(c.node.loss_policy == NODE_LOSS_FADE);
    // A different project's settings show as differences.
    project.node.policy = NodeLossPolicy::hold;
    CHECK_FALSE(node_differences(plan_node(project), parse_node_settings(reply)).empty());
    // A long line: a frame-rate note.
    Project long_line = default_project();
    long_line.strips.resize(1);
    long_line.strips[0].spec.count = 600;
    const NodePlan slow = plan_node(long_line);
    REQUIRE(slow.notes.size() == 1);
    CHECK(slow.notes[0].find("600 pixels") != std::string::npos);
    CHECK(node_lane_max_fps(680) == doctest::Approx(48.3).epsilon(0.01)); // SPEC 12.3.
    CHECK(node_lane_max_fps(kNodeLanePixelsAt60Fps) >= 60.0);
    CHECK(node_lane_max_fps(kNodeLanePixelsAt60Fps + 1) < 60.0);
}

TEST_CASE("node firmware: the DHCP fallback is a link-local address from the MAC") {
    std::uint8_t a[4];
    const std::uint8_t mac[6] = {0x02, 0x50, 0x12, 0x34, 0x56, 0x78};
    node_link_local_address(mac, a);
    CHECK(a[0] == 169);
    CHECK(a[1] == 254);
    CHECK(a[2] == 1 + 0x56 % 254);
    CHECK(a[3] == 0x78);
    // RFC 3927: the first and last /24 are reserved, whatever the MAC.
    for (const std::uint8_t b :
         {std::uint8_t{0}, std::uint8_t{253}, std::uint8_t{254}, std::uint8_t{255}}) {
        const std::uint8_t m[6] = {2, 0, 0, 0, b, 0};
        node_link_local_address(m, a);
        CHECK(a[2] >= 1);
        CHECK(a[2] <= 254);
    }
}

TEST_CASE("node setup: lines kept short enough for 60 fps when the eight allow it") {
    const std::vector<RigPreset> presets = builtin_rig_presets();
    const auto lotus = std::find_if(presets.begin(), presets.end(),
                                    [](const RigPreset &p) { return p.name == "Lotus"; });
    REQUIRE(lotus != presets.end());
    Project project = default_project();
    project.strips.clear();
    for (const StripSpec &s : lotus->strips) {
        project.strips.push_back({s, ColourOrder::grb});
    }
    const NodePlan plan = plan_node(project);
    CHECK(plan.pixels == 1328); // Joints out where petals cross and at their tips.
    REQUIRE(plan.lanes.size() == 3);
    CHECK(plan.lanes[0].pixels == 468);
    CHECK(plan.lanes[1].pixels == 480);
    CHECK(plan.lanes[2].pixels == 380);
    CHECK(plan.notes.empty()); // Every line at 60 fps or more.
    for (const NodeLane &lane : plan.lanes) {
        CHECK(node_lane_max_fps(lane.pixels) >= 60.0);
    }
}

TEST_CASE("node setup: each lane's pin and header position (port D, UM1974 table 13)") {
    CHECK(std::string(node_lane_pin(0)) == "PD0 (CN9 pin 25)");
    CHECK(std::string(node_lane_pin(2)) == "PD2 (CN8 pin 12)");
    CHECK(std::string(node_lane_pin(7)) == "PD7 (CN9 pin 2)");
    CHECK(std::string(node_lane_pin(8)).empty());
    CHECK(std::string(node_lane_pin(-1)).empty());
}

TEST_CASE("node setup: one strip per data line when the strips fit the eight") {
    Project project = default_project();
    project.strips.clear();
    for (int i = 0; i < 8; ++i) {
        StripSpec s;
        s.name = "Pillar " + std::to_string(i + 1);
        s.polyline = {{i * 1.0, 1.0, 0.0}, {i * 1.0, 2.0, 0.0}};
        s.count = 60;
        project.strips.push_back({s, ColourOrder::grb});
    }
    CHECK(plan_node(project).lanes.size() == 1); // Chained: 480 pixels fit one 60 fps line.
    project.node.one_strip_per_line = true;
    NodePlan plan = plan_node(project);
    REQUIRE(plan.lanes.size() == 8);
    for (size_t l = 0; l < 8; ++l) {
        CHECK(plan.lanes[l].strips == 1);
        CHECK(plan.lanes[l].first_strip == static_cast<int>(l));
        CHECK(plan.lanes[l].first_pixel == static_cast<int>(l) * 60);
        CHECK(plan.lanes[l].pixels == 60);
    }
    CHECK(plan.notes.empty());
    CHECK(plan.active_mask() == 0xFF);
    project.strips.push_back(project.strips.back()); // A ninth strip: chained, with a note.
    plan = plan_node(project);
    CHECK(plan.lanes.size() == 1);
    REQUIRE(plan.notes.size() == 1);
    CHECK(plan.notes[0].find("at most 8 strips") != std::string::npos);
    // Saved with the project.
    const Result<Project> back = parse_project_json(to_project_json(project).value());
    REQUIRE(back.ok());
    CHECK(back.value().node.one_strip_per_line);
}
