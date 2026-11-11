// Link packets (docs/PLAN_FOLLOW_MODE.md): a datagram from the DJ network as received, stamped
// on arrival with the steady clock, and the packet log: one JSON line a packet, the format
// tools/python/djlink_log.py writes, so a Python capture replays here and a capture made here
// reads there:
//   {"t_ns": steady clock ns, "utc": "...", "port": 50001, "src": "169.254.x.y", "hex": "..."}
// t_ns is the steady clock (QueryPerformanceCounter on Windows, as Python's perf_counter_ns and
// the steady_clock_ns column of a live recording's clock file), so packets and audio line up.
#pragma once

#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

struct LinkPacket {
    std::int64_t t_ns = 0;
    std::string utc; // For reading a log by eye; may be empty.
    int port = 0;    // The protocol port it arrived on (50000 to 50002, 60000 to 60002).
    std::string src; // The sender's address.
    std::vector<std::uint8_t> bytes;

    bool operator==(const LinkPacket &) const = default;
};

// The steady clock now, in ns (the packets' clock).
std::int64_t steady_now_ns();

// One line of the log (with the newline), and back.
std::string link_packet_line(const LinkPacket &packet);
Result<LinkPacket> parse_link_packet_line(std::string_view line);
// A whole log; blank lines skipped, a malformed line an error naming it.
Result<std::vector<LinkPacket>> parse_link_log(std::string_view text);
Result<std::vector<LinkPacket>> read_link_log(const std::filesystem::path &path);

} // namespace psyight
