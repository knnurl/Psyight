// Project file, schema 1 (SPEC section 10): `.psyproj` JSON holding the rig (strips with colour
// orders), the patch, the output settings, the limiters, the latency offset and the scene
// automation settings. Scenes, palettes, modulation and the MIDI map come later.
// Every section and field is optional when parsing and takes its default when missing, so files
// survive version changes; unknown fields are ignored. Whether to send to the network is not
// stored: the app always starts with sending off.
#pragma once

#include "core/result.h"
#include "engine/automation.h"
#include "engine/limiters.h"
#include "engine/rig.h"
#include "output/patch.h"
#include "output/sender.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

inline constexpr int kProjectSchemaVersion = 1;

struct ProjectStrip {
    StripSpec spec;
    ColourOrder order = ColourOrder::grb;
};

struct Project {
    std::vector<ProjectStrip> strips;
    int first_universe = 1;
    int pixels_per_universe = 170;
    OutputSettings output;            // cid is derived from source_name.
    std::string source_name = "Psyight"; // sACN source name and CID seed.
    LimiterParams limiter;
    double latency_offset_ms = 0.0;
    AutomationParams automation;
};

// The default frame rig (engine/rig.h default_rig) with default settings.
Project default_project();

Rig rig_of(const Project &project);
Patch patch_of(const Project &project);

const char *colour_order_name(ColourOrder order); // "rgb", "grb" and so on.

// Fails on malformed JSON, another schema, a wrongly typed or out-of-range field, a strip with
// fewer than two polyline points, or an unknown colour order or protocol.
Result<Project> parse_project_json(std::string_view json);
Result<std::string> to_project_json(const Project &project);

Result<Project> load_project(const std::filesystem::path &path);
Result<void> save_project(const std::filesystem::path &path, const Project &project);

} // namespace psyight
