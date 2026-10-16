#include "output/preflight.h"

#include <algorithm>
#include <cstdio>

namespace psyight {

namespace {

constexpr int kPixelBudget = 1500;     // SPEC 2.1.
constexpr int kExpectedRateHz = 48000; // The analysis rate (SPEC 4.1).
constexpr std::uint16_t kMaxPortAddress = 0x7fff;
constexpr int kMaxSacnUniverse = 63999;

std::string format(const char *pattern, auto... args) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), pattern, args...);
    return buffer;
}

std::string universe_list(const std::vector<int> &universes) {
    std::string out;
    for (size_t i = 0; i < universes.size(); ++i) {
        size_t j = i;
        while (j + 1 < universes.size() && universes[j + 1] == universes[j] + 1) {
            ++j;
        }
        out += (out.empty() ? "" : ", ") + std::to_string(universes[i]);
        if (j > i) {
            out += " to " + std::to_string(universes[j]);
        }
        i = j;
    }
    return out;
}

} // namespace

const char *check_level_name(CheckLevel level) {
    switch (level) {
    case CheckLevel::ok:
        return "ok";
    case CheckLevel::info:
        return "info";
    case CheckLevel::warn:
        return "warning";
    case CheckLevel::fail:
        return "fail";
    }
    return "info";
}

std::vector<PreflightCheck> run_preflight(const PreflightInputs &in) {
    std::vector<PreflightCheck> out;
    const auto add = [&out](const char *area, CheckLevel level, std::string message) {
        out.push_back({area, level, std::move(message)});
    };
    const Project &p = *in.project;
    const Rig &rig = *in.rig;
    const Patch &patch = *in.patch;

    // Audio.
    if (in.device_sample_rate_hz == 0) {
        add("Audio", CheckLevel::warn, "No audio device is open (load a track).");
    } else {
        add("Audio",
            in.device_sample_rate_hz == kExpectedRateHz ? CheckLevel::ok : CheckLevel::warn,
            format("Device at %d Hz, period %d frames (%.1f ms).", in.device_sample_rate_hz,
                   in.device_period_frames,
                   1e3 * in.device_period_frames / in.device_sample_rate_hz));
    }

    // Analysis.
    if (!in.track_loaded) {
        add("Analysis", CheckLevel::warn, "No track loaded.");
    } else if (in.analysis == nullptr) {
        add("Analysis", CheckLevel::fail, "No tempo found: no grid, so the show stays dark.");
    } else {
        const TrackAnalysis &a = *in.analysis;
        add("Analysis", CheckLevel::ok,
            format("%.2f BPM (tempo confidence %.2f, downbeat %.2f), %zu sections, %zu kicks, "
                   "%zu bass notes.",
                   a.grid.bpm, a.beats.tempo_confidence, a.beats.downbeat_confidence,
                   a.sections.size(), a.bass.kick_times_s.size(), a.bass.notes.size()));
        if (a.sections.empty()) {
            add("Analysis", CheckLevel::warn, "No sections: scene automation stays on scene 1.");
        }
    }

    // Rig.
    const auto pixels = static_cast<int>(rig.pixels.size());
    if (pixels == 0) {
        add("Rig", CheckLevel::fail, "The rig has no pixels.");
    } else {
        add("Rig", pixels > kPixelBudget ? CheckLevel::warn : CheckLevel::ok,
            format("%d pixels in %zu strips%s.", pixels, p.strips.size(),
                   pixels > kPixelBudget ? ", over the 1,500-pixel budget" : ""));
    }
    for (const ProjectStrip &s : p.strips) {
        const int fill = pixels_to_fill(s.spec);
        if (s.spec.count > fill + 1) {
            add("Rig", CheckLevel::warn,
                format("%s: %d pixels run %.2f m past its polyline.", s.spec.name.c_str(),
                       s.spec.count, (s.spec.count - fill) / s.spec.pixels_per_m));
        } else if (s.spec.count == 0) {
            add("Rig", CheckLevel::info, format("%s has no pixels.", s.spec.name.c_str()));
        }
    }

    // Patch.
    if (!patch.universes.empty()) {
        const int last = patch.universes.back();
        const bool sacn = p.output.protocol == OutputProtocol::sacn;
        if (sacn && last > kMaxSacnUniverse) {
            add("Patch", CheckLevel::fail, format("Universe %d is beyond sACN's 63999.", last));
        } else if (!sacn && last - 1 > kMaxPortAddress) {
            add("Patch", CheckLevel::fail,
                format("Universe %d is beyond Art-Net's port addresses.", last));
        } else {
            add("Patch", CheckLevel::ok,
                format("Universes %s, %d pixels each.", universe_list(patch.universes).c_str(),
                       p.pixels_per_universe));
        }
        if (sacn && p.output.sync &&
            std::find(patch.universes.begin(), patch.universes.end(), p.output.sync_universe) !=
                patch.universes.end()) {
            add("Patch", CheckLevel::fail,
                format("The sync universe %d also carries pixel data.", p.output.sync_universe));
        }
    }

    // Power.
    const double full_white_a = pixels * 3 * p.limiter.current_per_channel_ma * 1e-3;
    if (p.limiter.budget_a <= 0.0) {
        add("Power", CheckLevel::warn,
            format("Power budget off; full white would draw about %.1f A.", full_white_a));
    } else {
        add("Power", CheckLevel::ok,
            format("Budget %.1f A; full white would draw about %.1f A%s.", p.limiter.budget_a,
                   full_white_a, full_white_a > p.limiter.budget_a ? " (scaled down)" : ""));
    }

    // Limiter and timing.
    if (!p.limiter.flash_limiter) {
        add("Limiter", CheckLevel::fail,
            "Flash limiter off: photosensitive epilepsy risk (SPEC 7.7).");
    } else {
        add("Limiter", CheckLevel::ok,
            format("Flash limiter on, %.0f per second.", p.limiter.max_flashes_per_s));
    }
    if (p.limiter.brightness_cap < 0.2) {
        add("Limiter", CheckLevel::warn,
            format("Brightness cap %.2f: the show will be very dim.", p.limiter.brightness_cap));
    }
    if (p.latency_offset_ms == 0.0) {
        add("Timing", CheckLevel::warn,
            "Latency offset is 0 ms: set it for the distance from the speakers (about 2.9 ms "
            "per metre, SPEC 4.3).");
    } else {
        add("Timing", CheckLevel::ok, format("Latency offset %.0f ms.", p.latency_offset_ms));
    }

    // Network.
    if (!in.sending) {
        add("Network", CheckLevel::info, "Sending is off: only the preview gets frames.");
    }
    if (in.interfaces.empty()) {
        add("Network", CheckLevel::warn, "No network interface is up.");
    }
    const bool multicast = p.output.protocol == OutputProtocol::sacn && p.output.multicast;
    if (multicast) {
        add("Network", CheckLevel::info,
            "sACN multicast: switches need IGMP snooping, or it floods the network.");
        if (in.interfaces.size() > 1) {
            add("Network", CheckLevel::warn,
                format("%zu interfaces are up; multicast leaves on the one the routing table "
                       "picks.",
                       in.interfaces.size()));
        }
    } else {
        const auto &address = p.output.address;
        const std::string text = ipv4_text(address);
        const auto on =
            std::find_if(in.interfaces.begin(), in.interfaces.end(),
                         [&](const NetworkInterface &i) { return i.contains(address); });
        if (address[0] == 127) {
            add("Network", CheckLevel::info,
                format("Sending to %s, this machine: no node will receive it.", text.c_str()));
        } else if (on != in.interfaces.end()) {
            add("Network", CheckLevel::ok,
                format("Node address %s is on %s (%s).", text.c_str(), on->name.c_str(),
                       ipv4_text(on->address).c_str()));
        } else {
            add("Network", CheckLevel::warn,
                format("Node address %s is not on any local subnet.", text.c_str()));
        }
    }

    // Nodes.
    if (!in.nodes) {
        add("Nodes", CheckLevel::info, "Node discovery has not run.");
    } else if (in.nodes->empty()) {
        add("Nodes", CheckLevel::warn, "No Art-Net node answered the poll.");
    } else {
        std::vector<int> covered;
        for (const ArtNode &n : *in.nodes) {
            std::vector<int> outputs(n.output_universes.begin(), n.output_universes.end());
            covered.insert(covered.end(), outputs.begin(), outputs.end());
            add("Nodes", CheckLevel::ok,
                format("%s at %s: outputs on universes %s.", n.short_name.c_str(),
                       ipv4_text(n.ip).c_str(), universe_list(outputs).c_str()));
        }
        std::vector<int> missing;
        for (const int u : patch.universes) {
            if (std::find(covered.begin(), covered.end(), u) == covered.end()) {
                missing.push_back(u);
            }
        }
        if (!missing.empty()) {
            add("Nodes", CheckLevel::warn,
                format("No node reported an output on universes %s.",
                       universe_list(missing).c_str()));
        }
    }
    return out;
}

} // namespace psyight
