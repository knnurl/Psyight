#include "link/packet.h"

#include "io/text_file.h"

#include <nlohmann/json.hpp>

#include <chrono>

namespace psyight {

namespace {

constexpr char kHex[] = "0123456789abcdef";

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string link_packet_line(const LinkPacket &packet) {
    std::string hex;
    hex.reserve(packet.bytes.size() * 2);
    for (const std::uint8_t b : packet.bytes) {
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 15]);
    }
    const nlohmann::json j = {{"t_ns", packet.t_ns},
                              {"utc", packet.utc},
                              {"port", packet.port},
                              {"src", packet.src},
                              {"hex", hex}};
    return j.dump() + "\n";
}

Result<LinkPacket> parse_link_packet_line(std::string_view line) {
    try {
        const nlohmann::json j = nlohmann::json::parse(line);
        LinkPacket p;
        p.t_ns = j.at("t_ns").get<std::int64_t>();
        p.utc = j.value("utc", "");
        p.port = j.at("port").get<int>();
        p.src = j.value("src", "");
        const std::string hex = j.at("hex").get<std::string>();
        if (hex.size() % 2 != 0) {
            return Error{"odd hex length"};
        }
        p.bytes.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2) {
            const int hi = hex_value(hex[i]);
            const int lo = hex_value(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                return Error{"not hex"};
            }
            p.bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
        }
        return p;
    } catch (const nlohmann::json::exception &e) {
        return Error{std::string("invalid packet line: ") + e.what()};
    }
}

Result<std::vector<LinkPacket>> parse_link_log(std::string_view text) {
    std::vector<LinkPacket> out;
    size_t start = 0;
    int line_number = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++line_number;
        if (line.find_first_not_of(" \r\t") == std::string_view::npos) {
            continue;
        }
        Result<LinkPacket> p = parse_link_packet_line(line);
        if (!p) {
            return Error{"line " + std::to_string(line_number) + ": " + p.error().message};
        }
        out.push_back(std::move(p.value()));
    }
    return out;
}

Result<std::vector<LinkPacket>> read_link_log(const std::filesystem::path &path) {
    const Result<std::string> text = read_text_file(path);
    if (!text) {
        return text.error();
    }
    return parse_link_log(text.value());
}

} // namespace psyight
