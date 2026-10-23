#include "gui/preflight_view.h"

#include "gui/theme.h"
#include "gui/widgets.h"
#include "output/discovery.h"
#include "output/node_setup.h"
#include "output/preflight.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

namespace psyight {

namespace {

constexpr auto kInterfaceRefresh = std::chrono::seconds(3);
constexpr int kDiscoveryTimeoutMs = 1000;
constexpr std::uint16_t kArtNetPortNumber = 6454;

ImVec4 level_colour(CheckLevel level) {
    const ThemeTokens &t = theme();
    switch (level) {
    case CheckLevel::ok:
        return t.success;
    case CheckLevel::info:
        return t.text_dim;
    case CheckLevel::warn:
        return t.warning;
    case CheckLevel::fail:
        return t.danger;
    }
    return t.text;
}

// Polls every interface's subnet from a socket bound to that interface's address on Art-Net's
// port (nodes reply there). Bound, so the poll leaves through that interface: with several
// adapters in 169.254.0.0/16 (VPNs, virtual adapters), a poll from an unbound socket did not
// reliably reach a node cabled straight to the computer.
Result<std::vector<ArtNode>> discover(std::vector<NetworkInterface> interfaces) {
    std::vector<ArtNode> all;
    std::string first_error;
    for (const NetworkInterface &i : interfaces) {
        Result<std::unique_ptr<UdpSocket>> socket = open_udp_socket(i.address, kArtNetPortNumber);
        if (!socket) {
            if (first_error.empty()) {
                first_error = i.name + ": " + socket.error().message;
            }
            continue;
        }
        Result<std::vector<ArtNode>> found =
            discover_art_nodes(*socket.value(), {i.broadcast(), kArtNetPortNumber},
                               kDiscoveryTimeoutMs / static_cast<int>(interfaces.size()));
        if (!found) {
            if (first_error.empty()) {
                first_error = i.name + ": " + found.error().message;
            }
            continue;
        }
        for (ArtNode &n : found.value()) {
            const bool known = std::any_of(all.begin(), all.end(), [&](const ArtNode &a) {
                return a.ip == n.ip && a.bind_index == n.bind_index;
            });
            if (!known) {
                all.push_back(std::move(n));
            }
        }
    }
    if (all.empty() && !first_error.empty()) {
        return Error{first_error}; // Say why, rather than "no node answered".
    }
    return all;
}

} // namespace

void PreflightView::update() {
    const auto now = std::chrono::steady_clock::now();
    if (now - interfaces_at_ > kInterfaceRefresh) {
        interfaces_ = list_ipv4_interfaces(); // A few ms; not every frame.
        interfaces_at_ = now;
        // Discover by itself at start and whenever the addresses change (a cable plugged in, a
        // node's link-local address appearing on a direct cable).
        std::vector<std::array<std::uint8_t, 4>> addresses;
        for (const NetworkInterface &i : interfaces_) {
            addresses.push_back(i.address);
        }
        if (addresses != discovered_on_ && !interfaces_.empty() && !discovery_.valid()) {
            discovered_on_ = std::move(addresses);
            discovery_ = std::async(std::launch::async, discover, interfaces_);
        }
    }
    if (discovery_.valid() &&
        discovery_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Result<std::vector<ArtNode>> found = discovery_.get();
        if (found) {
            nodes_ = std::move(found.value());
            discovery_error_.clear();
        } else {
            discovery_error_ = found.error().message;
        }
    }

    PreflightInputs in;
    in.project = &project_.project();
    in.rig = &project_.rig();
    in.patch = &project_.patch();
    in.track_loaded = session_.has_track();
    in.analysis = session_.has_track() ? session_.track().track_analysis.get() : nullptr;
    Transport &transport = session_.transport();
    if (transport.is_open()) {
        in.device_sample_rate_hz = transport.buffer().sample_rate_hz;
        in.device_period_frames = transport.device_period_frames();
    }
    in.sending = preview_.sending();
    in.interfaces = interfaces_;
    in.nodes = nodes_;
    checks_ = run_preflight(in);
}

void PreflightView::draw() {
    ImGui::Begin("Preflight");
    const std::vector<PreflightCheck> &checks = checks_;

    const auto count = [&checks](CheckLevel level) {
        return std::count_if(checks.begin(), checks.end(),
                             [level](const PreflightCheck &c) { return c.level == level; });
    };
    const auto fails = count(CheckLevel::fail);
    const auto warnings = count(CheckLevel::warn);
    ImGui::TextColored(level_colour(fails > 0      ? CheckLevel::fail
                                    : warnings > 0 ? CheckLevel::warn
                                                   : CheckLevel::ok),
                       "%lld failed, %lld warnings", static_cast<long long>(fails),
                       static_cast<long long>(warnings));
    ImGui::SameLine();
    ImGui::BeginDisabled(discovery_.valid() || interfaces_.empty());
    if (ImGui::Button(discovery_.valid() ? "Discovering..." : "Discover Art-Net nodes")) {
        discovery_ = std::async(std::launch::async, discover, interfaces_);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Broadcasts an ArtPoll on each network interface and lists the nodes "
                          "that answer within a second.");
    ImGui::SameLine();
    if (ImGui::Button("Copy report")) {
        std::string report;
        for (const PreflightCheck &c : checks) {
            report += c.area + " (" + check_level_name(c.level) + "): " + c.message + "\n";
        }
        ImGui::SetClipboardText(report.c_str());
    }
    if (!discovery_error_.empty()) {
        ImGui::TextColored(level_colour(CheckLevel::warn), "Discovery: %s",
                           discovery_error_.c_str());
    }
    draw_test_patterns();
    draw_node_setup();
    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("checks", 2, flags)) {
        ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthFixed,
                                5.0f * ImGui::GetFontSize());
        ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthStretch);
        for (const PreflightCheck &c : checks) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(level_colour(c.level), "%s", c.area.c_str());
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, level_colour(c.level));
            ImGui::TextWrapped("%s", c.message.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void PreflightView::draw_discovery() {
    ImGui::BeginDisabled(discovery_.valid() || interfaces_.empty());
    if (ImGui::Button(discovery_.valid() ? "Discovering..." : "Discover Art-Net nodes")) {
        discovery_ = std::async(std::launch::async, discover, interfaces_);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Broadcasts an ArtPoll on each network interface and lists the nodes "
                          "that answer within a second.");
    if (!discovery_error_.empty()) {
        ImGui::TextColored(level_colour(CheckLevel::warn), "Discovery: %s",
                           discovery_error_.c_str());
    }
    if (!nodes_.has_value()) {
        ImGui::TextDisabled("Not run yet.");
    } else if (nodes_->empty()) {
        ImGui::TextColored(level_colour(CheckLevel::warn),
                           "No node answered: check the cable, the node's power and that this "
                           "computer is on the show network.");
        ImGui::TextWrapped("Still nothing? Windows Firewall may be blocking Psyight (a direct "
                           "cable counts as a public network): Windows Security > Firewall & "
                           "network protection > Allow an app through firewall > psyight.exe, "
                           "tick Public. The node's console (ST-LINK COM port, 115200) prints its "
                           "address.");
    } else {
        for (const ArtNode &n : *nodes_) {
            mono_text("%s  %s", ipv4_text(n.ip).c_str(), n.short_name.c_str());
        }
    }
}

void PreflightView::draw_test_patterns() {
    if (!ImGui::CollapsingHeader("Test patterns", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (!preview_.test_allowed()) {
        ImGui::TextDisabled("Test patterns run in Preflight or Preview (top bar).");
        return;
    }
    TestPattern t = preview_.test_pattern();
    // A segmented choice; clicking the one that is on turns it off.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, ImGui::GetStyle().ItemSpacing.y));
    bool first = true;
    for (const TestPatternKind kind :
         {TestPatternKind::chase, TestPatternKind::colour_order, TestPatternKind::identify_universe,
          TestPatternKind::all_on}) {
        if (!first) {
            ImGui::SameLine();
        }
        first = false;
        if (toggle_button(test_pattern_name(kind), t.kind == kind)) {
            t.kind = t.kind == kind ? TestPatternKind::off : kind;
        }
    }
    ImGui::PopStyleVar();

    const float em = ImGui::GetFontSize();
    const Project &p = project_.project();
    const std::shared_ptr<const ShowFrame> frame = preview_.latest_frame();
    const auto help = [](const char *text) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };
    switch (t.kind) {
    case TestPatternKind::off:
        help("Chase: each strip's direction and order. Colour order: catches a wrong RGB order. "
             "Identify universe: which pixels a universe reaches. All on: what full white draws "
             "through the limiters.");
        break;
    case TestPatternKind::chase: {
        ImGui::SetNextItemWidth(12.0f * em);
        const std::string shown = t.strip >= 0 && t.strip < static_cast<int>(p.strips.size())
                                      ? p.strips[static_cast<size_t>(t.strip)].spec.name
                                      : std::string("Every strip");
        if (ImGui::BeginCombo("##strip", shown.c_str())) {
            if (ImGui::Selectable("Every strip", t.strip < 0)) {
                t.strip = -1;
            }
            for (int i = 0; i < static_cast<int>(p.strips.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(p.strips[static_cast<size_t>(i)].spec.name.c_str(),
                                      t.strip == i)) {
                    t.strip = i;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(7.0f * em);
        drag_double("##speed", &t.chase_pixels_per_s, 0.5f, 1.0, 600.0, "%.0f px/s");
        help("A short segment runs from each strip's first pixel to its last, in the strip's own "
             "colour. Check each strip starts at the end you expect, and the order along the "
             "chain.");
        break;
    }
    case TestPatternKind::colour_order: {
        static constexpr const char *kNames[] = {"RED", "GREEN", "BLUE"};
        const ImVec4 colours[] = {ImVec4(1.0f, 0.25f, 0.25f, 1.0f), ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                                  ImVec4(0.35f, 0.5f, 1.0f, 1.0f)};
        const int step = frame != nullptr ? colour_order_step(frame->test_clock_s) : 0;
        ImGui::TextUnformatted("Every pixel should show");
        ImGui::SameLine();
        ImGui::PushFont(theme_font(ThemeFont::timecode));
        ImGui::TextColored(colours[step], "%s", kNames[step]);
        ImGui::PopFont();
        help("Red, green and blue in turn, a second each. A strip showing another colour has the "
             "wrong colour order: change its Order in the Rig tab.");
        break;
    }
    case TestPatternKind::identify_universe: {
        const std::vector<int> &universes = project_.patch().universes;
        if (!universes.empty() &&
            std::find(universes.begin(), universes.end(), t.universe) == universes.end()) {
            t.universe = universes.front();
        }
        ImGui::SetNextItemWidth(8.0f * em);
        if (ImGui::BeginCombo("Universe", std::to_string(t.universe).c_str())) {
            for (const int u : universes) {
                if (ImGui::Selectable(std::to_string(u).c_str(), u == t.universe)) {
                    t.universe = u;
                }
            }
            ImGui::EndCombo();
        }
        help("Only the pixels patched to this universe light, in cyan.");
        break;
    }
    case TestPatternKind::all_on:
        if (frame != nullptr) {
            mono_text("%.1f A estimated%s", frame->report.current_a,
                      frame->report.power_scale < 1.0 ? " (power limited)" : "");
        }
        help("White at full, through the brightness cap and the power budget: what the rig "
             "really draws. Mind the supply.");
        break;
    }
    if (t.kind != TestPatternKind::off && t.kind != TestPatternKind::all_on) {
        ImGui::SetNextItemWidth(7.0f * em);
        drag_double("Level", &t.level, 0.005f, 0.0, 1.0, "%.2f");
    }
    ImGui::Checkbox("Send to the lights", &t.send);
    ImGui::SetItemTooltip("Off: the preview (virtual node) only.");
    if (t.kind != TestPatternKind::off && t.send) {
        ImGui::SameLine();
        ImGui::TextColored(theme().danger, "Test pattern on the lights.");
    }
    preview_.set_test_pattern(t);
}

namespace {

constexpr int kNodeReplyTimeoutMs = 1000;
constexpr int kNodeSaveTimeoutMs = 4000; // The node pauses 1 to 2 s while its flash sector erases.

// Sends the commands in turn, stopping at the first that fails, then reads the settings back.
// Setting up asks the node's protocol first and leaves out what older firmware does not know.
NodeExchange exchange(std::array<std::uint8_t, 4> node, std::vector<std::string> commands) {
    NodeExchange out;
    Result<std::unique_ptr<UdpSocket>> socket = open_udp_socket();
    if (!socket) {
        out.error = socket.error().message;
        return out;
    }
    if (std::any_of(commands.begin(), commands.end(),
                    [](const std::string &c) { return c.rfind("set order", 0) == 0; })) {
        Result<std::string> first = node_request(*socket.value(), node, "get", kNodeReplyTimeoutMs);
        if (!first) {
            out.error = first.error().message;
            return out;
        }
        commands = commands_for_protocol(std::move(commands),
                                         node_protocol(parse_node_settings(first.value())));
    }
    for (const std::string &command : commands) {
        const int timeout = command == "save" ? kNodeSaveTimeoutMs : kNodeReplyTimeoutMs;
        Result<std::string> reply = node_request(*socket.value(), node, command, timeout);
        if (!reply) {
            out.error = reply.error().message;
            return out;
        }
        if (reply.value().rfind("error", 0) == 0) {
            out.failed_command = command;
            out.error = reply.value();
            while (!out.error.empty() && out.error.back() == '\n') {
                out.error.pop_back();
            }
            break; // Read back what the node has now.
        }
        ++out.done;
    }
    if (out.error.empty() && !commands.empty() && commands.back() == "reboot") {
        out.restarting = true; // Nothing to read until it is back.
        return out;
    }
    Result<std::string> settings = node_request(*socket.value(), node, "get", kNodeReplyTimeoutMs);
    if (!settings) {
        if (out.error.empty()) {
            out.error = settings.error().message;
        }
        return out;
    }
    out.settings = parse_node_settings(settings.value());
    out.read = true;
    return out;
}

} // namespace

void PreflightView::start_node_job(std::vector<std::string> commands, const char *what) {
    const std::optional<std::array<std::uint8_t, 4>> node = parse_ipv4(node_address_);
    if (!node) {
        node_status_ = "Type the node's address (a.b.c.d), or discover it first.";
        return;
    }
    node_job_what_ = what;
    node_status_.clear();
    node_job_ = std::async(std::launch::async, exchange, *node, std::move(commands));
}

void PreflightView::draw_node_setup() {
    if (!ImGui::CollapsingHeader("Node setup")) {
        return;
    }
    if (node_job_.valid() &&
        node_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        node_result_ = node_job_.get();
    }
    const Project &project = project_.project();
    const NodePlan plan = plan_node(project);
    if (node_address_.empty() && nodes_.has_value() && !nodes_->empty()) {
        node_address_ = ipv4_text(nodes_->front().ip); // The first node that answered.
    }
    const float em = ImGui::GetFontSize();
    ImGui::TextWrapped("This rig needs %d universes from %d, on %zu data line%s from the node "
                       "(each a chain of strips in patch order, at most %d pixels):",
                       plan.universes, plan.first_universe, plan.lanes.size(),
                       plan.lanes.size() == 1 ? "" : "s", kNodeLanePixels);
    if (ImGui::BeginTable("##lanes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Line");
        ImGui::TableSetupColumn("Strips", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pixels");
        ImGui::TableSetupColumn("Order");
        ImGui::TableHeadersRow();
        for (size_t l = 0; l < plan.lanes.size(); ++l) {
            const NodeLane &lane = plan.lanes[l];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            mono_text("%zu (PD%zu)", l + 1, l);
            ImGui::TableNextColumn();
            const std::string &first =
                project.strips[static_cast<size_t>(lane.first_strip)].spec.name;
            const std::string &last =
                project.strips[static_cast<size_t>(lane.first_strip + lane.strips - 1)].spec.name;
            if (lane.strips == 1) {
                ImGui::TextUnformatted(first.c_str());
            } else {
                ImGui::Text("%s to %s (%d strips)", first.c_str(), last.c_str(), lane.strips);
            }
            ImGui::TableNextColumn();
            mono_text("%d", lane.pixels);
            ImGui::TableNextColumn();
            mono_text("%s", colour_order_name(lane.order));
        }
        ImGui::EndTable();
    }
    for (const std::string &problem : plan.problems) {
        ImGui::TextColored(level_colour(CheckLevel::fail), "%s", problem.c_str());
    }
    for (const std::string &note : plan.notes) {
        ImGui::TextColored(level_colour(CheckLevel::warn), "%s", note.c_str());
    }

    // When the show's signal stops: saved with the project, set on the node with the rig.
    NodeFallback fallback = project.node;
    bool fallback_changed = false;
    ImGui::SetNextItemWidth(9.0f * em);
    if (ImGui::BeginCombo("When the signal stops", node_loss_policy_id(fallback.policy))) {
        for (const NodeLossPolicy p : kNodeLossPolicies) {
            if (ImGui::Selectable(node_loss_policy_id(p), p == fallback.policy)) {
                fallback.policy = p;
                fallback_changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The node's own behaviour without the show's data: hold the last frame, "
                          "fade it out, run its fallback show, or go dark.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(6.0f * em);
    fallback_changed |= drag_int("after (ms)", &fallback.loss_ms, 10.0f, 0, 600000, "%d");
    if (fallback.policy == NodeLossPolicy::fade) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(6.0f * em);
        fallback_changed |= drag_int("fade (ms)", &fallback.fade_ms, 10.0f, 0, 600000, "%d");
    }
    if (fallback.policy == NodeLossPolicy::fallback) {
        ImGui::SetNextItemWidth(6.0f * em);
        fallback_changed |=
            drag_double("Fallback brightness", &fallback.brightness, 0.005f, 0.0, 1.0, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(6.0f * em);
        fallback_changed |=
            drag_double("speed (Hz)", &fallback.speed_hz, 0.002f, 0.0, 10.0, "%.3f");
    }
    if (fallback_changed) {
        project_.edit().node = fallback;
        project_.changed();
    }

    // Which node.
    ImGui::SetNextItemWidth(9.0f * em);
    ImGui::InputTextWithHint("Node address", "a.b.c.d", &node_address_);
    if (nodes_.has_value() && !nodes_->empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(12.0f * em);
        if (ImGui::BeginCombo("##found", "Found nodes")) {
            for (const ArtNode &n : *nodes_) {
                const std::string label = ipv4_text(n.ip) + "  " + n.short_name;
                if (ImGui::Selectable(label.c_str())) {
                    node_address_ = ipv4_text(n.ip);
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("(or Discover Art-Net nodes, above)");
    }
    // The fallback show's colours.
    ImGui::Checkbox("Fallback show colours from", &node_send_palette_);
    ImGui::SetItemTooltip(
        "The node's own show when the signal is lost takes the first four colours "
        "of this palette.");
    ImGui::SameLine();
    node_palette_ =
        std::clamp(node_palette_, 0, std::max(0, static_cast<int>(project.palettes.size()) - 1));
    ImGui::BeginDisabled(!node_send_palette_ || project.palettes.empty());
    ImGui::SetNextItemWidth(10.0f * em);
    const char *palette_name =
        project.palettes.empty()
            ? "(none)"
            : project.palettes[static_cast<size_t>(node_palette_)].name.c_str();
    if (ImGui::BeginCombo("##palette", palette_name)) {
        for (size_t i = 0; i < project.palettes.size(); ++i) {
            if (ImGui::Selectable(project.palettes[i].name.c_str(),
                                  static_cast<int>(i) == node_palette_)) {
                node_palette_ = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    // Actions.
    const bool busy = node_job_.valid();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Read the node")) {
        start_node_job({}, "read");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!plan.problems.empty());
    if (primary_button("Set up the node")) {
        const Palette *palette = node_send_palette_ && !project.palettes.empty()
                                     ? &project.palettes[static_cast<size_t>(node_palette_)]
                                     : nullptr;
        start_node_job(node_commands(plan, palette), "set up");
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Sends this rig's universes, lines, outputs and colour orders, what to "
                          "do when the signal stops (and the colours), and applies them at once. "
                          "The node forgets them on a restart until saved.");
    ImGui::SameLine();
    if (ImGui::Button("Save on the node")) {
        start_node_job({"save"}, "save");
    }
    ImGui::SetItemTooltip("Keeps the node's settings after a restart. Its output pauses for 1 to "
                          "2 seconds while it writes them.");
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("Talking to the node...");
    }

    // What the node said.
    if (!node_status_.empty()) {
        ImGui::TextColored(level_colour(CheckLevel::warn), "%s", node_status_.c_str());
    }
    if (!node_result_.has_value() || busy) {
        return;
    }
    const NodeExchange &r = *node_result_;
    if (r.restarting) {
        ImGui::TextColored(level_colour(CheckLevel::ok),
                           "Saved; the node is restarting. Read it again in a few seconds.");
        return;
    }
    const bool old_firmware = r.read && node_protocol(r.settings) < kNodeProtocol;
    if (!r.error.empty() && !(old_firmware && r.failed_command == "apply")) {
        ImGui::TextColored(level_colour(CheckLevel::fail), "Node: %s", r.error.c_str());
    }
    if (!r.read) {
        return;
    }
    const auto name = r.settings.find("name");
    const std::vector<std::string> differences = node_differences(plan, r.settings);
    if (differences.empty()) {
        ImGui::TextColored(level_colour(CheckLevel::ok), "%s matches this rig%s.",
                           name != r.settings.end() ? name->second.c_str() : "The node",
                           node_job_what_ == "save" ? ", saved" : "");
    } else {
        ImGui::TextColored(level_colour(CheckLevel::warn), "%s does not match this rig:",
                           name != r.settings.end() ? name->second.c_str() : "The node");
        for (const std::string &d : differences) {
            ImGui::BulletText("%s", d.c_str());
        }
    }
    if (old_firmware) {
        ImGui::TextColored(level_colour(CheckLevel::warn),
                           "Its firmware is older (protocol %d): the settings take effect after "
                           "Save on the node and a restart. Updating the firmware applies them at "
                           "once.",
                           node_protocol(r.settings));
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Save and restart the node")) {
            start_node_job({"save", "reboot"}, "save");
        }
        ImGui::EndDisabled();
    }
}

} // namespace psyight
