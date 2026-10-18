#include "gui/rig_view.h"

#include "core/paths.h"
#include "gui/theme.h"
#include "gui/tier.h"
#include "gui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace psyight {

namespace {

constexpr int kPixelBudget = 1500; // SPEC 8: about 9 universes.
constexpr std::array<ColourOrder, 6> kOrders = {ColourOrder::rgb, ColourOrder::grb,
                                                ColourOrder::brg, ColourOrder::rbg,
                                                ColourOrder::gbr, ColourOrder::bgr};

std::string groups_text(const std::vector<std::string> &groups) {
    std::string out;
    for (const std::string &g : groups) {
        out += (out.empty() ? "" : ", ") + g;
    }
    return out;
}

std::vector<std::string> parse_groups(const std::string &text) {
    std::vector<std::string> out;
    std::string current;
    const auto flush = [&] {
        const size_t a = current.find_first_not_of(' ');
        const size_t b = current.find_last_not_of(' ');
        if (a != std::string::npos) {
            out.push_back(current.substr(a, b - a + 1));
        }
        current.clear();
    };
    for (const char c : text) {
        if (c == ',') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return out;
}

} // namespace

void RigView::draw() {
    ImGui::Begin("Rig");
    if (texts_version_ != project_.version()) {
        polyline_texts_.clear();
        group_texts_.clear();
        for (const ProjectStrip &s : project_.project().strips) {
            polyline_texts_.push_back(polyline_text(s.spec.polyline));
            group_texts_.push_back(groups_text(s.spec.groups));
        }
        path_text_ = path_to_utf8(project_.path());
        texts_version_ = project_.version();
    }
    const bool engineer = tier_at_least(Tier::engineer);
    if (!engineer) {
        ImGui::TextDisabled("Editing the rig needs the Engineer tier (top bar).");
    }
    ImGui::BeginDisabled(!engineer);
    draw_project();
    ImGui::Separator();
    draw_strips();
    ImGui::Separator();
    draw_patch();
    ImGui::EndDisabled();
    ImGui::End();
}

void RigView::draw_project() {
    const float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(14.0f * em);
    if (ImGui::InputText("Project file", &path_text_)) {
        const std::u8string text(path_text_.begin(), path_text_.end());
        project_.path() = std::filesystem::path(text);
    }
    if (ImGui::Button("Load")) {
        (void)project_.load(); // The status line shows the outcome.
    }
    ImGui::SameLine();
    if (primary_button(project_.dirty() ? "Save *###save" : "Save###save")) {
        (void)project_.save();
    }
    ImGui::SetItemTooltip("Also saved on exit when changed.");
    ImGui::SameLine();
    if (ImGui::Button("Default frame")) {
        project_.reset_to_default();
    }
    ImGui::SetItemTooltip("Replace everything with the 2.5 by 1.2 m test frame and default "
                          "settings.");
    if (!project_.status().empty()) {
        ImGui::TextWrapped("%s", project_.status().c_str());
    }
}

void RigView::draw_strips() {
    const float em = ImGui::GetFontSize();
    std::vector<ProjectStrip> &strips = project_.edit().strips;
    bool changed = false;
    int remove = -1;
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
    // Horizontal scrolling needs a fixed height: the rows, the header and the scrollbar.
    const float height =
        std::min(ImGui::GetContentRegionAvail().y * 0.6f,
                 (static_cast<float>(strips.size()) + 2.5f) * ImGui::GetFrameHeightWithSpacing());
    if (ImGui::BeginTable("strips", 7, flags, ImVec2(0.0f, height))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 4.5f * em);
        ImGui::TableSetupColumn("Points (x,y[,z] m)", ImGuiTableColumnFlags_WidthFixed, 10.0f * em);
        ImGui::TableSetupColumn("Per m", ImGuiTableColumnFlags_WidthFixed, 2.5f * em);
        ImGui::TableSetupColumn("Pixels", ImGuiTableColumnFlags_WidthFixed, 3.0f * em);
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 2.5f * em);
        ImGui::TableSetupColumn("Groups", ImGuiTableColumnFlags_WidthFixed, 5.0f * em);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 1.0f * em);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < strips.size() && i < polyline_texts_.size(); ++i) {
            ProjectStrip &s = strips[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::InputText("##name", &s.spec.name);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            const auto points = parse_polyline_text(polyline_texts_[i]);
            if (!points) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme().warning);
            }
            if (ImGui::InputText("##points", &polyline_texts_[i])) {
                if (const auto edited = parse_polyline_text(polyline_texts_[i])) {
                    s.spec.polyline = *edited;
                    changed = true;
                }
            }
            if (!points) {
                ImGui::PopStyleColor();
            }
            ImGui::SetItemTooltip(
                "Points separated by spaces, coordinates by commas, in metres; "
                "z may be left out. Pixels start half a pitch in from the first.");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            float density = static_cast<float>(s.spec.pixels_per_m);
            if (mono_field("##ppm", [&](const char *id) {
                    return ImGui::DragFloat(id, &density, 1.0f, 1.0f, 1000.0f, "%.0f");
                })) {
                s.spec.pixels_per_m = std::clamp(static_cast<double>(density), 1.0, 1000.0);
                changed = true;
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (mono_field("##count", [&](const char *id) {
                    return ImGui::DragInt(id, &s.spec.count, 1.0f, 0, 10000);
                })) {
                s.spec.count = std::clamp(s.spec.count, 0, 10000);
                changed = true;
            }
            const int fill = pixels_to_fill(s.spec);
            ImGui::SetItemTooltip("%d fill the polyline at this density. Double-click to type.",
                                  fill);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##order", colour_order_name(s.order),
                                  ImGuiComboFlags_NoArrowButton)) {
                for (const ColourOrder order : kOrders) {
                    if (ImGui::Selectable(colour_order_name(order), order == s.order)) {
                        s.order = order;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##groups", &group_texts_[i])) {
                s.spec.groups = parse_groups(group_texts_[i]);
                changed = true;
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) {
                remove = static_cast<int>(i);
            }
            ImGui::SetItemTooltip("Remove this strip.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Add strip")) {
        const Rig &rig = project_.rig();
        const double y = rig.pixels.empty() ? 0.0 : rig.max.y + 0.2;
        strips.push_back({{"strip " + std::to_string(strips.size() + 1),
                           {{0.0, y, 0.0}, {1.0, y, 0.0}},
                           60.0,
                           60,
                           {}},
                          ColourOrder::grb});
        polyline_texts_.push_back(polyline_text(strips.back().spec.polyline));
        group_texts_.emplace_back();
        changed = true;
    }
    if (remove >= 0) {
        strips.erase(strips.begin() + remove);
        polyline_texts_.erase(polyline_texts_.begin() + remove);
        group_texts_.erase(group_texts_.begin() + remove);
        changed = true;
    }
    if (changed) {
        project_.changed();
        texts_version_ = project_.version(); // Keep the texts as typed.
    }
    for (const ProjectStrip &s : project_.project().strips) {
        const int fill = pixels_to_fill(s.spec);
        if (s.spec.count > fill + 1) {
            ImGui::TextColored(theme().warning, "%s: %d pixels run %.2f m past its polyline.",
                               s.spec.name.c_str(), s.spec.count,
                               (s.spec.count - fill) / s.spec.pixels_per_m);
        }
    }
}

void RigView::draw_patch() {
    const float em = ImGui::GetFontSize();
    const Project &p = project_.project();
    const Rig &rig = project_.rig();
    const Patch &patch = project_.patch();
    int per_universe = p.pixels_per_universe;
    ImGui::SetNextItemWidth(6.0f * em);
    // Typed or stepped, not dragged: a drag would sweep the live patch through every value.
    if (mono_field("Pixels per universe",
                   [&](const char *id) { return ImGui::InputInt(id, &per_universe); })) {
        project_.edit().pixels_per_universe = std::clamp(per_universe, 1, 170);
        project_.changed();
    }
    ImGui::SetItemTooltip("170 RGB pixels use 510 of a universe's 512 channels (SPEC 8).");
    const auto pixels = static_cast<int>(rig.pixels.size());
    mono_text("%d pixels in %zu strips, universes %d to %d, %.1f by %.1f by %.1f m", pixels,
              p.strips.size(), patch.universes.empty() ? 0 : patch.universes.front(),
              patch.universes.empty() ? 0 : patch.universes.back(), rig.max.x - rig.min.x,
              rig.max.y - rig.min.y, rig.max.z - rig.min.z);
    if (pixels > kPixelBudget) {
        ImGui::TextColored(theme().warning, "Over the 1,500-pixel budget (SPEC 2.1).");
    }
    const double full_white_a = pixels * 3 * p.limiter.current_per_channel_ma * 1e-3;
    mono_text("Full white: %.1f A estimated; power budget %.1f A%s", full_white_a,
              p.limiter.budget_a, p.limiter.budget_a <= 0.0 ? " (off)" : "");
    // Where each strip lands.
    if (ImGui::TreeNode("Patch by strip")) {
        size_t first = 0;
        for (size_t s = 0; s < p.strips.size(); ++s) {
            const auto count = static_cast<size_t>(std::max(0, p.strips[s].spec.count));
            if (count == 0 || first + count > patch.pixels.size()) {
                first += count;
                continue;
            }
            const PatchedPixel &a = patch.pixels[first];
            const PatchedPixel &b = patch.pixels[first + count - 1];
            mono_text("%s: universe %d channel %d to universe %d channel %d (%s)",
                      p.strips[s].spec.name.c_str(),
                      patch.universes[static_cast<size_t>(a.universe_slot)], a.channel + 1,
                      patch.universes[static_cast<size_t>(b.universe_slot)], b.channel + 3,
                      colour_order_name(p.strips[s].order));
            first += count;
        }
        ImGui::TreePop();
    }
}

} // namespace psyight
