// Project file, schema 1 (SPEC section 10): `.psyproj` JSON holding the rig (strips with colour
// orders), the patch, the output settings, the limiters, the latency offset, the scene
// automation settings, the scenes (layers with their effect parameters, section tags, modulation
// routes) and the palettes. The MIDI map comes later.
// Every section and field is optional when parsing and takes its default when missing, so files
// survive version changes; unknown fields (and unknown effect parameters) are ignored. Whether to
// send to the network is not stored: the app always starts with sending off.
#pragma once

#include "core/result.h"
#include "engine/automation.h"
#include "engine/colour.h"
#include "engine/limiters.h"
#include "engine/rig.h"
#include "io/midi_map.h"
#include "output/patch.h"
#include "output/sender.h"

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

inline constexpr int kProjectSchemaVersion = 1;

struct ProjectStrip {
    StripSpec spec;
    ColourOrder order = ColourOrder::grb;
};

// What the pixel node does when the show's signal stops (SPEC 12.2), set on the node by
// Preflight > Node setup. Defaults are the firmware's.
enum class NodeLossPolicy { hold, fade, fallback, blackout };
struct NodeFallback {
    NodeLossPolicy policy = NodeLossPolicy::fallback;
    int loss_ms = 2000;       // Without data this long: the signal is lost.
    int fade_ms = 3000;       // Fade: to black over this.
    double brightness = 0.1; // Fallback show, 0 to 1 before gamma.
    double speed_hz = 0.05;   // Fallback show drift.
    // Each strip on its own data line (no chaining) when the strips fit the node's 8 lines;
    // otherwise strips chain, as many to a line as keep 60 fps.
    bool one_strip_per_line = false;
};
const char *node_loss_policy_id(NodeLossPolicy policy); // "hold", "fade", "fallback", "blackout".
inline constexpr std::array<NodeLossPolicy, 4> kNodeLossPolicies = {
    NodeLossPolicy::hold, NodeLossPolicy::fade, NodeLossPolicy::fallback, NodeLossPolicy::blackout};

// The built-in preset the rig was made from (Rig > New rig > Presets), with its settings by id,
// so Current rig can show and change them; locked: the strips stay as edited, the settings are
// shown but do not rebuild them. An empty preset: a rig of its own (a template, the user's,
// a drawing, or edited strips only).
struct RigSource {
    std::string preset;
    std::map<std::string, double> values;
    bool locked = false;
};

struct Project {
    std::vector<ProjectStrip> strips;
    RigSource rig_source;
    int first_universe = 1;
    int pixels_per_universe = 170;
    OutputSettings output;               // cid is derived from source_name.
    std::string source_name = "Psyight"; // sACN source name and CID seed.
    NodeFallback node;                   // The pixel node's signal-loss settings.
    LimiterParams limiter;
    std::vector<GroupCap> group_caps;
    double latency_offset_ms = 0.0;
    AutomationParams automation;
    std::vector<Scene> scenes = default_scenes();
    std::vector<Palette> palettes = default_palettes();
    std::vector<MidiBinding> midi; // MIDI learn (io/midi_map.h).
    std::string midi_port;         // Reopened at start when present.
    // The macro knobs (modulation sources), saved with the show as a set saves its macros.
    std::array<double, kMacroCount> macro_values{};
    std::array<std::string, kMacroCount> macro_names{}; // Empty: "Macro n".
    std::string show_pack; // The show pack last loaded (apply_show_pack); empty: none.
};

// "Macro n" (from 1) unless the project names it.
std::string macro_name(const Project &project, int index);

// A preset's strips rebuilt with new settings, keeping what still fits of the current ones: a
// strip with the same name keeps its colour order and its place in the wiring (data line)
// order; new names follow in the preset's order with the first strip's colour order. Moved
// points are not kept (the shape comes from the settings).
std::vector<ProjectStrip> rebuilt_strips(const std::vector<ProjectStrip> &current,
                                         std::vector<StripSpec> made);

// The default frame rig (engine/rig.h default_rig) with default settings.
Project default_project();

Rig rig_of(const Project &project);
Patch patch_of(const Project &project);

const char *colour_order_name(ColourOrder order); // "rgb", "grb" and so on.

// Polyline as editable text: points separated by spaces or semicolons, coordinates by commas, in
// metres; z may be left out ("0,0 2.5,0" is two points at z = 0). nullopt when malformed or
// shorter than two points.
std::optional<std::vector<Vec3>> parse_polyline_text(const std::string &text);
std::string polyline_text(const std::vector<Vec3> &polyline);

// Fails on malformed JSON, another schema, a wrongly typed or out-of-range field, a strip with
// fewer than two polyline points, an unknown colour order, protocol, effect, blend mode or
// modulation id, an empty scene or palette list, or a palette without colours.
Result<Project> parse_project_json(std::string_view json);
Result<std::string> to_project_json(const Project &project);

Result<Project> load_project(const std::filesystem::path &path);
Result<void> save_project(const std::filesystem::path &path, const Project &project);

} // namespace psyight
