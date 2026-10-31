// Fuzz test for the node firmware's portable core (firmware/node-f767/src/core): everything a
// node takes from the network. Built with AddressSanitizer, so a read or write outside a buffer
// stops the run with a report, where on the board it would corrupt memory or fault.
//
// Each round sends the node either a packet or a config command:
//  - packets start as well-formed sACN (data, sync), Art-Net (DMX, sync, poll) or DDP (with and
//    without a timecode) and are mutated: bit flips, odd bytes, length fields set to extremes,
//    truncation, extra bytes. Every parser sees every packet (any bytes can reach any port), and
//    what parses goes to node_on_packet, as on the board;
//  - commands are random "set" lines (known keys, extreme and malformed values), the other
//    commands, or mutated text, never null-terminated (UDP payloads are not). An "apply" takes
//    the new settings at once, as main.c does.
// After each round the node renders a frame at a time that jumps about (including across the
// 32-bit wrap) and transposes it for the LED DMA. Every input sits in a heap block of exactly its
// size, so reading one byte past it is caught.
//
// Deterministic (a fixed seed), so a failure reproduces. Usage: node_fuzz [rounds] [seed].

#include "node_config.h"
#include "node_core.h"
#include "protocol.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes &p, size_t at, unsigned v) {
    p[at] = static_cast<std::uint8_t>(v >> 8);
    p[at + 1] = static_cast<std::uint8_t>(v);
}

void put32(Bytes &p, size_t at, std::uint32_t v) {
    put16(p, at, v >> 16);
    put16(p, at + 2, v & 0xffffu);
}

const std::uint8_t kAcnId[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};

Bytes sacn_data(unsigned universe, unsigned sync_universe, std::uint8_t sequence, size_t slots) {
    Bytes p(126 + slots, 0);
    put16(p, 0, 0x0010);
    std::memcpy(&p[4], kAcnId, 12);
    put16(p, 16, 0x7000u | static_cast<unsigned>(p.size() - 16));
    put32(p, 18, 0x00000004u);
    put16(p, 38, 0x7000u | static_cast<unsigned>(p.size() - 38));
    put32(p, 40, 0x00000002u);
    p[108] = 100;
    put16(p, 109, sync_universe);
    p[111] = sequence;
    put16(p, 113, universe);
    put16(p, 115, 0x7000u | static_cast<unsigned>(p.size() - 115));
    p[117] = 0x02;
    p[118] = 0xa1;
    put16(p, 121, 1);
    put16(p, 123, static_cast<unsigned>(slots + 1));
    for (size_t i = 0; i < slots; ++i) {
        p[126 + i] = static_cast<std::uint8_t>(i * 7);
    }
    return p;
}

Bytes sacn_sync(unsigned sync_universe, std::uint8_t sequence) {
    Bytes p(49, 0);
    put16(p, 0, 0x0010);
    std::memcpy(&p[4], kAcnId, 12);
    put16(p, 16, 0x7000u | (49 - 16));
    put32(p, 18, 0x00000008u);
    put16(p, 38, 0x7000u | (49 - 38));
    put32(p, 40, 0x00000001u);
    p[44] = sequence;
    put16(p, 45, sync_universe);
    return p;
}

Bytes art_header(std::uint16_t opcode, size_t size) {
    Bytes p(size, 0);
    std::memcpy(&p[0], "Art-Net", 8);
    p[8] = static_cast<std::uint8_t>(opcode & 0xff);
    p[9] = static_cast<std::uint8_t>(opcode >> 8);
    p[11] = 14;
    return p;
}

Bytes art_dmx(unsigned port_address, std::uint8_t sequence, size_t slots) {
    Bytes p = art_header(0x5000, 18 + slots);
    p[12] = sequence;
    p[14] = static_cast<std::uint8_t>(port_address & 0xff);
    p[15] = static_cast<std::uint8_t>((port_address >> 8) & 0x7f);
    put16(p, 16, static_cast<unsigned>(slots));
    for (size_t i = 0; i < slots; ++i) {
        p[18 + i] = static_cast<std::uint8_t>(i * 3);
    }
    return p;
}

Bytes ddp(std::uint8_t sequence, std::uint32_t offset, size_t length, bool push, bool timecode) {
    const size_t header = timecode ? 14 : 10;
    Bytes p(header + length, 0);
    p[0] = static_cast<std::uint8_t>(0x40 | (timecode ? 0x10 : 0) | (push ? 0x01 : 0));
    p[1] = sequence;
    p[2] = 0x0b;
    p[3] = 1;
    put32(p, 4, offset);
    put16(p, 8, static_cast<unsigned>(length));
    for (size_t i = 0; i < length; ++i) {
        p[header + i] = static_cast<std::uint8_t>(i * 5);
    }
    return p;
}

struct Fuzzer {
    std::mt19937 rng;
    explicit Fuzzer(std::uint32_t seed) : rng(seed) {}

    std::uint32_t below(std::uint32_t n) {
        return n == 0 ? 0 : static_cast<std::uint32_t>(rng() % n);
    }
    bool chance(std::uint32_t one_in) { return below(one_in) == 0; }

    std::uint32_t interesting(size_t size) {
        const std::uint32_t values[] = {0,
                                        1,
                                        2,
                                        0x7f,
                                        0x80,
                                        0xff,
                                        0x100,
                                        0x0fff,
                                        0x7fff,
                                        0x8000,
                                        0xffff,
                                        0x10000,
                                        0x7fffffffu,
                                        0x80000000u,
                                        0xffffffffu,
                                        static_cast<std::uint32_t>(size),
                                        static_cast<std::uint32_t>(size + 1),
                                        static_cast<std::uint32_t>(size - 16)};
        return chance(4) ? rng() : values[below(sizeof(values) / sizeof(values[0]))];
    }

    Bytes seed_packet() {
        const std::uint8_t seq = static_cast<std::uint8_t>(rng());
        switch (below(8)) {
        case 0:
            return sacn_data(1 + below(30), chance(2) ? 0 : 1000, seq, below(513));
        case 1:
            return sacn_sync(chance(2) ? 1000 : below(65536), seq);
        case 2:
            return art_dmx(below(40), seq, 2 + below(511));
        case 3:
            return art_header(0x5200, 14); // ArtSync.
        case 4:
            return art_header(0x2000, 14); // ArtPoll.
        case 5:
            return ddp(static_cast<std::uint8_t>(below(16)), below(30000), below(1441), chance(2),
                       false);
        case 6:
            return ddp(static_cast<std::uint8_t>(below(16)), interesting(0), below(1441), chance(2),
                       true);
        default: { // Noise.
            Bytes p(below(1500));
            for (std::uint8_t &b : p) {
                b = static_cast<std::uint8_t>(rng());
            }
            return p;
        }
        }
    }

    void mutate(Bytes &p) {
        const std::uint32_t ops = 1 + below(4);
        for (std::uint32_t op = 0; op < ops; ++op) {
            switch (below(6)) {
            case 0:
                if (!p.empty()) {
                    p[below(static_cast<std::uint32_t>(p.size()))] ^=
                        static_cast<std::uint8_t>(1u << below(8));
                }
                break;
            case 1:
                if (!p.empty()) {
                    p[below(static_cast<std::uint32_t>(p.size()))] =
                        static_cast<std::uint8_t>(interesting(p.size()));
                }
                break;
            case 2:
                if (p.size() >= 2) {
                    put16(p, below(static_cast<std::uint32_t>(p.size() - 1)),
                          interesting(p.size()) & 0xffffu);
                }
                break;
            case 3:
                if (p.size() >= 4) {
                    put32(p, below(static_cast<std::uint32_t>(p.size() - 3)),
                          interesting(p.size()));
                }
                break;
            case 4:
                p.resize(below(static_cast<std::uint32_t>(p.size() + 1)));
                break;
            default:
                for (std::uint32_t i = 1 + below(64); i > 0; --i) {
                    p.push_back(static_cast<std::uint8_t>(rng()));
                }
                break;
            }
        }
    }

    std::string number() {
        const char *values[] = {"0",          "1",
                                "2",          "3",
                                "8",          "24",
                                "25",         "170",
                                "171",        "255",
                                "256",        "680",
                                "681",        "63999",
                                "64000",      "65535",
                                "65536",      "600000",
                                "600001",     "4294967295",
                                "4294967296", "99999999999999999999",
                                "-1",         "+1",
                                " 1",         "1 ",
                                "0x10",       "1.5",
                                "",           "abc"};
        return chance(5) ? std::to_string(rng())
                         : values[below(sizeof(values) / sizeof(values[0]))];
    }

    std::string text(size_t most) {
        std::string s(below(static_cast<std::uint32_t>(most + 1)), ' ');
        for (char &c : s) {
            c = static_cast<char>(chance(8) ? rng() : 'a' + below(26));
        }
        return s;
    }

    std::string command() {
        static const char *keys[] = {"ip",
                                     "mask",
                                     "gateway",
                                     "dhcp",
                                     "name",
                                     "long_name",
                                     "protocols",
                                     "universe",
                                     "universes",
                                     "pixels_per_universe",
                                     "lane0",
                                     "lane3",
                                     "lane7",
                                     "lane8",
                                     "lane",
                                     "active",
                                     "loss_ms",
                                     "policy",
                                     "fade_ms",
                                     "fallback_brightness",
                                     "fallback_speed",
                                     "palette",
                                     "order0",
                                     "order7",
                                     "order8",
                                     "order",
                                     "bogus"};
        switch (below(12)) {
        case 0:
            return "get";
        case 1:
            return "apply";
        case 2:
            return "defaults";
        case 3:
            return "save";
        case 4:
            return text(200); // Noise, sometimes past the 160-byte limit.
        default:
            break;
        }
        const std::string key = keys[below(sizeof(keys) / sizeof(keys[0]))];
        std::string value;
        if (key == "ip" || key == "mask" || key == "gateway") {
            value = number() + "." + number() + "." + number() + "." + number();
        } else if (key.rfind("lane", 0) == 0) {
            value = number() + (chance(4) ? "" : " " + number()) + (chance(8) ? " 1" : "");
        } else if (key == "policy") {
            const char *v[] = {"hold", "fade", "fallback", "blackout", "Hold", ""};
            value = v[below(6)];
        } else if (key.rfind("order", 0) == 0) {
            const char *v[] = {"rgb", "grb", "brg", "rbg", "gbr", "bgr", "rgbw", ""};
            value = v[below(8)];
        } else if (key == "palette") {
            for (std::uint32_t i = below(7); i > 0; --i) {
                char colour[8];
                std::snprintf(colour, sizeof(colour), "%06x",
                              static_cast<unsigned>(rng() & 0xffffff));
                value += (value.empty() ? "" : " ") + std::string(chance(6) ? "zz" : colour);
            }
        } else if (key == "name" || key == "long_name") {
            value = text(80);
        } else {
            value = number();
        }
        std::string line = "set " + key + (chance(10) ? "" : " " + value);
        if (chance(5)) {
            line += chance(2) ? "\r\n" : "  ";
        }
        if (chance(6)) {
            Bytes bytes(line.begin(), line.end());
            mutate(bytes);
            line.assign(bytes.begin(), bytes.end());
        }
        return line;
    }
};

// An exactly sized heap copy, so ASan catches a read one past the end.
std::unique_ptr<std::uint8_t[]> exact_copy(const std::uint8_t *data, size_t n) {
    std::unique_ptr<std::uint8_t[]> copy(new std::uint8_t[n]);
    if (n > 0) {
        std::memcpy(copy.get(), data, n);
    }
    return copy;
}

struct Lanes {
    std::uint8_t lanes[NODE_LANES][NODE_LANE_BYTES];
};

} // namespace

int main(int argc, char **argv) {
    const long rounds = argc > 1 ? std::atol(argv[1]) : 200000;
    const std::uint32_t seed = argc > 2 ? static_cast<std::uint32_t>(std::atol(argv[2])) : 1u;
    Fuzzer f(seed);
    const std::uint8_t mac[6] = {0x02, 0x50, 1, 2, 3, 4};
    auto config = std::make_unique<node_config>();
    node_config_defaults(config.get(), mac);
    auto node = std::make_unique<node_state>();
    node_init(node.get(), &config->node);
    auto lanes = std::make_unique<Lanes>();
    std::vector<std::uint32_t> words;
    std::uint32_t now_ms = 0;
    long parsed = 0;
    long commands = 0;
    long applied = 0;
    long frames = 0;
    for (long round = 0; round < rounds; ++round) {
        if (f.chance(8)) {
            const std::string line = f.command();
            auto bytes =
                exact_copy(reinterpret_cast<const std::uint8_t *>(line.data()), line.size());
            auto reply = std::make_unique<char[]>(1400); // As net.c's.
            const node_command_action action =
                node_config_command(config.get(), mac, reinterpret_cast<const char *>(bytes.get()),
                                    line.size(), reply.get(), 1400);
            if (std::memchr(reply.get(), '\0', 1400) == nullptr) {
                std::fprintf(stderr, "round %ld: reply not terminated for \"%s\"\n", round,
                             line.c_str());
                return 1;
            }
            ++commands;
            if (action == NODE_CMD_APPLY) {
                node_init(node.get(), &config->node);
                ++applied;
            }
        } else {
            Bytes p = f.seed_packet();
            if (!f.chance(4)) { // A quarter go through intact, to reach the deeper paths.
                f.mutate(p);
            }
            auto bytes = exact_copy(p.data(), p.size());
            psy_packet packet{};
            if (psy_parse_sacn(bytes.get(), p.size(), &packet)) {
                node_on_packet(node.get(), &packet, false, now_ms);
                ++parsed;
            }
            if (psy_parse_art_net(bytes.get(), p.size(), &packet)) {
                node_on_packet(node.get(), &packet, true, now_ms);
                ++parsed;
            }
            if (psy_parse_ddp(bytes.get(), p.size(), &packet)) {
                node_on_packet(node.get(), &packet, false, now_ms);
                ++parsed;
            }
        }
        // Time: mostly a frame on, sometimes a long pause, sometimes back a little (packets are
        // stamped after the loop reads the clock), sometimes across the 32-bit wrap.
        switch (f.below(16)) {
        case 0:
            now_ms += 2000 + f.below(600000);
            break;
        case 1:
            now_ms -= f.below(3);
            break;
        case 2:
            now_ms = 0xffffffffu - f.below(5000);
            break;
        default:
            now_ms += f.below(40);
            break;
        }
        if (node_frame(node.get(), now_ms, lanes->lanes)) {
            const std::uint16_t pixels = node_max_lane_pixels(&node->s);
            words.assign(static_cast<size_t>(pixels) * 24u, 0u);
            node_transpose(lanes->lanes, pixels, config->active_mask,
                           words.empty() ? nullptr : words.data());
            ++frames;
        }
        (void)node_signal_lost(node.get(), now_ms);
    }
    std::printf("node_fuzz: %ld rounds (seed %u): %ld packets parsed, %ld commands (%ld applied), "
                "%ld frames; no memory errors\n",
                rounds, static_cast<unsigned>(seed), parsed, commands, applied, frames);
    return 0;
}
