// Follow mode (docs/PLAN_FOLLOW_MODE.md): the packet log, the Pro DJ Link and TCNet decoders.
// Decoders are checked against packets built byte by byte at the documented offsets, not only
// against the encoders, so a change of layout shows here.

#include "link/decks.h"
#include "link/packet.h"
#include "link/prodjlink.h"
#include "link/tcnet.h"

#include <doctest/doctest.h>

#include <variant>
#include <vector>

using namespace psyight;

namespace {

std::vector<std::uint8_t> blank(size_t size, std::uint8_t type) {
    std::vector<std::uint8_t> b(size, 0);
    const char magic[] = "Qspt1WmJOL";
    for (size_t i = 0; i < 10; ++i) {
        b[i] = static_cast<std::uint8_t>(magic[i]);
    }
    b[0x0a] = type;
    return b;
}

void name_at(std::vector<std::uint8_t> &b, size_t at, const char *name) {
    for (size_t i = 0; name[i] != 0; ++i) {
        b[at + i] = static_cast<std::uint8_t>(name[i]);
    }
}

} // namespace

TEST_CASE("link log: lines as djlink_log.py writes them, round trip") {
    const char *line = R"({"t_ns": 15344529414200, "utc": "2026-11-10T22:49:15.882+00:00", )"
                       R"("port": 50001, "src": "169.254.12.3", "hex": "5173707431576d4a4f4c28"})";
    const Result<LinkPacket> p = parse_link_packet_line(line);
    REQUIRE(p.ok());
    CHECK(p.value().t_ns == 15344529414200);
    CHECK(p.value().port == 50001);
    CHECK(p.value().src == "169.254.12.3");
    REQUIRE(p.value().bytes.size() == 11);
    CHECK(p.value().bytes[10] == 0x28);
    const Result<LinkPacket> again = parse_link_packet_line(link_packet_line(p.value()));
    REQUIRE(again.ok());
    CHECK(again.value() == p.value());
    const Result<std::vector<LinkPacket>> log =
        parse_link_log(link_packet_line(p.value()) + "\n" + link_packet_line(p.value()));
    REQUIRE(log.ok());
    CHECK(log.value().size() == 2);
    CHECK_FALSE(parse_link_log("{\"t_ns\": 1}\n").ok());
    CHECK_FALSE(parse_link_packet_line(R"({"t_ns":1,"port":1,"hex":"abc"})").ok());
}

TEST_CASE("pro dj link: decoders read the documented offsets") {
    using namespace prodjlink;
    // Beat: 128.00 BPM, +6% pitch, beat 3 of the bar, from player 2.
    std::vector<std::uint8_t> beat = blank(0x60, 0x28);
    name_at(beat, 0x0b, "CDJ-3000");
    beat[0x21] = 2;
    beat[0x26] = 0x01; // Next beat in 0x1f4 = 500 ms.
    beat[0x27] = 0xf4;
    const std::uint32_t pitch = 0x100000 + 0x100000 * 6 / 100;
    beat[0x54] = static_cast<std::uint8_t>(pitch >> 24);
    beat[0x55] = static_cast<std::uint8_t>(pitch >> 16);
    beat[0x56] = static_cast<std::uint8_t>(pitch >> 8);
    beat[0x57] = static_cast<std::uint8_t>(pitch);
    beat[0x5a] = 12800 >> 8;
    beat[0x5b] = 12800 & 0xff;
    beat[0x5c] = 3;
    const auto b = decode(kBeatPort, beat);
    REQUIRE(b.has_value());
    const Beat &m = std::get<Beat>(*b);
    CHECK(m.name == "CDJ-3000");
    CHECK(m.device == 2);
    CHECK(m.bpm == doctest::Approx(128.0));
    CHECK(m.pitch == doctest::Approx(0.06).epsilon(1e-5));
    CHECK(m.beat_in_bar == 3);
    CHECK(m.next_beat_ms == 500);

    // On-air from the mixer (device 33): channels 1 and 3.
    std::vector<std::uint8_t> air = blank(0x2d, 0x03);
    name_at(air, 0x0b, "DJM-A9");
    air[0x21] = 33;
    air[0x24] = 1;
    air[0x26] = 1;
    const auto a = decode(kBeatPort, air);
    REQUIRE(a.has_value());
    const OnAir &o = std::get<OnAir>(*a);
    CHECK(o.device == 33);
    CHECK(o.channels == 4);
    CHECK(o.on_air[0]);
    CHECK_FALSE(o.on_air[1]);
    CHECK(o.on_air[2]);

    // CDJ-3000 absolute position: a 7 min track at 61.5 s, -2.5%, 145.0 BPM.
    std::vector<std::uint8_t> pos = blank(0x3c, 0x0b);
    pos[0x21] = 3;
    pos[0x27] = 420 & 0xff; // 420 s.
    pos[0x26] = 420 >> 8;
    pos[0x29] = (61500 >> 16) & 0xff;
    pos[0x2a] = (61500 >> 8) & 0xff;
    pos[0x2b] = 61500 & 0xff;
    const auto minus = static_cast<std::uint32_t>(std::int32_t{-250});
    pos[0x2c] = static_cast<std::uint8_t>(minus >> 24);
    pos[0x2d] = static_cast<std::uint8_t>(minus >> 16);
    pos[0x2e] = static_cast<std::uint8_t>(minus >> 8);
    pos[0x2f] = static_cast<std::uint8_t>(minus);
    pos[0x3a] = 1450 >> 8;
    pos[0x3b] = 1450 & 0xff;
    const auto p = decode(kBeatPort, pos);
    REQUIRE(p.has_value());
    const Position &q = std::get<Position>(*p);
    CHECK(q.device == 3);
    CHECK(q.track_length_s == 420.0);
    CHECK(q.playhead_ms == 61500.0);
    CHECK(q.pitch == doctest::Approx(-0.025));
    CHECK(q.bpm == doctest::Approx(145.0));

    // Keep-alive on 50000: name, number, address.
    std::vector<std::uint8_t> keep = blank(0x36, 0x06);
    name_at(keep, 0x0c, "CDJ-3000");
    keep[0x24] = 4;
    keep[0x2c] = 169;
    keep[0x2d] = 254;
    keep[0x2e] = 7;
    keep[0x2f] = 9;
    const auto k = decode(kAnnouncePort, keep);
    REQUIRE(k.has_value());
    CHECK(std::get<KeepAlive>(*k).device == 4);
    CHECK(std::get<KeepAlive>(*k).address == std::array<std::uint8_t, 4>{169, 254, 7, 9});

    // Not Pro DJ Link, too short, another type, another port: nothing.
    CHECK_FALSE(decode(kBeatPort, std::vector<std::uint8_t>(0x60, 0)).has_value());
    CHECK_FALSE(decode(kBeatPort, std::vector<std::uint8_t>(beat.begin(), beat.begin() + 0x40))
                    .has_value());
    CHECK_FALSE(decode(kBeatPort, blank(0x60, 0x77)).has_value());
    CHECK_FALSE(decode(kStatusPort, beat).has_value());
}

TEST_CASE("pro dj link: the encoders round-trip through the decoders") {
    using namespace prodjlink;
    Beat beat{"CDJ-3000", 1, 147.5, -0.031, 4, 403, 1210};
    const Beat b = std::get<Beat>(*decode(kBeatPort, encode(beat)));
    CHECK(b.bpm == doctest::Approx(147.5));
    CHECK(b.pitch == doctest::Approx(-0.031).epsilon(1e-5));
    CHECK(b.beat_in_bar == 4);
    CHECK(b.next_bar_ms == 1210);
    OnAir air{"DJM-V10", 33, 6, {false, true, false, false, false, true}};
    const OnAir a = std::get<OnAir>(*decode(kBeatPort, encode(air)));
    CHECK(a.channels == 6);
    CHECK(a.on_air == air.on_air);
    Position pos{"CDJ-3000", 2, 431.0, 200123.0, 0.0425, 151.3};
    const Position p = std::get<Position>(*decode(kBeatPort, encode(pos)));
    CHECK(p.playhead_ms == 200123.0);
    CHECK(p.pitch == doctest::Approx(0.0425));
    CHECK(p.bpm == doctest::Approx(151.3));
    pos.bpm = 0.0; // No grid.
    CHECK(std::get<Position>(*decode(kBeatPort, encode(pos))).bpm == 0.0);
    KeepAlive keep{"XDJ-XZ", 2, {192, 168, 1, 20}};
    const KeepAlive k = std::get<KeepAlive>(*decode(kAnnouncePort, encode(keep)));
    CHECK(k.name == "XDJ-XZ");
    CHECK(k.address == keep.address);
}

TEST_CASE("tcnet: the time packet's playheads, totals and states") {
    using namespace tcnet;
    std::vector<std::uint8_t> t(104, 0);
    t[0] = 0x34;
    t[2] = 3;
    t[3] = 5;
    t[4] = 'T';
    t[5] = 'C';
    t[6] = 'N';
    t[7] = 254;
    const char *name = "BRIDGE";
    for (size_t i = 0; name[i] != 0; ++i) {
        t[8 + i] = static_cast<std::uint8_t>(name[i]);
    }
    // Layer 2 at 75.25 s of 400 s, playing.
    const std::uint32_t at = 75250;
    const std::uint32_t total = 400000;
    for (size_t i = 0; i < 4; ++i) {
        t[28 + i] = static_cast<std::uint8_t>(at >> (8 * i));
        t[60 + i] = static_cast<std::uint8_t>(total >> (8 * i));
    }
    t[97] = 3;
    const std::optional<Time> d = decode_time(t);
    REQUIRE(d.has_value());
    CHECK(d->header.node_name == "BRIDGE");
    CHECK(d->header.version_major == 3);
    CHECK(d->playhead_ms[1] == 75250);
    CHECK(d->total_ms[1] == 400000);
    CHECK(d->has_states);
    CHECK(d->state[1] == LayerState::playing);
    CHECK(advancing(d->state[1]));
    CHECK_FALSE(advancing(d->state[0]));
    const std::optional<Time> again = decode_time(encode_time(*d));
    REQUIRE(again.has_value());
    CHECK(again->playhead_ms == d->playhead_ms);
    CHECK(again->state == d->state);
    // A short time packet: the playheads only.
    const std::optional<Time> short_one =
        decode_time(std::vector<std::uint8_t>(t.begin(), t.begin() + 60));
    REQUIRE(short_one.has_value());
    CHECK_FALSE(short_one->has_totals);
    CHECK(short_one->playhead_ms[1] == 75250);
    // Another type or no "TCN": nothing.
    t[7] = 5;
    CHECK_FALSE(decode_time(t).has_value());
    CHECK(decode_header(t).has_value());
    t[5] = 'X';
    CHECK_FALSE(decode_header(t).has_value());
}

namespace {

constexpr std::int64_t kMs = 1'000'000;

LinkPacket on_port(int port, std::vector<std::uint8_t> bytes, std::int64_t t_ns) {
    LinkPacket p;
    p.t_ns = t_ns;
    p.port = port;
    p.src = "169.254.1.2";
    p.bytes = std::move(bytes);
    return p;
}

LinkPacket position(int device, double playhead_ms, std::int64_t t_ns, double pitch = 0.0,
                    double length_s = 400.0, double bpm = 150.0) {
    return on_port(prodjlink::kBeatPort,
                   prodjlink::encode(prodjlink::Position{"CDJ-3000", device, length_s, playhead_ms,
                                                         pitch, bpm * (1 + pitch)}),
                   t_ns);
}

} // namespace

TEST_CASE("decks: positions say playing, paused and cued; the playhead extrapolates") {
    DeckSet decks;
    std::int64_t t = 1000 * kMs;
    // Deck 2 playing at +2%: positions every 30 ms advance by 30.6 ms.
    double head = 60000.0;
    for (int k = 0; k < 10; ++k) {
        REQUIRE(decks.apply(position(2, head, t, 0.02)));
        head += 30.0 * 1.02;
        t += 30 * kMs;
    }
    const DeckState &d = *decks.deck(2);
    CHECK(d.present);
    CHECK(d.playing);
    CHECK(d.has_position);
    CHECK(d.track_length_s == 400.0);
    CHECK(d.pitch == doctest::Approx(0.02));
    CHECK(d.track_bpm == doctest::Approx(150.0).epsilon(0.002));
    CHECK(d.started_ns == 1030 * kMs);
    // 100 ms after the last report: 102 ms further.
    CHECK(deck_position_ms(d, d.position_ns + 100 * kMs) == doctest::Approx(d.playhead_ms + 102.0));
    // Paused: the playhead stops; after 150 ms without movement the deck stops.
    const double held = d.playhead_ms;
    for (int k = 0; k < 8; ++k) {
        decks.apply(position(2, held, t, 0.02));
        t += 30 * kMs;
    }
    CHECK_FALSE(decks.deck(2)->playing);
    CHECK(deck_position_ms(*decks.deck(2), t + 500 * kMs) == doctest::Approx(held));
    // Cued back to 0 (a jump backwards): stopped; then plays again from there.
    decks.apply(position(2, 0.0, t, 0.02));
    CHECK_FALSE(decks.deck(2)->playing);
    t += 30 * kMs;
    decks.apply(position(2, 30.6, t, 0.02));
    CHECK(decks.deck(2)->playing);
    CHECK(decks.deck(2)->started_ns == t);
    // Other decks untouched; silence clears the deck.
    CHECK_FALSE(decks.deck(1)->present);
    decks.expire(t + 4000 * kMs);
    CHECK_FALSE(decks.deck(2)->present);
    CHECK(decks.deck(2)->number == 2);
}

TEST_CASE("decks: beats alone, the mixer's on-air flags, TCNet layers") {
    DeckSet decks;
    // A CDJ-2000NXS2 sends beats but no position: playing, position unknown.
    const prodjlink::Beat beat{"CDJ-2000NXS2", 1, 140.0, 0.0, 1, 428, 1714};
    decks.apply(on_port(prodjlink::kBeatPort, prodjlink::encode(beat), 100 * kMs));
    CHECK(decks.deck(1)->playing);
    CHECK_FALSE(decks.deck(1)->has_position);
    CHECK(decks.deck(1)->track_bpm == 140.0);
    decks.expire(600 * kMs); // Under two beats (0.86 s): still playing.
    CHECK(decks.deck(1)->playing);
    decks.expire(1100 * kMs); // Over two beats without one: stopped.
    CHECK_FALSE(decks.deck(1)->playing);

    // The mixer: channels 2 and 3 on air.
    prodjlink::OnAir air{"DJM-A9", 33, 4, {false, true, true, false, false, false}};
    decks.apply(on_port(prodjlink::kBeatPort, prodjlink::encode(air), 1200 * kMs));
    CHECK(decks.mixer_name() == "DJM-A9");
    CHECK(decks.deck(2)->on_air_known);
    CHECK(decks.deck(2)->on_air);
    CHECK_FALSE(decks.deck(1)->on_air);
    CHECK(decks.deck(1)->on_air_known);

    // TCNet: layer 3 playing at 12.5 s of 380 s; layer 4 paused.
    tcnet::Time t;
    t.header.node_name = "BRIDGE";
    t.playhead_ms[2] = 12500;
    t.total_ms[2] = 380000;
    t.state[2] = tcnet::LayerState::playing;
    t.playhead_ms[3] = 4000;
    t.total_ms[3] = 300000;
    t.state[3] = tcnet::LayerState::paused;
    decks.apply(on_port(tcnet::kTimePort, tcnet::encode_time(t), 1300 * kMs));
    CHECK(decks.deck(3)->playing);
    CHECK(decks.deck(3)->track_length_s == 380.0);
    CHECK(deck_position_ms(*decks.deck(3), 1400 * kMs) == doctest::Approx(12600.0));
    CHECK_FALSE(decks.deck(4)->playing);
    CHECK(decks.deck(4)->present);
    CHECK_FALSE(decks.deck(5)->present); // Empty layers are not decks.
    // The mixer gone quiet: on-air no longer known.
    decks.expire(5000 * kMs);
    CHECK(decks.mixer_name().empty());
    CHECK_FALSE(decks.deck(2)->on_air_known);
}
