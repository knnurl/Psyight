#include "output/node_setup.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace psyight {

int NodePlan::active_mask() const { return (1 << static_cast<int>(lanes.size())) - 1; }

namespace {

// Strips chained in patch order, a new line when the next strip would pass `limit` pixels.
std::vector<NodeLane> chain(const Project &project, int limit) {
    std::vector<NodeLane> lanes;
    int pixels = 0;
    for (size_t s = 0; s < project.strips.size(); ++s) {
        const int count = std::max(0, project.strips[s].spec.count);
        if (lanes.empty() || lanes.back().pixels + count > limit) {
            lanes.push_back({pixels, 0, static_cast<int>(s), 0, project.strips[s].order});
        }
        lanes.back().pixels += count;
        ++lanes.back().strips;
        pixels += count;
    }
    return lanes;
}

} // namespace

NodePlan plan_node(const Project &project) {
    NodePlan plan;
    plan.first_universe = project.first_universe;
    plan.pixels_per_universe = project.pixels_per_universe;
    // Strips chained in patch order. Lines short enough for the show's 60 fps when the node's 8
    // lines allow it (fewer pixels a line refresh faster), else as full as a line takes.
    const bool own_lines = project.node.one_strip_per_line;
    if (own_lines && static_cast<int>(project.strips.size()) <= kNodeLanes) {
        plan.lanes = chain(project, 0); // A limit of 0: every strip starts a line.
    } else {
        plan.lanes = chain(project, kNodeLanePixelsAt60Fps);
        if (static_cast<int>(plan.lanes.size()) > kNodeLanes) {
            plan.lanes = chain(project, kNodeLanePixels);
        }
        if (own_lines) {
            plan.notes.push_back("One strip per line needs at most " + std::to_string(kNodeLanes) +
                                 " strips; this rig has " + std::to_string(project.strips.size()) +
                                 ", so they are chained.");
        }
    }
    for (size_t s = 0; s < project.strips.size(); ++s) {
        const int count = std::max(0, project.strips[s].spec.count);
        if (count > kNodeLanePixels) {
            plan.problems.push_back(project.strips[s].spec.name + " has " + std::to_string(count) +
                                    " pixels: a lane takes at most " +
                                    std::to_string(kNodeLanePixels) + ".");
        }
        plan.pixels += count;
    }
    for (const NodeLane &lane : plan.lanes) {
        for (int s = lane.first_strip + 1; s < lane.first_strip + lane.strips; ++s) {
            const ProjectStrip &strip = project.strips[static_cast<size_t>(s)];
            if (strip.order != lane.order) {
                plan.notes.push_back(strip.spec.name + " is " + colour_order_name(strip.order) +
                                     " on a line that starts " + colour_order_name(lane.order) +
                                     ": the show is right, but the node's own fallback show "
                                     "uses " +
                                     colour_order_name(lane.order) + " for the whole line.");
            }
        }
    }
    plan.fallback = project.node;
    for (size_t l = 0; l < plan.lanes.size(); ++l) {
        if (plan.lanes[l].pixels > kNodeLanePixelsAt60Fps) {
            char line[160];
            std::snprintf(line, sizeof(line),
                          "Line %zu has %d pixels: it refreshes at most %.0f times a second, "
                          "below the show's 60 (fewer pixels a line keep every frame).",
                          l + 1, plan.lanes[l].pixels, node_lane_max_fps(plan.lanes[l].pixels));
            plan.notes.push_back(line);
        }
    }
    const int per_universe = std::max(1, plan.pixels_per_universe);
    plan.universes = (plan.pixels + per_universe - 1) / per_universe;
    if (static_cast<int>(plan.lanes.size()) > kNodeLanes) {
        plan.problems.push_back("The strips need " + std::to_string(plan.lanes.size()) +
                                " data lines; the node has " + std::to_string(kNodeLanes) + ".");
    }
    if (plan.universes > kNodeMaxUniverses) {
        plan.problems.push_back("The rig needs " + std::to_string(plan.universes) +
                                " universes; the node takes " + std::to_string(kNodeMaxUniverses) +
                                ".");
    }
    return plan;
}

const char *node_lane_pin(int lane) {
    // UM1974 Table 13 (firmware/node-f767/src/ws2812: PD0 to PD7).
    static const char *const kPins[kNodeLanes] = {
        "PD0 (CN9 pin 25)", "PD1 (CN9 pin 27)", "PD2 (CN8 pin 12)", "PD3 (CN9 pin 10)",
        "PD4 (CN9 pin 8)",  "PD5 (CN9 pin 6)",  "PD6 (CN9 pin 4)",  "PD7 (CN9 pin 2)"};
    return lane >= 0 && lane < kNodeLanes ? kPins[lane] : "";
}

double node_lane_max_fps(int pixels) { return 1.0 / (std::max(0, pixels) * 30e-6 + 300e-6); }

namespace {

std::string hex_colour(const Rgb &c) {
    const auto byte = [](float v) {
        return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    char text[8];
    std::snprintf(text, sizeof(text), "%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    return text;
}

} // namespace

std::vector<std::string> node_commands(const NodePlan &plan, const Palette *palette) {
    std::vector<std::string> commands;
    for (int l = 0; l < kNodeLanes; ++l) {
        const bool used = l < static_cast<int>(plan.lanes.size());
        const NodeLane lane = used ? plan.lanes[static_cast<size_t>(l)] : NodeLane{};
        commands.push_back("set lane" + std::to_string(l) + " " + std::to_string(lane.first_pixel) +
                           " " + std::to_string(lane.pixels));
    }
    for (size_t l = 0; l < plan.lanes.size(); ++l) {
        commands.push_back("set order" + std::to_string(l) + " " +
                           colour_order_name(plan.lanes[l].order));
    }
    commands.push_back("set active " + std::to_string(plan.active_mask()));
    commands.push_back("set universe " + std::to_string(plan.first_universe));
    commands.push_back("set universes " + std::to_string(plan.universes));
    commands.push_back("set pixels_per_universe " + std::to_string(plan.pixels_per_universe));
    const NodeFallback &f = plan.fallback;
    commands.push_back(std::string("set policy ") + node_loss_policy_id(f.policy));
    commands.push_back("set loss_ms " + std::to_string(f.loss_ms));
    commands.push_back("set fade_ms " + std::to_string(f.fade_ms));
    commands.push_back("set fallback_brightness " +
                       std::to_string(std::lround(std::clamp(f.brightness, 0.0, 1.0) * 1000.0)));
    commands.push_back("set fallback_speed " +
                       std::to_string(std::lround(std::clamp(f.speed_hz, 0.0, 10.0) * 1000.0)));
    if (palette != nullptr && !palette->colours.empty()) {
        std::string line = "set palette";
        const size_t n =
            std::min(palette->colours.size(), static_cast<size_t>(kNodePaletteColours));
        for (size_t i = 0; i < n; ++i) {
            line += " " + hex_colour(palette->colours[i]);
        }
        commands.push_back(line);
    }
    commands.push_back("apply");
    return commands;
}

std::vector<std::string> commands_for_protocol(std::vector<std::string> commands, int protocol) {
    if (protocol < 3) {
        std::erase_if(commands, [](const std::string &c) { return c.rfind("set order", 0) == 0; });
    }
    return commands;
}

std::map<std::string, std::string> parse_node_settings(std::string_view reply) {
    std::map<std::string, std::string> settings;
    while (!reply.empty()) {
        const size_t end = reply.find('\n');
        std::string_view line = reply.substr(0, end);
        reply = end == std::string_view::npos ? std::string_view() : reply.substr(end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1);
        }
        const size_t space = line.find(' ');
        if (line.empty() || space == 0) {
            continue;
        }
        settings[std::string(line.substr(0, space))] =
            space == std::string_view::npos ? std::string() : std::string(line.substr(space + 1));
    }
    return settings;
}

int node_protocol(const std::map<std::string, std::string> &settings) {
    const auto it = settings.find("protocol");
    return it == settings.end() ? 1 : std::max(1, std::atoi(it->second.c_str()));
}

std::vector<std::string> node_differences(const NodePlan &plan,
                                          const std::map<std::string, std::string> &settings) {
    std::vector<std::string> out;
    const auto check = [&](const std::string &key, const std::string &want, const char *what) {
        const auto it = settings.find(key);
        const std::string have = it == settings.end() ? "(none)" : it->second;
        if (have != want) {
            out.push_back(std::string(what) + ": the node has " + have + ", the rig needs " + want);
        }
    };
    check("universe", std::to_string(plan.first_universe), "First universe");
    check("universes", std::to_string(plan.universes), "Universes");
    check("pixels_per_universe", std::to_string(plan.pixels_per_universe), "Pixels per universe");
    check("active", std::to_string(plan.active_mask()), "Outputs");
    for (int l = 0; l < kNodeLanes; ++l) {
        if (l >= static_cast<int>(plan.lanes.size())) {
            break; // Unused lanes are off (the outputs mask): what they hold does not matter.
        }
        const NodeLane &lane = plan.lanes[static_cast<size_t>(l)];
        check("lane" + std::to_string(l),
              std::to_string(lane.first_pixel) + " " + std::to_string(lane.pixels),
              ("Lane " + std::to_string(l + 1)).c_str());
        if (node_protocol(settings) >= 3) { // Older firmware has no orders.
            check("order" + std::to_string(l), colour_order_name(lane.order),
                  ("Lane " + std::to_string(l + 1) + " colour order").c_str());
        }
    }
    const NodeFallback &f = plan.fallback;
    check("policy", node_loss_policy_id(f.policy), "Signal loss");
    check("loss_ms", std::to_string(f.loss_ms), "Signal lost after (ms)");
    check("fade_ms", std::to_string(f.fade_ms), "Fade (ms)");
    check("fallback_brightness",
          std::to_string(std::lround(std::clamp(f.brightness, 0.0, 1.0) * 1000.0)),
          "Fallback brightness (thousandths)");
    check("fallback_speed", std::to_string(std::lround(std::clamp(f.speed_hz, 0.0, 10.0) * 1000.0)),
          "Fallback speed (mHz)");
    return out;
}

Result<std::string> node_request(UdpSocket &socket, const std::array<std::uint8_t, 4> &node,
                                 const std::string &command, int timeout_ms) {
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(command.data());
    std::array<std::uint8_t, 2048> buffer{};
    Ipv4Endpoint from;
    // Sent up to three times: the node's first reply can wait for ARP to resolve this computer
    // (seconds on a direct cable), and a resent command finds it resolved. Every command is safe
    // to repeat. Replies from elsewhere (another node, a stray packet) are skipped.
    for (int send = 0; send < kNodeRequestTries; ++send) {
        if (Result<void> sent = socket.send_to(std::span<const std::uint8_t>(bytes, command.size()),
                                               {node, kNodeConfigPort});
            !sent) {
            return sent.error();
        }
        for (int stray = 0; stray < 4; ++stray) {
            Result<size_t> got = socket.receive(buffer, timeout_ms, &from);
            if (!got) {
                return got.error();
            }
            if (got.value() == 0) {
                break; // Timed out: send again.
            }
            if (from.address == node) {
                return std::string(reinterpret_cast<const char *>(buffer.data()), got.value());
            }
        }
    }
    return Error{"no reply from " + ipv4_text(node) + " (is it a Psyight node, and on?)"};
}

} // namespace psyight
