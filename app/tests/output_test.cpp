// Output: patch, and sACN and Art-Net packets against their byte layouts and a round trip.

#include "engine/rig.h"
#include "output/packets.h"
#include "output/patch.h"
#include "output/sender.h"
#include "output/virtual_node.h"

#include <doctest/doctest.h>

#include <numeric>
#include <optional>
#include <vector>

using namespace psyight;

namespace {

std::vector<std::uint8_t> ramp(size_t n) {
    std::vector<std::uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    return v;
}

std::uint16_t be16(const std::vector<std::uint8_t> &p, size_t at) {
    return static_cast<std::uint16_t>((p[at] << 8) | p[at + 1]);
}

} // namespace

TEST_CASE("patch: 170 pixels per universe, never split, colour order applied") {
    const Rig rig = default_rig(); // 444 pixels.
    const Patch patch = build_patch(rig, 5);
    REQUIRE(patch.pixels.size() == 444);
    CHECK(patch.universes == std::vector<int>{5, 6, 7});
    CHECK(patch.pixels[169].universe_slot == 0);
    CHECK(patch.pixels[169].channel == 507);
    CHECK(patch.pixels[170].universe_slot == 1);
    CHECK(patch.pixels[170].channel == 0);

    std::vector<std::uint8_t> rgb(3 * 444, 0);
    rgb[0] = 10; // Pixel 0: R 10, G 20, B 30.
    rgb[1] = 20;
    rgb[2] = 30;
    rgb[3 * 443 + 0] = 1; // Last pixel: R 1, G 2, B 3.
    rgb[3 * 443 + 1] = 2;
    rgb[3 * 443 + 2] = 3;
    const std::vector<UniverseData> grb = patch_frame(patch, rgb);
    REQUIRE(grb.size() == 3);
    CHECK(grb[0].channels.size() == 510);
    CHECK(grb[1].channels.size() == 510);
    CHECK(grb[2].channels.size() == 3 * (444 - 340));
    CHECK(grb[0].channels[0] == 20); // GRB: G first.
    CHECK(grb[0].channels[1] == 10);
    CHECK(grb[0].channels[2] == 30);
    CHECK(grb[2].channels[309] == 2);
    CHECK(grb[2].channels[310] == 1);
    CHECK(grb[2].channels[311] == 3);

    const Patch rgb_patch = build_patch(rig, 1, 170, std::vector<ColourOrder>(8, ColourOrder::bgr));
    const std::vector<UniverseData> bgr = patch_frame(rgb_patch, rgb);
    CHECK(bgr[0].universe == 1);
    CHECK(bgr[0].channels[0] == 30);
    CHECK(bgr[0].channels[2] == 10);
}

TEST_CASE("sACN: a full data packet matches the E1.31 layout") {
    SacnSource source;
    std::iota(source.cid.begin(), source.cid.end(), std::uint8_t{1});
    source.sync_universe = 7;
    const std::vector<std::uint8_t> slots = ramp(512);
    std::vector<std::uint8_t> p;
    REQUIRE(build_sacn_data(source, 0x0102, 42, slots, p) == 638);
    CHECK(be16(p, 0) == 0x0010);
    CHECK(be16(p, 2) == 0x0000);
    CHECK(std::string(reinterpret_cast<const char *>(&p[4]), 9) == "ASC-E1.17");
    CHECK(p[13] == 0);
    CHECK(be16(p, 16) == 0x726e); // Flags 0x7, length 622.
    CHECK(be16(p, 18) == 0x0000);
    CHECK(be16(p, 20) == 0x0004);
    CHECK(p[22] == 1);
    CHECK(p[37] == 16);
    CHECK(be16(p, 38) == 0x7258); // Length 600.
    CHECK(be16(p, 42) == 0x0002);
    CHECK(std::string(reinterpret_cast<const char *>(&p[44])) == "Psyight");
    CHECK(p[107] == 0);
    CHECK(p[108] == 100);
    CHECK(be16(p, 109) == 7);
    CHECK(p[111] == 42);
    CHECK(p[112] == 0);
    CHECK(be16(p, 113) == 0x0102);
    CHECK(be16(p, 115) == 0x720b); // Length 523.
    CHECK(p[117] == 0x02);
    CHECK(p[118] == 0xa1);
    CHECK(be16(p, 119) == 0x0000);
    CHECK(be16(p, 121) == 0x0001);
    CHECK(be16(p, 123) == 513);
    CHECK(p[125] == 0);
    CHECK(std::equal(slots.begin(), slots.end(), p.begin() + 126));
    CHECK(sacn_multicast_group(0x0102) == std::array<std::uint8_t, 4>{239, 255, 1, 2});
}

TEST_CASE("sACN: sync packet layout, and out-of-range arguments are refused") {
    Cid cid{};
    cid[0] = 0xab;
    std::vector<std::uint8_t> p;
    REQUIRE(build_sacn_sync(cid, 7, 9, p) == 49);
    CHECK(be16(p, 16) == 0x7021); // Length 33.
    CHECK(be16(p, 20) == 0x0008);
    CHECK(p[22] == 0xab);
    CHECK(be16(p, 38) == 0x700b); // Length 11.
    CHECK(be16(p, 42) == 0x0001);
    CHECK(p[44] == 9);
    CHECK(be16(p, 45) == 7);
    CHECK(be16(p, 47) == 0);

    SacnSource source;
    CHECK(build_sacn_data(source, 0, 0, ramp(3), p) == 0);
    CHECK(build_sacn_data(source, 64000, 0, ramp(3), p) == 0);
    CHECK(build_sacn_data(source, 1, 0, ramp(513), p) == 0);
    CHECK(build_sacn_sync(cid, 0, 0, p) == 0);
}

TEST_CASE("Art-Net: ArtDmx and ArtSync layouts") {
    std::vector<std::uint8_t> p;
    REQUIRE(build_art_dmx(0x0123, 5, ramp(3), p) == 18 + 4); // Padded to even.
    CHECK(std::string(reinterpret_cast<const char *>(p.data())) == "Art-Net");
    CHECK(p[8] == 0x00); // OpDmx 0x5000, little-endian.
    CHECK(p[9] == 0x50);
    CHECK(p[10] == 0);
    CHECK(p[11] == 14);
    CHECK(p[12] == 5);
    CHECK(p[13] == 0);
    CHECK(p[14] == 0x23); // SubUni.
    CHECK(p[15] == 0x01); // Net.
    CHECK(be16(p, 16) == 4);
    CHECK(p[18] == 3);
    CHECK(p[21] == 0);
    CHECK(build_art_dmx(0x8000, 0, ramp(2), p) == 0);
    REQUIRE(build_art_sync(p) == 14);
    CHECK(p[8] == 0x00);
    CHECK(p[9] == 0x52);
    CHECK(p[13] == 0);
}

TEST_CASE("output packets: build then parse returns what went in, and corrupt packets fail") {
    SacnSource source;
    source.priority = 150;
    source.sync_universe = 9;
    const std::vector<std::uint8_t> slots = ramp(333);
    std::vector<std::uint8_t> p;
    build_sacn_data(source, 300, 200, slots, p);
    auto parsed = parse_sacn(p);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == ParsedPacket::Kind::dmx);
    CHECK(parsed->universe == 300);
    CHECK(parsed->sequence == 200);
    CHECK(parsed->priority == 150);
    CHECK(parsed->sync_universe == 9);
    CHECK(parsed->slots == slots);
    CHECK_FALSE(parse_art_net(p).has_value());
    p[125] = 0xdd; // Non-zero start code.
    CHECK_FALSE(parse_sacn(p).has_value());
    p.pop_back(); // Truncated.
    CHECK_FALSE(parse_sacn(p).has_value());

    build_sacn_sync(source.cid, 9, 4, p);
    parsed = parse_sacn(p);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == ParsedPacket::Kind::sync);
    CHECK(parsed->sync_universe == 9);
    CHECK(parsed->sequence == 4);

    build_art_dmx(299, 17, slots, p); // Port address 299 is sACN universe 300.
    parsed = parse_art_net(p);
    REQUIRE(parsed.has_value());
    CHECK(parsed->universe == 300);
    CHECK(parsed->sequence == 17);
    REQUIRE(parsed->slots.size() == 334); // Padded.
    CHECK(std::equal(slots.begin(), slots.end(), parsed->slots.begin()));
    CHECK_FALSE(parse_sacn(p).has_value());
    p[3] = 'x';
    CHECK_FALSE(parse_art_net(p).has_value());

    build_art_sync(p);
    parsed = parse_art_net(p);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == ParsedPacket::Kind::sync);
}

TEST_CASE("DDP: a frame in packets of 480 pixels, pushed on the last; the virtual node shows it") {
    std::vector<std::uint8_t> packet;
    const std::vector<std::uint8_t> data = {1, 2, 3, 4, 5, 6};
    REQUIRE(build_ddp(7, 1440, data, true, packet) == 16);
    CHECK(packet[0] == 0x41); // Version 1, push.
    CHECK(packet[1] == 7);
    CHECK(packet[3] == 1);
    CHECK(packet[6] == 0x05); // 1440 = 0x5A0.
    CHECK(packet[7] == 0xA0);
    CHECK(packet[9] == 6);
    const std::optional<ParsedPacket> back = parse_ddp(packet);
    REQUIRE(back);
    CHECK(back->kind == ParsedPacket::Kind::ddp);
    CHECK(back->offset == 1440);
    CHECK(back->push);
    CHECK(back->slots == data);
    CHECK_FALSE(parse_ddp(std::span(packet).first(12))); // Shorter than it says.
    std::vector<std::uint8_t> big(kDdpMaxData + 1);
    CHECK(build_ddp(1, 0, big, true, packet) == 0);

    // Three full universes (1530 bytes): two packets, the second pushing.
    struct Capture final : PacketSink {
        std::vector<std::vector<std::uint8_t>> packets;
        void send(std::span<const std::uint8_t> p, const Ipv4Endpoint &to) override {
            CHECK(to.port == kDdpPort);
            packets.emplace_back(p.begin(), p.end());
        }
    } sink;
    OutputSettings settings;
    settings.protocol = OutputProtocol::ddp;
    settings.address = {192, 168, 0, 220};
    std::vector<UniverseData> universes;
    for (int u = 0; u < 3; ++u) {
        universes.push_back(
            {1 + u, std::vector<std::uint8_t>(510, static_cast<std::uint8_t>(u + 1))});
    }
    OutputSender sender;
    CHECK(sender.send_frame(settings, universes, sink) == 2);
    REQUIRE(sink.packets.size() == 2);
    const auto first = parse_ddp(sink.packets[0]);
    const auto second = parse_ddp(sink.packets[1]);
    REQUIRE(first);
    REQUIRE(second);
    CHECK_FALSE(first->push);
    CHECK(second->push);
    CHECK(first->slots.size() == 1440);
    CHECK(second->offset == 1440);
    CHECK(second->slots.size() == 90);
    CHECK(first->slots[510] == 2); // Universe 2 starts at 510.
    CHECK(second->sequence == first->sequence % 15 + 1);

    // The virtual node puts a DDP frame back on the rig.
    const Rig rig = default_rig();
    const Patch patch = build_patch(rig);
    std::vector<std::uint8_t> rgb(3 * rig.pixels.size());
    for (size_t i = 0; i < rgb.size(); ++i) {
        rgb[i] = static_cast<std::uint8_t>(i * 13 + 5);
    }
    VirtualNode node;
    node.set_patch(patch);
    sender.send_frame(settings, patch_frame(patch, rgb), node);
    CHECK(node.rgb() == rgb);
    CHECK(node.stats().bad_packets == 0);
}
