// Preflight checks (SPEC section 11): what to confirm before a show, as a list of findings.
// A pure function of what it is given, so it is testable without hardware; the GUI gathers the
// inputs (the project, the audio device, the network interfaces, discovered nodes).
// Not yet checked: recording disk space (no recorder yet), per-PSU budgets (one budget so far).
#pragma once

#include "analysis/track_analysis.h"
#include "engine/rig.h"
#include "io/project.h"
#include "output/art_poll.h"
#include "output/patch.h"
#include "output/udp_socket.h"

#include <optional>
#include <string>
#include <vector>

namespace psyight {

enum class CheckLevel { ok, info, warn, fail };

struct PreflightCheck {
    std::string area; // "Audio", "Analysis", "Rig", "Patch", "Power", "Limiter", "Network".
    CheckLevel level = CheckLevel::ok;
    std::string message;
};

struct PreflightInputs {
    const Project *project = nullptr;        // Required.
    const Rig *rig = nullptr;                // Required: built from the project.
    const Patch *patch = nullptr;            // Required.
    const TrackAnalysis *analysis = nullptr; // Null: no track or no tempo.
    bool track_loaded = false;
    int device_sample_rate_hz = 0; // 0: no device open.
    int device_period_frames = 0;
    bool sending = false;
    std::vector<NetworkInterface> interfaces;
    std::optional<std::vector<ArtNode>> nodes; // Unset: discovery not run.
};

std::vector<PreflightCheck> run_preflight(const PreflightInputs &in);

const char *check_level_name(CheckLevel level); // "ok", "info", "warning", "fail".

} // namespace psyight
