#include "gui/preflight_view.h"

#include "gui/theme.h"
#include "output/discovery.h"
#include "output/preflight.h"

#include <imgui.h>

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

// Polls every interface's subnet from one socket on Art-Net's port (nodes reply there).
Result<std::vector<ArtNode>> discover(std::vector<NetworkInterface> interfaces) {
    Result<std::unique_ptr<UdpSocket>> socket = open_udp_socket({}, kArtNetPortNumber);
    if (!socket) {
        return socket.error();
    }
    std::vector<ArtNode> all;
    for (const NetworkInterface &i : interfaces) {
        Result<std::vector<ArtNode>> found =
            discover_art_nodes(*socket.value(), {i.broadcast(), kArtNetPortNumber},
                               kDiscoveryTimeoutMs / static_cast<int>(interfaces.size()));
        if (!found) {
            return found.error();
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
    return all;
}

} // namespace

void PreflightView::draw() {
    ImGui::Begin("Preflight");
    const auto now = std::chrono::steady_clock::now();
    if (now - interfaces_at_ > kInterfaceRefresh) {
        interfaces_ = list_ipv4_interfaces(); // A few ms; not every frame.
        interfaces_at_ = now;
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
    const std::vector<PreflightCheck> checks = run_preflight(in);

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

} // namespace psyight
