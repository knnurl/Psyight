// Show: sender and virtual node round trips, the renderer, the UDP socket and the show thread.

#include "engine/rig.h"
#include "output/packets.h"
#include "output/patch.h"
#include "output/sender.h"
#include "output/show.h"
#include "output/udp_socket.h"
#include "output/virtual_node.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace psyight;

namespace {

// Passes packets on to a node, dropping those from drop_from on, and records their destinations.
struct RecordingSink final : PacketSink {
    explicit RecordingSink(VirtualNode &n) : node(n) {}
    void send(std::span<const std::uint8_t> packet, const Ipv4Endpoint &to) override {
        destinations.push_back(to);
        if (drop_from >= 0 && static_cast<int>(destinations.size()) > drop_from) {
            return;
        }
        node.send(packet, to);
    }
    VirtualNode &node;
    std::vector<Ipv4Endpoint> destinations;
    int drop_from = -1;
};

std::vector<std::uint8_t> frame_bytes(int frame) {
    std::vector<std::uint8_t> v(3 * 444);
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::uint8_t>(i * 13 + static_cast<size_t>(frame) * 101);
    }
    return v;
}

std::vector<std::uint8_t> ramp(size_t n) {
    std::vector<std::uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    return v;
}

} // namespace

TEST_CASE("sender and virtual node: frames round trip in both protocols, latched on sync") {
    const Patch patch = build_patch(default_rig(), 1);
    for (const OutputProtocol protocol : {OutputProtocol::sacn, OutputProtocol::art_net}) {
        CAPTURE(static_cast<int>(protocol));
        OutputSettings settings;
        settings.protocol = protocol;
        settings.cid = cid_from_name("test");
        VirtualNode node;
        node.set_patch(patch);
        OutputSender sender;
        for (int f = 0; f < 300; ++f) { // Past both sequence wraps.
            RecordingSink sink(node);
            CHECK(sender.send_frame(settings, patch_frame(patch, frame_bytes(f)), sink) == 4);
            REQUIRE(node.rgb() == frame_bytes(f));
        }
        CHECK(node.stats().packets == 1200);
        CHECK(node.stats().bad_packets == 0);
        CHECK(node.stats().sequence_gaps == 0);

        // Without the sync packet the node keeps showing the previous frame.
        RecordingSink no_sync(node);
        no_sync.drop_from = 3;
        sender.send_frame(settings, patch_frame(patch, frame_bytes(300)), no_sync);
        CHECK(node.rgb() == frame_bytes(299));
        // A frame lost entirely shows up as a sequence gap on every universe in the next.
        RecordingSink lost(node);
        lost.drop_from = 0;
        sender.send_frame(settings, patch_frame(patch, frame_bytes(301)), lost);
        RecordingSink fine(node);
        sender.send_frame(settings, patch_frame(patch, frame_bytes(302)), fine);
        CHECK(node.stats().sequence_gaps == 3);
        CHECK(node.rgb() == frame_bytes(302));
    }
}

TEST_CASE("sender: multicast, unicast and Art-Net destinations") {
    const Patch patch = build_patch(default_rig(), 0x0102);
    OutputSettings settings;
    settings.sync_universe = 999;
    VirtualNode node;
    RecordingSink sink(node);
    OutputSender sender;
    sender.send_frame(settings, patch_frame(patch, frame_bytes(0)), sink);
    REQUIRE(sink.destinations.size() == 4);
    CHECK(sink.destinations[0].address == std::array<std::uint8_t, 4>{239, 255, 1, 2});
    CHECK(sink.destinations[2].address == std::array<std::uint8_t, 4>{239, 255, 1, 4});
    CHECK(sink.destinations[3].address == std::array<std::uint8_t, 4>{239, 255, 3, 0xe7});
    CHECK(sink.destinations[0].port == 5568);
    settings.multicast = false;
    settings.address = {192, 168, 1, 50};
    settings.sync = false;
    RecordingSink unicast(node);
    CHECK(sender.send_frame(settings, patch_frame(patch, frame_bytes(0)), unicast) == 3);
    CHECK(unicast.destinations[1].address == std::array<std::uint8_t, 4>{192, 168, 1, 50});
    settings.protocol = OutputProtocol::art_net;
    RecordingSink art(node);
    sender.send_frame(settings, patch_frame(patch, frame_bytes(0)), art);
    CHECK(art.destinations[0].port == 6454);
    CHECK(cid_from_name("a") != cid_from_name("b"));
    CHECK(cid_from_name("a") == cid_from_name("a"));
}

TEST_CASE("show renderer: deterministic, and the node shows the limited frame's bytes") {
    auto analysis = std::make_shared<TrackAnalysis>();
    analysis->sample_rate_hz = 48000;
    analysis->duration_s = 20.0;
    analysis->grid = {150.0, seconds_to_samples(1.0, 48000), 48000};
    for (int k = 0; k < 40; ++k) {
        analysis->bass.kick_times_s.push_back(1.0 + 0.4 * k);
    }
    analysis->sections = {{0.0, 20.0, "groove", 1.0}};
    ShowConfig config;
    config.analysis = analysis;
    config.grid = analysis->grid;
    ShowRenderer a;
    ShowRenderer b;
    bool lit = false;
    for (int f = 0; f < 120; ++f) {
        const double t_s = 1.0 + f / 60.0;
        const ShowFrame fa = a.render(config, t_s);
        const ShowFrame fb = b.render(config, t_s);
        REQUIRE(fa.node_rgb == fb.node_rgb);
        REQUIRE(fa.node_rgb == to_bytes(fa.frame, config.limiter.gamma));
        CHECK(fa.packets_sent == 4);
        CHECK(fa.section == "groove");
        lit |= std::any_of(fa.node_rgb.begin(), fa.node_rgb.end(), [](auto v) { return v > 0; });
    }
    CHECK(lit);
    config.analysis = nullptr; // No analysis: black.
    ++config.version;
    const ShowFrame black = a.render(config, 3.0);
    CHECK(std::all_of(black.node_rgb.begin(), black.node_rgb.end(), [](auto v) { return v == 0; }));
}

TEST_CASE("show renderer: blackout sends black, hold freezes the look") {
    auto analysis = std::make_shared<TrackAnalysis>();
    analysis->sample_rate_hz = 48000;
    analysis->duration_s = 20.0;
    analysis->grid = {150.0, seconds_to_samples(1.0, 48000), 48000};
    for (int k = 0; k < 40; ++k) {
        analysis->bass.kick_times_s.push_back(1.0 + 0.4 * k);
    }
    analysis->sections = {{0.0, 20.0, "groove", 1.0}};
    ShowConfig config;
    config.analysis = analysis;
    config.grid = analysis->grid;
    ShowRenderer r;
    const auto lit = [](const ShowFrame &f) {
        return std::any_of(f.node_rgb.begin(), f.node_rgb.end(), [](auto v) { return v > 0; });
    };
    CHECK(lit(r.render(config, 1.05)));
    config.blackout = true;
    ++config.version;
    for (int f = 0; f < 10; ++f) {
        const ShowFrame black = r.render(config, 1.1 + f / 60.0);
        CHECK_FALSE(lit(black));
        CHECK(black.packets_sent == 4); // Still sent: nodes get black, not silence.
    }
    config.blackout = false;
    ++config.version;
    const ShowFrame before = r.render(config, 1.3);
    config.hold = true;
    ++config.version;
    for (int f = 1; f < 30; ++f) {
        const ShowFrame held = r.render(config, 1.3 + f / 60.0); // Kicks pass meanwhile.
        CHECK(held.node_rgb == before.node_rgb);
        CHECK(held.index > before.index);
    }
    config.hold = false;
    ++config.version;
    CHECK(r.render(config, 1.85).node_rgb != before.node_rgb);
}

TEST_CASE("UDP socket: an Art-Net packet over loopback, and IPv4 parsing") {
    CHECK(parse_ipv4("192.168.1.50") == std::array<std::uint8_t, 4>{192, 168, 1, 50});
    CHECK_FALSE(parse_ipv4("192.168.1").has_value());
    CHECK_FALSE(parse_ipv4("192.168.1.256").has_value());
    CHECK_FALSE(parse_ipv4("1.2.3.4x").has_value());
    auto receiver = open_udp_socket({127, 0, 0, 1}, 0);
    REQUIRE(receiver.ok());
    auto sender = open_udp_socket();
    REQUIRE(sender.ok());
    std::vector<std::uint8_t> packet;
    build_art_dmx(0, 9, ramp(510), packet);
    const Ipv4Endpoint to{{127, 0, 0, 1}, receiver.value()->local_port()};
    REQUIRE(to.port != 0);
    REQUIRE(sender.value()->send_to(packet, to).ok());
    std::vector<std::uint8_t> buffer(1500);
    const Result<size_t> got = receiver.value()->receive(buffer, 1000);
    REQUIRE(got.ok());
    REQUIRE(got.value() == packet.size());
    buffer.resize(got.value());
    const auto parsed = parse_art_net(buffer);
    REQUIRE(parsed.has_value());
    CHECK(parsed->universe == 1);
    CHECK(parsed->slots == ramp(510));
    buffer.resize(1500);
    const Result<size_t> none = receiver.value()->receive(buffer, 10);
    REQUIRE(none.ok());
    CHECK(none.value() == 0);
}

TEST_CASE("show thread: renders at the configured rate from the position source") {
    std::atomic<SampleIndex> position{48000};
    ShowThread thread([&position] { return position.load(); }, 60.0);
    CHECK(thread.latest() == nullptr); // No config yet.
    thread.set_config(std::make_shared<ShowConfig>());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto first = thread.latest();
    REQUIRE(first != nullptr);
    CHECK(first->t_s == doctest::Approx(1.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto later = thread.latest();
    CHECK(later->index - first->index >= 25); // About 30 at 60 Hz; loose for a busy machine.
    CHECK(later->index - first->index <= 35);
}

// Tool (skipped by default): frame interval and render cost of the show thread on the default
// rig, 10 s at 60 Hz. Run: psyight_tests -tc="show thread: timing" --no-skip
TEST_CASE("show thread: timing" * doctest::skip()) {
    auto analysis = std::make_shared<TrackAnalysis>();
    analysis->sample_rate_hz = 48000;
    analysis->duration_s = 60.0;
    analysis->grid = {150.0, 0, 48000};
    for (int k = 0; k < 150; ++k) {
        analysis->bass.kick_times_s.push_back(0.4 * k);
    }
    analysis->sections = {{0.0, 60.0, "groove", 1.0}};
    auto config = std::make_shared<ShowConfig>();
    config->analysis = analysis;
    config->grid = analysis->grid;
    const auto started = std::chrono::steady_clock::now();
    ShowThread thread(
        [started] {
            const double s =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            return static_cast<SampleIndex>(s * 48000.0);
        },
        60.0);
    thread.set_config(config);
    std::vector<double> intervals_ms;
    std::vector<double> render_ms;
    std::int64_t last_index = -1;
    auto last_seen = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(10)) {
        const auto frame = thread.latest();
        if (frame != nullptr && frame->index != last_index) {
            const auto now = std::chrono::steady_clock::now();
            if (last_index >= 0) {
                intervals_ms.push_back(
                    std::chrono::duration<double, std::milli>(now - last_seen).count() /
                    static_cast<double>(frame->index - last_index));
                render_ms.push_back(frame->render_ms);
            }
            last_index = frame->index;
            last_seen = now;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    std::sort(intervals_ms.begin(), intervals_ms.end());
    std::sort(render_ms.begin(), render_ms.end());
    const auto q = [](const std::vector<double> &v, double p) {
        return v[static_cast<size_t>(p * static_cast<double>(v.size() - 1))];
    };
    MESSAGE("frames " << last_index + 1 << " in 10 s; interval ms p5 " << q(intervals_ms, 0.05)
                      << " p50 " << q(intervals_ms, 0.5) << " p95 " << q(intervals_ms, 0.95)
                      << "; render ms p50 " << q(render_ms, 0.5) << " p99 " << q(render_ms, 0.99)
                      << " max " << render_ms.back() << "; late frames "
                      << thread.latest()->late_frames);
}
