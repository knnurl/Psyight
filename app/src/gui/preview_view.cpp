#include "gui/preview_view.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace psyight {

namespace {

constexpr float kMarginPixels = 12.0f;
constexpr ImU32 kBackground = IM_COL32(8, 8, 12, 255);
const ImVec4 kWarning(1.0f, 0.45f, 0.35f, 1.0f);
constexpr double kRateWindowS = 1.0;

// Node bytes are LED drive levels (gamma applied); the screen wants the frame's values back.
ImU32 to_imgui(const std::uint8_t *rgb, double gamma) {
    const auto channel = [gamma](std::uint8_t b) {
        return static_cast<int>(std::lround(255.0 * std::pow(b / 255.0, 1.0 / gamma)));
    };
    return IM_COL32(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]), 255);
}

} // namespace

PreviewView::PreviewView(Session &session, ProjectState &project)
    : session_(session), project_(project),
      show_([&session] { return session.transport().position(); }) {}

void PreviewView::sync_config() {
    std::shared_ptr<const TrackAnalysis> analysis =
        session_.has_track() ? session_.track().track_analysis : nullptr;
    const Grid grid = session_.grid();
    const int sample_rate_hz = session_.has_track() ? session_.track().sample_rate_hz : 48000;
    dirty_ |= analysis != config_.analysis || grid.bpm != config_.grid.bpm ||
              grid.first_downbeat != config_.grid.first_downbeat ||
              sample_rate_hz != config_.sample_rate_hz || project_.version() != project_version_;
    if (!dirty_) {
        return;
    }
    config_.analysis = std::move(analysis);
    config_.grid = grid;
    config_.sample_rate_hz = sample_rate_hz;
    const Project &p = project_.project();
    config_.rig = project_.rig();
    config_.patch = project_.patch();
    config_.output = p.output;
    config_.limiter = p.limiter;
    config_.latency_offset_s = 1e-3 * p.latency_offset_ms;
    config_.automation = p.automation;
    project_version_ = project_.version();
    config_.version = ++version_;
    show_.set_config(std::make_shared<const ShowConfig>(config_));
    dirty_ = false;
}

void PreviewView::draw() {
    sync_config();
    ImGui::Begin("Preview");
    const std::shared_ptr<const ShowFrame> frame = show_.latest();
    if (!session_.has_track() || session_.track().track_analysis == nullptr || frame == nullptr) {
        ImGui::TextUnformatted(session_.has_track() ? "No analysis (no tempo found)."
                                                    : "No track loaded.");
        ImGui::End();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const double since_s = std::chrono::duration<double>(now - rate_since_).count();
    if (rate_index_ < 0 || since_s >= kRateWindowS) {
        if (rate_index_ >= 0) {
            measured_fps_ = static_cast<double>(frame->index - rate_index_) / since_s;
        }
        rate_index_ = frame->index;
        rate_since_ = now;
    }

    draw_controls(*frame);
    ImGui::Text("%s, beat %d of the bar, %.1f A estimated%s", frame->section.c_str(),
                frame->beat_in_bar + 1, frame->report.current_a,
                frame->report.power_scale < 1.0 ? " (power limited)" : "");
    draw_rig(*frame);
    ImGui::End();
}

void PreviewView::draw_controls(const ShowFrame &frame) {
    const float em = ImGui::GetFontSize();
    draw_scene_cards(frame);
    edit_scene_ = std::clamp(edit_scene_, 0, static_cast<int>(config_.scenes.size()) - 1);
    Scene &scene = config_.scenes[static_cast<size_t>(edit_scene_)];
    ImGui::SetNextItemWidth(7.0f * em);
    float latency_ms = static_cast<float>(project_.project().latency_offset_ms);
    if (ImGui::DragFloat("Latency offset (ms)", &latency_ms, 1.0f, -200.0f, 200.0f, "%.0f")) {
        project_.edit().latency_offset_ms = latency_ms;
        project_.changed();
    }
    ImGui::SetItemTooltip("Render time minus playhead. About 2.9 ms per metre from the speakers "
                          "(SPEC 4.3).");
    const std::string layers_label = "Layers and palette of " + scene.name + "###layers";
    if (ImGui::TreeNode(layers_label.c_str())) {
        ImGui::SetNextItemWidth(9.0f * em);
        if (ImGui::BeginCombo("Palette (base)",
                              config_.palettes[static_cast<size_t>(scene.palette)].name.c_str())) {
            for (int i = 0; i < static_cast<int>(config_.palettes.size()); ++i) {
                if (ImGui::Selectable(config_.palettes[static_cast<size_t>(i)].name.c_str(),
                                      i == scene.palette)) {
                    scene.palette = i;
                    dirty_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Automation shifts it by one every %d phrases.",
                              config_.automation.palette_every_phrases);
        for (size_t i = 0; i < scene.layers.size(); ++i) {
            Layer &layer = scene.layers[i];
            ImGui::PushID(static_cast<int>(i));
            dirty_ |= ImGui::Checkbox(layer.effect->name(), &layer.enabled);
            ImGui::SameLine(14.0f * em);
            ImGui::SetNextItemWidth(8.0f * em);
            dirty_ |= ImGui::SliderFloat("opacity", &layer.opacity, 0.0f, 1.0f, "%.2f");
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Limiters")) {
        LimiterParams p = project_.project().limiter;
        bool changed = false;
        float cap = static_cast<float>(p.brightness_cap);
        ImGui::SetNextItemWidth(8.0f * em);
        if (ImGui::SliderFloat("Brightness cap", &cap, 0.0f, 1.0f, "%.2f")) {
            p.brightness_cap = cap;
            changed = true;
        }
        float budget = static_cast<float>(p.budget_a);
        ImGui::SetNextItemWidth(8.0f * em);
        if (ImGui::DragFloat("Power budget (A, 0: off)", &budget, 0.1f, 0.0f, 100.0f, "%.1f")) {
            p.budget_a = budget;
            changed = true;
        }
        changed |= ImGui::Checkbox("Flash limiter (3 per second)", &p.flash_limiter);
        if (changed) {
            project_.edit().limiter = p;
            project_.changed();
        }
        if (!p.flash_limiter) {
            ImGui::TextColored(kWarning, "Flash limiter off: photosensitive epilepsy risk "
                                         "(SPEC 7.7).");
        }
        ImGui::Text("Flashes held back so far: %lld", static_cast<long long>(frame.flashes_limited));
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Output")) {
        draw_output(frame);
        ImGui::TreePop();
    }
}

void PreviewView::draw_scene_cards(const ShowFrame &frame) {
    const ImGuiStyle &style = ImGui::GetStyle();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const bool manual = config_.manual_scene >= 0;
    for (int i = 0; i < static_cast<int>(config_.scenes.size()); ++i) {
        const std::string &name = config_.scenes[static_cast<size_t>(i)].name;
        const float width = ImGui::CalcTextSize(name.c_str()).x + 2.0f * style.FramePadding.x;
        if (i > 0 && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width < right) {
            ImGui::SameLine();
        }
        const bool playing = frame.mix.to == i;
        const bool fading_out = frame.mix.from == i;
        if (playing || fading_out) {
            ImVec4 colour = style.Colors[ImGuiCol_ButtonActive];
            colour.w = playing ? 1.0f : 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Button, colour);
        }
        ImGui::PushID(i);
        if (ImGui::Button(name.c_str())) {
            config_.manual_scene = i;
            edit_scene_ = i;
            dirty_ = true;
        }
        ImGui::PopID();
        if (playing || fading_out) {
            ImGui::PopStyleColor();
        }
        std::string tags;
        for (const std::string &s : config_.scenes[static_cast<size_t>(i)].sections) {
            tags += (tags.empty() ? "" : ", ") + s;
        }
        ImGui::SetItemTooltip("For: %s. Click to hold this scene.",
                              tags.empty() ? "manual use only" : tags.c_str());
    }
    bool automatic = !manual;
    if (ImGui::Checkbox("Auto (by section)", &automatic)) {
        config_.manual_scene = automatic ? -1 : std::max(0, frame.mix.to);
        dirty_ = true;
    }
    ImGui::SetItemTooltip("Scenes follow the sections; clicking a scene holds it until this is "
                          "ticked again.");
    if (frame.mix.from >= 0) {
        ImGui::SameLine();
        ImGui::Text("crossfade %.0f%%", 100.0 * frame.mix.to_weight);
    }
    if (frame.mix.palette_shift != 0) {
        ImGui::SameLine();
        ImGui::Text("palette +%d", frame.mix.palette_shift);
    }
}

void PreviewView::draw_output(const ShowFrame &frame) {
    const float em = ImGui::GetFontSize();
    OutputSettings o = project_.project().output;
    int first_universe = project_.project().first_universe;
    bool changed = false;
    dirty_ |= ImGui::Checkbox("Send to network", &config_.send_to_network);
    ImGui::SetItemTooltip("Off: packets go to the virtual node (this preview) only.");
    int protocol = o.protocol == OutputProtocol::sacn ? 0 : 1;
    ImGui::SetNextItemWidth(8.5f * em);
    if (ImGui::Combo("Protocol", &protocol, "sACN (E1.31)\0Art-Net 4\0")) {
        o.protocol = protocol == 0 ? OutputProtocol::sacn : OutputProtocol::art_net;
        changed = true;
    }
    if (o.protocol == OutputProtocol::sacn) {
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Multicast", &o.multicast);
        ImGui::SetItemTooltip("239.255.hi.lo per universe. Switches need IGMP snooping, or "
                              "multicast floods the network (SPEC 8).");
    }
    if (o.protocol == OutputProtocol::art_net || !o.multicast) {
        ImGui::SetNextItemWidth(8.0f * em);
        if (!address_active_ && parse_ipv4(address_text_) != o.address) {
            address_text_ = std::to_string(o.address[0]) + "." + std::to_string(o.address[1]) +
                            "." + std::to_string(o.address[2]) + "." +
                            std::to_string(o.address[3]); // Loaded or reset elsewhere.
        }
        if (ImGui::InputText("Node address", &address_text_)) {
            if (const auto address = parse_ipv4(address_text_)) {
                o.address = *address;
                changed = true;
            }
        }
        address_active_ = ImGui::IsItemActive();
        if (!parse_ipv4(address_text_)) {
            ImGui::SameLine();
            ImGui::TextColored(kWarning, "not an IPv4 address");
        }
    }
    ImGui::SetNextItemWidth(6.0f * em);
    if (ImGui::InputInt("First universe", &first_universe)) {
        first_universe = std::clamp(first_universe, 1, 63000);
        changed = true;
    }
    changed |= ImGui::Checkbox("Sync", &o.sync);
    if (o.sync && o.protocol == OutputProtocol::sacn) {
        ImGui::SameLine();
        int sync_universe = o.sync_universe;
        ImGui::SetNextItemWidth(6.0f * em);
        if (ImGui::InputInt("Sync universe", &sync_universe)) {
            o.sync_universe = static_cast<std::uint16_t>(std::clamp(sync_universe, 1, 63999));
            changed = true;
        }
    }
    if (changed) {
        Project &p = project_.edit();
        p.output = o;
        p.first_universe = first_universe;
        project_.changed();
    }
    const std::vector<int> &universes = project_.patch().universes;
    if (o.protocol == OutputProtocol::sacn && o.sync &&
        std::find(universes.begin(), universes.end(), o.sync_universe) != universes.end()) {
        ImGui::TextColored(kWarning, "The sync universe carries pixel data.");
    }
    ImGui::Text("Universes %d to %d, %d packets a frame, %.1f fps",
                universes.empty() ? 0 : universes.front(), universes.empty() ? 0 : universes.back(),
                frame.packets_sent, measured_fps_);
    ImGui::Text("Frame cost %.2f ms, %lld late frames", frame.render_ms,
                static_cast<long long>(frame.late_frames));
    ImGui::SetItemTooltip("Render, limiters, patch and send, measured on the show thread.");
    ImGui::Text("Node: %lld packets, %lld latched, %lld gaps, %lld bad",
                static_cast<long long>(frame.node.packets),
                static_cast<long long>(frame.node.latches),
                static_cast<long long>(frame.node.sequence_gaps),
                static_cast<long long>(frame.node.bad_packets));
    if (frame.send_errors > 0 || !frame.network_error.empty()) {
        ImGui::TextColored(kWarning, "Network: %lld send errors. %s",
                           static_cast<long long>(frame.send_errors),
                           frame.network_error.c_str());
    }
}

void PreviewView::draw_rig(const ShowFrame &frame) {
    const Rig &rig = config_.rig;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 2.0f * kMarginPixels || size.y < 2.0f * kMarginPixels) {
        return;
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), kBackground);
    const double width_m = std::max(1e-3, rig.max.x - rig.min.x);
    const double height_m = std::max(1e-3, rig.max.y - rig.min.y);
    const double scale = std::min((size.x - 2.0f * kMarginPixels) / width_m,
                                  (size.y - 2.0f * kMarginPixels) / height_m);
    const float radius = std::max(1.5f, static_cast<float>(0.45 * scale / 60.0));
    const float left = origin.x + 0.5f * (size.x - static_cast<float>(width_m * scale));
    const float bottom = origin.y + 0.5f * (size.y + static_cast<float>(height_m * scale));
    for (size_t i = 0; i < rig.pixels.size() && 3 * i + 2 < frame.node_rgb.size(); ++i) {
        const Vec3 &q = rig.pixels[i].position;
        const ImVec2 at(left + static_cast<float>((q.x - rig.min.x) * scale),
                        bottom - static_cast<float>((q.y - rig.min.y) * scale)); // y up.
        draw->AddCircleFilled(at, radius, to_imgui(&frame.node_rgb[3 * i], config_.limiter.gamma));
    }
    ImGui::Dummy(size);
}

} // namespace psyight
