// Art-Net discovery and preflight checks.

#include "io/project.h"
#include "output/art_poll.h"
#include "output/discovery.h"
#include "output/preflight.h"
#include "output/udp_socket.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <thread>

using namespace psyight;

namespace {

bool has(const std::vector<PreflightCheck> &checks, const std::string &area, CheckLevel level,
         const std::string &fragment) {
    return std::any_of(checks.begin(), checks.end(), [&](const PreflightCheck &c) {
        return c.area == area && c.level == level &&
               c.message.find(fragment) != std::string::npos;
    });
}

struct Fixture {
    Project project = default_project();
    Rig rig;
    Patch patch;
    PreflightInputs in;

    std::vector<PreflightCheck> run() {
        rig = rig_of(project);
        patch = patch_of(project);
        in.project = &project;
        in.rig = &rig;
        in.patch = &patch;
        return run_preflight(in);
    }
};

} // namespace

TEST_CASE("ArtPoll and ArtPollReply: layouts and a round trip") {
    std::vector<std::uint8_t> p;
    REQUIRE(build_art_poll(p) == 14);
    CHECK(std::string(reinterpret_cast<const char *>(p.data())) == "Art-Net");
    CHECK(p[8] == 0x00);
    CHECK(p[9] == 0x20); // OpPoll 0x2000, little-endian.
    CHECK(p[11] == 14);
    CHECK(is_art_poll(p));

    ArtNode node;
    node.ip = {2, 0, 0, 10};
    node.short_name = "F767 node";
    node.long_name = "Psyight STM32F767 pixel node";
    node.report = "#0001 [0001] Power on tests successful";
    node.output_universes = {0x0123 + 1, 0x0124 + 1, 0x0125 + 1};
    node.bind_index = 1;
    REQUIRE(build_art_poll_reply(node, p) == 239);
    CHECK(p[8] == 0x00);
    CHECK(p[9] == 0x21); // OpPollReply.
    CHECK(p[10] == 2);
    CHECK(p[13] == 10);
    CHECK(p[14] == 0x36); // Port 6454, little-endian.
    CHECK(p[15] == 0x19);
    CHECK(p[18] == 0x01); // Net.
    CHECK(p[19] == 0x02); // Sub-Net.
    CHECK(p[173] == 3);   // NumPorts low byte.
    CHECK(p[174] == 0x80);
    CHECK(p[190] == 0x03); // SwOut: low nibbles.
    CHECK(p[192] == 0x05);
    const auto back = parse_art_poll_reply(p);
    REQUIRE(back.has_value());
    CHECK(back->ip == node.ip);
    CHECK(back->short_name == node.short_name);
    CHECK(back->long_name == node.long_name);
    CHECK(back->report == node.report);
    CHECK(back->output_universes == node.output_universes);
    CHECK(back->bind_index == 1);
    CHECK_FALSE(is_art_poll(p));
    // Ports across Sub-Nets cannot share one reply.
    node.output_universes = {16, 17};
    CHECK(build_art_poll_reply(node, p) == 0);
}

TEST_CASE("discovery: a poll over loopback collects a fake node's replies once each") {
    auto controller = open_udp_socket({127, 0, 0, 1}, 0);
    auto fake = open_udp_socket({127, 0, 0, 1}, 0);
    REQUIRE(controller.ok());
    REQUIRE(fake.ok());
    std::thread node_thread([&fake] {
        std::vector<std::uint8_t> buffer(1500);
        Ipv4Endpoint from;
        const Result<size_t> got = fake.value()->receive(buffer, 2000, &from);
        if (!got.ok() || got.value() == 0 || !is_art_poll({buffer.data(), got.value()})) {
            return;
        }
        ArtNode a;
        a.ip = {127, 0, 0, 1};
        a.short_name = "fake";
        a.output_universes = {1, 2, 3, 4};
        ArtNode b = a;
        b.output_universes = {5};
        b.bind_index = 2;
        std::vector<std::uint8_t> reply;
        build_art_poll_reply(a, reply);
        fake.value()->send_to(reply, from);
        fake.value()->send_to(reply, from); // A duplicate.
        build_art_poll_reply(b, reply);
        fake.value()->send_to(reply, from);
    });
    const Result<std::vector<ArtNode>> nodes = discover_art_nodes(
        *controller.value(), {{127, 0, 0, 1}, fake.value()->local_port()}, 300);
    node_thread.join();
    REQUIRE(nodes.ok());
    REQUIRE(nodes.value().size() == 2);
    CHECK(nodes.value()[0].output_universes == std::vector<std::uint16_t>{1, 2, 3, 4});
    CHECK(nodes.value()[1].output_universes == std::vector<std::uint16_t>{5});
}

TEST_CASE("network interfaces: subnets and broadcast addresses") {
    NetworkInterface i;
    i.address = {192, 168, 1, 20};
    i.mask = {255, 255, 255, 0};
    CHECK(i.broadcast() == std::array<std::uint8_t, 4>{192, 168, 1, 255});
    CHECK(i.contains({192, 168, 1, 77}));
    CHECK_FALSE(i.contains({192, 168, 2, 77}));
    CHECK(ipv4_text({10, 0, 0, 1}) == "10.0.0.1");
    for (const NetworkInterface &n : list_ipv4_interfaces()) {
        CHECK(n.address[0] != 127); // Loopback is left out.
    }
}

TEST_CASE("preflight: the default project before a show") {
    Fixture f;
    const auto checks = f.run();
    CHECK(has(checks, "Audio", CheckLevel::warn, "No audio device"));
    CHECK(has(checks, "Analysis", CheckLevel::warn, "No track loaded"));
    CHECK(has(checks, "Rig", CheckLevel::ok, "444 pixels in 4 strips"));
    CHECK(has(checks, "Patch", CheckLevel::ok, "Universes 1 to 3, 170 pixels each"));
    CHECK(has(checks, "Power", CheckLevel::ok, "(scaled down)"));
    CHECK(has(checks, "Limiter", CheckLevel::ok, "Flash limiter on"));
    CHECK(has(checks, "Timing", CheckLevel::warn, "Latency offset is 0 ms"));
    CHECK(has(checks, "Network", CheckLevel::info, "Sending is off"));
    CHECK(has(checks, "Network", CheckLevel::info, "IGMP"));
    CHECK(has(checks, "Nodes", CheckLevel::info, "has not run"));
    CHECK_FALSE(std::any_of(checks.begin(), checks.end(),
                            [](const PreflightCheck &c) { return c.level == CheckLevel::fail; }));
}

TEST_CASE("preflight: failures and warnings that matter") {
    Fixture f;
    f.project.limiter.flash_limiter = false;
    f.project.output.sync_universe = 2;
    f.project.limiter.budget_a = 0.0;
    f.project.strips[0].spec.count = 200; // The bottom polyline holds 150.
    f.in.track_loaded = true;             // But no analysis: no tempo.
    f.in.device_sample_rate_hz = 44100;
    f.in.device_period_frames = 441;
    auto checks = f.run();
    CHECK(has(checks, "Limiter", CheckLevel::fail, "Flash limiter off"));
    CHECK(has(checks, "Patch", CheckLevel::fail, "sync universe 2"));
    CHECK(has(checks, "Power", CheckLevel::warn, "Power budget off"));
    CHECK(has(checks, "Rig", CheckLevel::warn, "bottom: 200 pixels run 0.83 m past"));
    CHECK(has(checks, "Analysis", CheckLevel::fail, "No tempo"));
    CHECK(has(checks, "Audio", CheckLevel::warn, "44100 Hz, period 441 frames (10.0 ms)"));

    Fixture art;
    art.project.output.protocol = OutputProtocol::art_net;
    art.project.output.address = {192, 168, 1, 50};
    NetworkInterface lan;
    lan.name = "Ethernet";
    lan.address = {192, 168, 1, 20};
    lan.mask = {255, 255, 255, 0};
    art.in.interfaces = {lan};
    art.in.nodes = std::vector<ArtNode>{};
    checks = art.run();
    CHECK(has(checks, "Network", CheckLevel::ok, "192.168.1.50 is on Ethernet (192.168.1.20)"));
    CHECK(has(checks, "Nodes", CheckLevel::warn, "No Art-Net node answered"));
    ArtNode node;
    node.ip = {192, 168, 1, 50};
    node.short_name = "F767";
    node.output_universes = {1, 2};
    art.in.nodes = std::vector<ArtNode>{node};
    art.project.output.address = {10, 0, 0, 5};
    checks = art.run();
    CHECK(has(checks, "Nodes", CheckLevel::ok, "F767 at 192.168.1.50: outputs on universes 1 to 2"));
    CHECK(has(checks, "Nodes", CheckLevel::warn, "universes 3"));
    CHECK(has(checks, "Network", CheckLevel::warn, "10.0.0.5 is not on any local subnet"));
}
