#include "gui/rig_view.h"

#include "core/paths.h"
#include "gui/file_dialog.h"
#include "gui/theme.h"
#include "gui/tier.h"
#include "gui/widgets.h"
#include "io/build_kit.h"
#include "io/dxf_rig.h"
#include "io/presets.h"
#include "io/rig_export.h"
#include "output/node_setup.h"

#include <imgui.h>
#include <imgui_internal.h> // TableGetHoveredRow.
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace psyight {

namespace {

// The strip to buy: each strip's pixels at its density.
double strip_length_m(const std::vector<StripSpec> &strips) {
    double total = 0.0;
    for (const StripSpec &s : strips) {
        total += s.pixels_per_m > 0.0 ? s.count / s.pixels_per_m : 0.0;
    }
    return total;
}

// Each strip's node line and pin, as Patch by strip shows them (the export's wiring labels).
std::vector<std::string> wiring_notes(const Project &p) {
    std::vector<std::string> notes(p.strips.size());
    const NodePlan plan = plan_node(p);
    for (size_t l = 0; l < plan.lanes.size(); ++l) {
        const NodeLane &lane = plan.lanes[l];
        for (int k = 0; k < lane.strips; ++k) {
            const auto s = static_cast<size_t>(lane.first_strip + k);
            if (s < notes.size()) {
                notes[s] =
                    "line " + std::to_string(l + 1) + ", " + node_lane_pin(static_cast<int>(l));
            }
        }
    }
    return notes;
}

std::string lower_extension(const std::filesystem::path &path) {
    std::string ext = path_to_utf8(path.extension());
    for (char &ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext;
}

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
        texts_version_ = project_.version();
    }
    const bool engineer = tier_at_least(Tier::engineer);
    if (!engineer) {
        ImGui::TextDisabled("Editing the rig needs the Engineer tier (top bar).");
    }
    ImGui::BeginDisabled(!engineer);
    draw_project();
    draw_templates();
    // The rig as it is: its strips (the data line's order) and patch, the summary in the title.
    const Project &p = project_.project();
    std::vector<StripSpec> specs;
    for (const ProjectStrip &strip : p.strips) {
        specs.push_back(strip.spec);
    }
    char title[128];
    std::snprintf(title, sizeof(title),
                  "Current rig: %zu strips, %zu pixels, %.2f m of strip###current", p.strips.size(),
                  project_.rig().pixels.size(), strip_length_m(specs));
    if (ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_current_settings();
        ImGui::Separator();
        draw_strips();
        ImGui::Separator();
        draw_patch();
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void RigView::draw_project() {
    // The project: its name, and the File menu's commands (gui/file_flow.h carries them out,
    // asking first when there are unsaved changes).
    ImGui::Text("Project: %s%s", project_.name().c_str(), project_.dirty() ? " (unsaved)" : "");
    ImGui::SetItemTooltip("%s", project_.untitled() ? "Not saved to a file yet."
                                                    : path_to_utf8(project_.path()).c_str());
    if (ImGui::Button("New")) {
        project_.request(ProjectRequest::new_project);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        project_.request(ProjectRequest::open);
    }
    ImGui::SameLine();
    if (primary_button(project_.dirty() ? "Save *###save" : "Save###save")) {
        project_.request(project_.untitled() ? ProjectRequest::save_as : ProjectRequest::save);
    }
    ImGui::SetItemTooltip("Ctrl+S. The project changes on disk only when you save.");
    ImGui::SameLine();
    if (ImGui::Button("Save as...")) {
        project_.request(ProjectRequest::save_as);
    }
    ImGui::SameLine();
    if (ImGui::Button("Default frame")) {
        project_.reset_to_default();
    }
    ImGui::SetItemTooltip("Replace everything with the 2.5 by 1.2 m test frame and default "
                          "settings.");
    ImGui::SameLine();
    draw_export();
    if (!export_status_.empty()) {
        ImGui::TextDisabled("%s", export_status_.c_str());
    }
    if (!project_.status().empty()) {
        ImGui::TextWrapped("%s", project_.status().c_str());
    }
}

void RigView::draw_templates() {
    if (ImGui::TreeNode("New rig")) {
        draw_template_controls();
        ImGui::TreePop();
    }
}

void RigView::draw_template_controls() {
    // Four sources at one level: templates made from a few numbers, the built-in presets, the
    // rigs saved by the user, and a drawing. Each gives strips; the summary and Replace below are
    // shared.
    std::vector<StripSpec> strips;
    RigSource source; // The preset, when the Presets tab makes the strips.
    bool mine = false;
    // Tabs inside a panel: the chosen one lighter with an accent line, the others blending in
    // (the theme's tab colours are for docked windows, whose chosen tab merges into the window).
    const ThemeTokens &t = theme();
    ImGui::PushStyleColor(ImGuiCol_Tab, t.panel);
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, t.panel);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, t.frame_hover);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, t.frame_hover);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, t.accent);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, t.accent);
    if (ImGui::BeginTabBar("##rig source", ImGuiTabBarFlags_DrawSelectedOverline)) {
        if (ImGui::BeginTabItem("Templates")) {
            draw_generators(strips);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Presets")) {
            draw_presets(strips);
            const RigPreset &preset = presets_[static_cast<size_t>(preset_)];
            source.preset = preset.name;
            for (size_t k = 0; k < preset.params.size(); ++k) {
                source.values[preset.params[k].id] =
                    preset_values_[static_cast<size_t>(preset_)][k];
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("My rigs")) {
            mine = true;
            draw_user_rigs(strips);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("From drawing")) {
            draw_svg(strips);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor(6); // Read as each tab is drawn: kept until the bar ends.
    if (!strips.empty()) {
        draw_replace(std::move(strips), source);
    }
    if (mine) {
        draw_save_rig();
    }
}

void RigView::draw_generators(std::vector<StripSpec> &strips) {
    const float em = ImGui::GetFontSize();
    const float field = 7.0f * em;
    ImGui::RadioButton("Pillars", &generator_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Matrix panel", &generator_, 1);
    if (generator_ == 0) {
        PillarsTemplate &t = pillars_;
        ImGui::SetNextItemWidth(field);
        drag_int("Pillars", &t.count, 0.1f, 1, kMaxTemplateStrips);
        ImGui::SetNextItemWidth(field);
        drag_double("Height", &t.height_m, 0.01f, 0.05, 20.0, "%.2f m");
        ImGui::SetNextItemWidth(field);
        drag_double("First pixel above the floor", &t.base_m, 0.01f, 0.0, 20.0, "%.2f m");
        ImGui::Checkbox("Around a circle", &t.circle);
        ImGui::SetNextItemWidth(field);
        if (t.circle) {
            drag_double("Radius", &t.radius_m, 0.01f, 0.1, 50.0, "%.2f m");
        } else {
            drag_double("Spacing", &t.spacing_m, 0.01f, 0.05, 50.0, "%.2f m");
        }
        ImGui::SetNextItemWidth(field);
        drag_double("Density", &t.pixels_per_m, 1.0f, 1.0, 1000.0, "%.0f px/m");
        ImGui::Checkbox("Serpentine (alternate pillars run top to bottom)", &t.serpentine);
        strips = pillars_strips(t);
    } else {
        MatrixTemplate &t = matrix_;
        ImGui::SetNextItemWidth(field);
        drag_int("Rows", &t.rows, 0.1f, 1, kMaxTemplateStrips);
        ImGui::SetNextItemWidth(field);
        drag_double("Width", &t.width_m, 0.01f, 0.05, 50.0, "%.2f m");
        ImGui::SetNextItemWidth(field);
        drag_double("Row spacing", &t.row_spacing_m, 0.001f, 0.005, 5.0, "%.3f m");
        ImGui::SetNextItemWidth(field);
        drag_double("Bottom row above the floor", &t.base_m, 0.01f, 0.0, 20.0, "%.2f m");
        ImGui::SetNextItemWidth(field);
        drag_double("Density", &t.pixels_per_m, 1.0f, 1.0, 1000.0, "%.0f px/m");
        ImGui::Checkbox("Serpentine (alternate rows run right to left)", &t.serpentine);
        strips = matrix_strips(t);
    }
}

// A list of rigs with their category headings (consecutive rigs of one category share one);
// hovering a name shows its description. Returns the chosen index.
int RigView::rig_list(const char *id, const std::vector<RigPreset> &rigs, int chosen) {
    chosen = std::clamp(chosen, 0, static_cast<int>(rigs.size()) - 1);
    const float rows = std::min(static_cast<float>(rigs.size()) + 4.0f, 12.0f);
    if (ImGui::BeginListBox(id, ImVec2(-FLT_MIN, rows * ImGui::GetTextLineHeightWithSpacing()))) {
        for (size_t i = 0; i < rigs.size(); ++i) {
            if (!rigs[i].category.empty() && (i == 0 || rigs[i].category != rigs[i - 1].category)) {
                ImGui::SeparatorText(rigs[i].category.c_str());
            }
            const bool selected = static_cast<int>(i) == chosen;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(rigs[i].name.c_str(), selected)) {
                chosen = static_cast<int>(i);
            }
            ImGui::SetItemTooltip("%s", rigs[i].description.c_str());
            if (selected && ImGui::IsWindowAppearing()) {
                ImGui::SetScrollHereY();
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    return chosen;
}

// A preset's settings as controls; true when one changed. Overall width comes first: it is the
// preset's Scale shown as the width it gives, and resizes the whole shape. The lengths below it
// show their real size (the setting times the scale), so typing 0.15 m gives 0.15 m. Density is
// shown in pixels per metre.
bool RigView::draw_preset_params(const RigPreset &preset, std::vector<double> &values) {
    const std::vector<double> before = values;
    const float field = 7.0f * ImGui::GetFontSize();
    std::vector<size_t> order;
    double scale = 1.0;
    for (size_t k = 0; k < preset.params.size(); ++k) {
        if (preset.params[k].id == "scale") {
            order.insert(order.begin(), k);
            scale = values[k];
        } else {
            order.push_back(k);
        }
    }
    for (const size_t k : order) {
        const RigPresetParam &q = preset.params[k];
        ImGui::PushID(static_cast<int>(k));
        if (q.id == "scale") {
            // Scale shown as the overall size: the width the shape has at scale 1 (with the
            // other settings as they are) times the scale; typing a width sets the scale.
            std::vector<double> unscaled = values;
            unscaled[k] = 1.0;
            const std::vector<StripSpec> base = build_rig_preset(preset, unscaled);
            double min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9;
            for (const StripSpec &s : base) {
                for (const Vec3 &v : s.polyline) {
                    min_x = std::min(min_x, v.x);
                    max_x = std::max(max_x, v.x);
                    min_y = std::min(min_y, v.y);
                    max_y = std::max(max_y, v.y);
                }
            }
            const double width_m = std::max(0.01, max_x - min_x);
            const double height_m = std::max(0.0, max_y - min_y);
            double overall_m = width_m * values[k];
            ImGui::SetNextItemWidth(field);
            if (drag_double("Overall width", &overall_m, 0.005f, q.min * width_m, q.max * width_m,
                            "%.3f m")) {
                values[k] = std::clamp(overall_m / width_m, q.min, q.max);
            }
            ImGui::SetItemTooltip("Resizes the whole shape: every length below changes with it "
                                  "(scale %.3f). A length below changes only itself, and so "
                                  "the width.",
                                  values[k]);
            ImGui::SameLine();
            ImGui::TextDisabled("%.3f m tall", height_m * values[k]);
        } else if (q.id == "density") {
            // Density shown in pixels per metre: the densest strip's at the multiplier (strips
            // at other densities scale with it); typing a density sets the multiplier.
            std::vector<double> plain = values;
            plain[k] = 1.0;
            double lowest = 1e9;
            double highest = 0.0;
            for (const StripSpec &s : build_rig_preset(preset, plain)) {
                lowest = std::min(lowest, s.pixels_per_m);
                highest = std::max(highest, s.pixels_per_m);
            }
            highest = std::max(1.0, highest);
            double per_m = highest * values[k];
            ImGui::SetNextItemWidth(field);
            if (drag_double("Density", &per_m, 1.0f, q.min * highest, q.max * highest,
                            "%.0f px/m")) {
                values[k] = std::clamp(per_m / highest, q.min, q.max);
            }
            if (lowest < highest - 1e-9) {
                ImGui::SameLine();
                ImGui::TextDisabled("(others %.0f px/m)", lowest * values[k]);
            }
            ImGui::SetItemTooltip("LEDs per metre of strip (60 is the common WS2812 strip).");
        } else if (q.toggle) {
            bool on = values[k] >= 0.5;
            if (ImGui::Checkbox(q.label.c_str(), &on)) {
                values[k] = on ? 1.0 : 0.0;
            }
        } else if (q.integer) {
            int v = static_cast<int>(std::lround(values[k]));
            ImGui::SetNextItemWidth(field);
            if (drag_int(q.label.c_str(), &v, 0.1f, static_cast<int>(q.min),
                         static_cast<int>(q.max))) {
                values[k] = v;
            }
        } else if (q.unit == "m") {
            // A length at its real size: the setting times the scale.
            double real_m = values[k] * scale;
            ImGui::SetNextItemWidth(field);
            if (drag_double(q.label.c_str(), &real_m,
                            static_cast<float>((q.max - q.min) * scale / 400.0), q.min * scale,
                            q.max * scale, "%.3f m")) {
                values[k] = std::clamp(real_m / scale, q.min, q.max);
            }
        } else {
            const std::string format = "%.2f" + (q.unit.empty() ? std::string() : " " + q.unit);
            ImGui::SetNextItemWidth(field);
            drag_double(q.label.c_str(), &values[k], static_cast<float>((q.max - q.min) / 400.0),
                        q.min, q.max, format.c_str());
        }
        ImGui::PopID();
    }
    return values != before;
}

void RigView::draw_current_settings() {
    const Project &p = project_.project();
    if (p.rig_source.preset.empty()) {
        ImGui::TextDisabled("A rig of its own (a template, one of your rigs or a drawing): edit "
                            "its strips below or on the Stage.");
        return;
    }
    const auto found = std::find_if(presets_.begin(), presets_.end(), [&](const RigPreset &r) {
        return r.name == p.rig_source.preset;
    });
    if (found == presets_.end()) {
        ImGui::TextDisabled("Made from the %s preset, which is no longer built in.",
                            p.rig_source.preset.c_str());
        return;
    }
    std::vector<double> values;
    for (const RigPresetParam &q : found->params) {
        const auto v = p.rig_source.values.find(q.id);
        values.push_back(v != p.rig_source.values.end() ? v->second : q.value);
    }
    ImGui::Text("Made from the %s preset.", found->name.c_str());
    ImGui::SameLine();
    bool locked = p.rig_source.locked;
    if (ImGui::Checkbox("Lock", &locked)) {
        project_.edit().rig_source.locked = locked;
        project_.changed();
    }
    ImGui::SetItemTooltip("Locked, the strips stay exactly as they are (hand edits, moved "
                          "points). Unlocked, changing a setting rebuilds the shape: strips keep "
                          "their colour order and wiring place by name, moved points are lost.");
    ImGui::PushID("current settings");
    ImGui::BeginDisabled(locked);
    if (draw_preset_params(*found, values)) {
        project_.edit().strips = rebuilt_strips(p.strips, build_rig_preset(*found, values));
        for (size_t k = 0; k < found->params.size(); ++k) {
            project_.edit().rig_source.values[found->params[k].id] = values[k];
        }
        project_.changed();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void RigView::draw_presets(std::vector<StripSpec> &strips) {
    preset_ = rig_list("##presets", presets_, preset_);
    const RigPreset &preset = presets_[static_cast<size_t>(preset_)];
    ImGui::TextUnformatted(preset.name.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", preset.description.c_str());
    ImGui::PopTextWrapPos();
    // Its settings, kept per preset while the app runs; the strips follow them.
    if (preset_values_.size() != presets_.size()) {
        preset_values_.resize(presets_.size());
    }
    std::vector<double> &values = preset_values_[static_cast<size_t>(preset_)];
    if (values.size() != preset.params.size()) {
        values.clear();
        for (const RigPresetParam &q : preset.params) {
            values.push_back(q.value);
        }
    }
    draw_preset_params(preset, values);
    if (!preset.params.empty() && ImGui::SmallButton("Defaults")) {
        values.clear(); // Refilled with the defaults next frame.
    }
    strips = build_rig_preset(preset, values);
}

void RigView::draw_replace(std::vector<StripSpec> strips, const RigSource &source) {
    // What it would make, against the budgets.
    const Project &p = project_.project();
    const ColourOrder order = p.strips.empty() ? ColourOrder::grb : p.strips.front().order;
    const Rig rig = build_rig(strips);
    const Patch patch = build_patch(rig, p.first_universe, p.pixels_per_universe,
                                    std::vector<ColourOrder>(strips.size(), order));
    const auto pixels = static_cast<int>(rig.pixels.size());
    ImGui::Spacing();
    mono_text("%zu strips, %d pixels, %.2f m of strip, universes %d to %d, %.1f A at full white",
              strips.size(), pixels, strip_length_m(strips),
              patch.universes.empty() ? 0 : patch.universes.front(),
              patch.universes.empty() ? 0 : patch.universes.back(),
              pixels * 3 * p.limiter.current_per_channel_ma * 1e-3);
    if (pixels > kPixelBudget) {
        ImGui::TextColored(theme().warning, "Over the 1,500-pixel budget (SPEC 2.1).");
    }
    if (ImGui::Button("Replace the rig...")) {
        ImGui::OpenPopup("Replace the rig?");
    }
    ImGui::SetItemTooltip("The strips are replaced; scenes, palettes, limiters and output stay.");
    if (ImGui::BeginPopupModal("Replace the rig?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Replace the %zu strips with these %zu?", p.strips.size(), strips.size());
        ImGui::TextDisabled("Scenes, palettes, limiters and output stay. Group masks use the new "
                            "tags.");
        if (ImGui::Button("Replace")) {
            std::vector<ProjectStrip> made;
            for (StripSpec &s : strips) {
                made.push_back({std::move(s), order});
            }
            project_.edit().strips = std::move(made);
            project_.edit().rig_source = source; // Empty unless a preset made it.
            project_.changed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void RigView::reload_user_rigs() {
    user_rigs_ = load_user_presets(kUserPresetsDir).rigs;
    user_rigs_loaded_ = true;
}

void RigView::draw_user_rigs(std::vector<StripSpec> &strips) {
    if (!user_rigs_loaded_) {
        reload_user_rigs();
    }
    if (user_rigs_.empty()) {
        ImGui::TextDisabled("No rigs of your own yet. Name the current rig below and save it, to "
                            "start any project from it.");
        return;
    }
    user_rig_ = rig_list("##my rigs", user_rigs_, user_rig_);
    const RigPreset &chosen = user_rigs_[static_cast<size_t>(user_rig_)];
    ImGui::TextUnformatted(chosen.name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        ImGui::OpenPopup("Delete the rig?");
    }
    bool deleted = false;
    if (ImGui::BeginPopupModal("Delete the rig?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\" from your rigs?", chosen.name.c_str());
        ImGui::TextDisabled("Projects keep their own strips; only this saved rig goes.");
        if (ImGui::Button("Delete")) {
            std::error_code error;
            std::filesystem::remove(preset_path(kUserPresetsDir, PresetKind::rig, chosen.name),
                                    error);
            rig_status_ = error ? "Not deleted: " + error.message() : "Deleted " + chosen.name;
            deleted = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", chosen.description.c_str());
    ImGui::PopTextWrapPos();
    if (deleted) {
        reload_user_rigs(); // After the last use of `chosen`.
        return;
    }
    strips = chosen.strips;
}

void RigView::draw_svg(std::vector<StripSpec> &strips) {
    if (ImGui::Button("Choose a drawing...")) {
        svg_requested_ = true;
    }
    ImGui::SetItemTooltip("An SVG (Inkscape) or a DXF (Fusion: right-click a sketch, Save As DXF; "
                          "LibreCAD, KiCad), in real units such as mm.");
    if (!svg_status_.empty()) {
        ImGui::TextColored(theme().warning, "%s", svg_status_.c_str());
    }
    if (svg_text_.empty()) {
        ImGui::TextWrapped(
            "Draw each light run as a path, the way its pixels run (Inkscape: Path > "
            "Reverse turns one round). Layers name the groups: put the contour in "
            "a layer called contour, the rings in rings. Hidden layers (a photo "
            "traced over) are left out. A DXF works too (Fusion: right-click the sketch, Save "
            "As DXF): its layers are the groups, and arcs and circles on the left are turned "
            "so mirrored halves run from the middle outwards.");
        return;
    }
    ImGui::TextUnformatted(svg_file_.c_str());
    const float field = 7.0f * ImGui::GetFontSize();
    ImGui::SetNextItemWidth(field);
    drag_double("Width", &svg_options_.width_m, 0.01f, 0.05, 50.0, "%.2f m");
    ImGui::SetItemTooltip("The whole drawing scaled to this width.");
    ImGui::SetNextItemWidth(field);
    drag_double("Bottom above the floor", &svg_options_.base_m, 0.01f, 0.0, 20.0, "%.2f m");
    ImGui::SetNextItemWidth(field);
    drag_double("Density", &svg_options_.pixels_per_m, 1.0f, 1.0, 1000.0, "%.0f px/m");
    ImGui::Checkbox("Centre lines of solid rings", &svg_options_.centre_lines);
    ImGui::SetItemTooltip("A ring drawn as two outlines (a cut part's inner and outer edge) "
                          "becomes one strip along its middle.");
    if (svg_options_.centre_lines) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(field);
        drag_double("up to##ring width", &svg_options_.ring_width_m, 0.001f, 0.002, 0.5,
                    "%.3f m wide");
        ImGui::SetItemTooltip("Outlines further apart than this stay two strips (as drawn).");
    }
    const auto same = [](const SvgRigOptions &a, const SvgRigOptions &b) {
        return a.width_m == b.width_m && a.base_m == b.base_m && a.pixels_per_m == b.pixels_per_m &&
               a.centre_lines == b.centre_lines && a.ring_width_m == b.ring_width_m;
    };
    if (!svg_built_ || !svg_built_options_ || !same(*svg_built_options_, svg_options_)) {
        svg_built_ = svg_dxf_ ? rig_from_dxf(svg_text_, svg_options_)
                              : rig_from_svg(svg_text_, svg_options_); // When a setting changed.
        svg_built_options_ = svg_options_;
    }
    if (!*svg_built_) {
        ImGui::TextColored(theme().warning, "%s", svg_built_->error().message.c_str());
        return;
    }
    const SvgRig &rig = svg_built_->value();
    mono_text("%.2f by %.2f m (drawn %.2f m wide)", rig.width_m, rig.height_m, rig.natural_width_m);
    if (rig.centre_lines > 0) {
        ImGui::TextDisabled("%d solid ring%s as centre lines.", rig.centre_lines,
                            rig.centre_lines == 1 ? "" : "s");
    }
    if (rig.too_short > 0) {
        ImGui::TextDisabled("%d path%s shorter than a pixel left out.", rig.too_short,
                            rig.too_short == 1 ? "" : "s");
    }
    strips = rig.strips;
}

// Export drawing: what to draw, then SVG or DXF; the file is written after the frame.
void RigView::draw_export() {
    if (ImGui::Button("Export drawing...")) {
        ImGui::OpenPopup("Export the rig as a drawing");
    }
    ImGui::SetItemTooltip("The rig as an SVG or DXF to build from: paths, channels, pixels and "
                          "wiring, in millimetres.");
    if (!ImGui::BeginPopupModal("Export the rig as a drawing", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    RigExportOptions &o = export_options_;
    // Each layer's colour (laser software maps colours to cut, engrave and so on).
    const auto colour = [](const char *id, ExportColour &c) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(6.0f * ImGui::GetFontSize());
        int index = static_cast<int>(c);
        if (ImGui::Combo(id, &index, kExportColourNames, IM_ARRAYSIZE(kExportColourNames))) {
            c = static_cast<ExportColour>(index);
        }
    };
    ImGui::Checkbox("Centre lines (where the LEDs sit)", &o.centre_lines);
    colour("##centre colour", o.centre_colour);
    ImGui::Checkbox("Channels", &o.channels);
    colour("##channel colour", o.channel_colour);
    ImGui::SetItemTooltip("Each path offset either side: grooves or a mount to cut.");
    if (o.channels) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(7.0f * ImGui::GetFontSize());
        double width_mm = o.channel_width_m * 1000.0;
        if (drag_double("wide##channel", &width_mm, 0.1f, 1.0, 100.0, "%.1f mm")) {
            o.channel_width_m = width_mm / 1000.0;
        }
    }
    ImGui::Checkbox("Pixels (a dot at every LED)", &o.pixels);
    colour("##pixel colour", o.pixel_colour);
    ImGui::SameLine();
    ImGui::Checkbox("filled (engrave)", &o.fill_pixels);
    ImGui::SetItemTooltip("SVG only: DXF circles are outlines (laser software can fill them).");
    ImGui::Checkbox("Wiring (data-in arrows; order, name, line and pin)", &o.wiring);
    colour("##wiring colour", o.wiring_colour);
    ImGui::TextDisabled("Each on a layer of its own and in its colour, in millimetres; laser "
                        "software maps the colours to operations (for example black cut, red "
                        "engrave).");
    if (ImGui::Button("SVG...")) {
        export_requested_ = 1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("DXF...")) {
        export_requested_ = 2;
        ImGui::CloseCurrentPopup();
    }
    // The build kit: the panel's parts to cut, one file each.
    ImGui::SeparatorText("Build kit (a layered panel)");
    ImGui::TextDisabled("Four files to cut: back, spacer islands (the channels' walls), one "
                        "acrylic lattice, front mask. Bolt holes, wire holes and numbered "
                        "islands included; cut black, engrave red.");
    BuildKitOptions &k = kit_options_;
    k.channel_width_m = o.channel_width_m;
    int style = static_cast<int>(k.style);
    ImGui::TextUnformatted("Style");
    ImGui::SameLine();
    ImGui::RadioButton("Panel", &style, 0);
    ImGui::SetItemTooltip("A solid back and front: the rig on a board, room on the back for the "
                          "node and power supply.");
    ImGui::SameLine();
    ImGui::RadioButton("Open lattice", &style, 1);
    ImGui::SetItemTooltip("No board: every layer follows the channels, the wall shows through "
                          "between them. Walls too thin for a bolt get none: glue the layers.");
    k.style = static_cast<BuildKitStyle>(style);
    const auto mm_field = [](const char *label, double &metres, double lo, double hi,
                             const char *tip) {
        ImGui::SetNextItemWidth(7.0f * ImGui::GetFontSize());
        double v = metres * 1000.0;
        if (drag_double(label, &v, 0.1f, lo, hi, "%.1f mm")) {
            metres = v / 1000.0;
        }
        ImGui::SetItemTooltip("%s", tip);
    };
    mm_field("Window", k.window_width_m, 2.0, 100.0,
             "The front mask's openings: a little narrower than the channels, to clamp the "
             "acrylic.");
    ImGui::SameLine();
    mm_field("Acrylic overlap", k.overlap_m, 0.0, 30.0,
             "How far the acrylic reaches past each side of a channel.");
    if (k.style == BuildKitStyle::lattice) {
        mm_field("Wall", k.wall_m, 2.0, 50.0,
                 "Wood each side of a channel. 5 mm suits MDF or good birch ply; under about "
                 "9.5 mm a wall holds no bolt and is glued.");
    } else {
        mm_field("Margin", k.margin_m, 2.0, 200.0, "The panel beyond the outermost channels.");
    }
    ImGui::SameLine();
    mm_field("Bolt hole", k.bolt_hole_m, 1.0, 20.0, "3.4 mm clears M3; 4.5 mm clears M4.");
    mm_field("Wire hole", k.wire_hole_m, 1.0, 30.0, "At every strip end, through the back.");
    ImGui::TextDisabled("Sheets (for the one-DXF assembly's heights):");
    mm_field("Back##sheet", k.back_thickness_m, 1.0, 50.0, "The back panel's sheet.");
    ImGui::SameLine();
    mm_field("Spacer##sheet", k.spacer_thickness_m, 1.0, 50.0,
             "The spacer's sheet: the distance from the LEDs to the acrylic.");
    mm_field("Acrylic##sheet", k.acrylic_thickness_m, 1.0, 20.0, "The acrylic sheet.");
    ImGui::SameLine();
    mm_field("Front##sheet", k.front_thickness_m, 1.0, 50.0, "The front mask's sheet.");
    k.cut_colour = o.channel_colour;
    k.engrave_colour = o.pixel_colour;
    ImGui::TextDisabled("Uses the channel width above; cut in the Channels colour, engrave in the "
                        "Pixels colour.");
    if (ImGui::Button("Build kit SVG...")) {
        export_requested_ = 3;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Build kit DXF...")) {
        export_requested_ = 4;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Build kit, one DXF...")) {
        export_requested_ = 5;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("All four parts in one DXF, each on its own layers, stacked at its "
                          "height and as thick as its sheet: the assembled panel in a 3D viewer.");
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RigView::export_drawing(void *owner) {
    const bool kit = export_requested_ >= 3;
    const bool one_file = export_requested_ == 5;
    const bool dxf = export_requested_ == 2 || export_requested_ >= 4;
    export_requested_ = 0;
    const Project &p = project_.project();
    if (one_file) {
        const std::filesystem::path suggested =
            (project_.untitled() ? std::string("rig") : project_.path().stem().string()) +
            "_assembly.dxf";
        const auto chosen = save_drawing_file_dialog(owner, suggested, true);
        if (!chosen) {
            return;
        }
        std::vector<StripSpec> strips;
        for (const ProjectStrip &s : p.strips) {
            strips.push_back(s.spec);
        }
        // With the rig export on the LEDs' plane (centre lines, pixels, wiring as ticked above).
        const BuildKit made = build_kit(strips, kit_options_, &export_options_, wiring_notes(p));
        std::ofstream out(*chosen, std::ios::binary);
        out << made.assembly_dxf;
        char height[32];
        std::snprintf(height, sizeof(height), "%.0f", made.height_m * 1000.0);
        export_status_ =
            out ? "Build kit in one DXF: " + path_to_utf8(*chosen) + " (" + height + " mm deep)."
                : "Could not write " + path_to_utf8(*chosen) + ".";
        return;
    }
    if (kit) {
        // The parts beside the chosen name: <name>_1_back, _2_spacer, _3_acrylic, _4_front.
        const std::filesystem::path suggested =
            (project_.untitled() ? std::string("rig") : project_.path().stem().string()) + "_kit" +
            (dxf ? ".dxf" : ".svg");
        const auto chosen = save_drawing_file_dialog(owner, suggested, dxf);
        if (!chosen) {
            return;
        }
        std::vector<StripSpec> strips;
        for (const ProjectStrip &s : p.strips) {
            strips.push_back(s.spec);
        }
        const BuildKit made = build_kit(strips, kit_options_);
        bool ok = true;
        for (size_t i = 0; i < made.parts.size(); ++i) {
            std::filesystem::path file = *chosen;
            file.replace_filename(chosen->stem().string() + "_" + std::to_string(i + 1) + "_" +
                                  made.parts[i].name + (dxf ? ".dxf" : ".svg"));
            std::ofstream out(file, std::ios::binary);
            out << (dxf ? made.parts[i].dxf : made.parts[i].svg);
            ok = ok && static_cast<bool>(out);
        }
        export_status_ = ok ? "Build kit: " + std::to_string(made.parts.size()) + " files beside " +
                                  path_to_utf8(*chosen) + " (" + std::to_string(made.islands) +
                                  " spacer islands, " + std::to_string(made.bolts) + " bolts)."
                            : "Could not write the build kit beside " + path_to_utf8(*chosen) + ".";
        return;
    }
    const std::filesystem::path suggested =
        (project_.untitled() ? std::filesystem::path("rig") : project_.path().stem()).string() +
        (dxf ? ".dxf" : ".svg");
    const auto chosen = save_drawing_file_dialog(owner, suggested, dxf);
    if (!chosen) {
        return;
    }
    std::vector<StripSpec> strips;
    for (const ProjectStrip &s : p.strips) {
        strips.push_back(s.spec);
    }
    const std::vector<std::string> notes = wiring_notes(p);
    const std::string text = dxf ? rig_to_dxf(strips, notes, export_options_)
                                 : rig_to_svg(strips, notes, export_options_);
    std::ofstream out(*chosen, std::ios::binary);
    out << text;
    export_status_ = out ? "Exported " + path_to_utf8(*chosen) + "."
                         : "Could not write " + path_to_utf8(*chosen) + ".";
}

void RigView::after_frame(void *owner) {
    if (export_requested_ != 0) {
        export_drawing(owner);
    }
    if (!svg_requested_) {
        return;
    }
    svg_requested_ = false;
    const auto chosen = open_svg_file_dialog(owner);
    if (!chosen) {
        return;
    }
    std::ifstream in(*chosen, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    if (!in) {
        svg_status_ = "Could not read " + path_to_utf8(chosen->filename()) + ".";
        return;
    }
    // Its own size first; the width field starts there.
    const bool dxf = lower_extension(*chosen) == ".dxf";
    SvgRigOptions natural = svg_options_;
    natural.width_m = 0.0; // The drawing's own size first: the width field starts there.
    const Result<SvgRig> made =
        dxf ? rig_from_dxf(text.str(), natural) : rig_from_svg(text.str(), natural);
    if (!made) {
        svg_status_ = path_to_utf8(chosen->filename()) + ": " + made.error().message;
        return;
    }
    svg_status_.clear();
    svg_text_ = text.str();
    svg_dxf_ = dxf;
    svg_file_ = path_to_utf8(chosen->filename());
    svg_options_.width_m = made.value().natural_width_m;
    svg_built_.reset();
}

void RigView::draw_save_rig() {
    const float em = ImGui::GetFontSize();
    ImGui::SeparatorText("Save the current rig");
    ImGui::SetNextItemWidth(14.0f * em);
    ImGui::InputTextWithHint("##rig name", "Name", &rig_name_);
    ImGui::SameLine();
    const Project &p = project_.project();
    std::string name = rig_name_;
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    ImGui::BeginDisabled(name.empty() || p.strips.empty());
    if (primary_button("Save to my rigs")) {
        RigPreset preset;
        preset.name = name;
        for (const ProjectStrip &s : p.strips) {
            preset.strips.push_back(s.spec);
        }
        const Rig rig = build_rig(preset.strips);
        preset.description = std::to_string(preset.strips.size()) + " strips, " +
                             std::to_string(rig.pixels.size()) + " pixels, saved from " +
                             project_.name() + ".";
        if (const Result<void> saved = save_rig_preset(kUserPresetsDir, preset); saved) {
            rig_status_ = "Saved " + name + " to your rigs";
            reload_user_rigs();
            for (size_t i = 0; i < user_rigs_.size(); ++i) {
                if (user_rigs_[i].name == name) {
                    user_rig_ = static_cast<int>(i);
                }
            }
            rig_name_.clear();
        } else {
            rig_status_ = saved.error().message;
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Saves the current strips (shape, density and groups) to presets/rigs, "
                          "to start any project from. A rig of the same name is replaced.");
    if (!rig_status_.empty()) {
        ImGui::TextDisabled("%s", rig_status_.c_str());
    }
}

// Order for wiring: the strips chained by nearest ends from a chosen first one, turned round
// where that shortens the jumpers (optional), after a confirmation that shows the jumpers' total.
void RigView::draw_wiring_order(std::vector<ProjectStrip> &strips, bool &changed) {
    if (ImGui::Button("Order for wiring...")) {
        wiring_first_ = 0;
        ImGui::OpenPopup("Order for wiring");
    }
    ImGui::SetItemTooltip("Chain the strips by nearest ends, as one data line wired by hand.");
    if (!ImGui::BeginPopupModal("Order for wiring", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (strips.empty()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    wiring_first_ = std::clamp(wiring_first_, 0, static_cast<int>(strips.size()) - 1);
    ImGui::TextUnformatted("The data line starts at the first pixel of");
    ImGui::SetNextItemWidth(12.0f * ImGui::GetFontSize());
    if (ImGui::BeginCombo("##first",
                          strips[static_cast<size_t>(wiring_first_)].spec.name.c_str())) {
        for (size_t i = 0; i < strips.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(strips[i].spec.name.c_str(),
                                  static_cast<int>(i) == wiring_first_)) {
                wiring_first_ = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Turn strips round where that shortens the jumpers", &wiring_reverse_);
    ImGui::SetItemTooltip("A strip turned round runs its pixels the other way (effects that move "
                          "along strips change direction there).");
    std::vector<StripSpec> specs;
    for (const ProjectStrip &s : strips) {
        specs.push_back(s.spec);
    }
    const std::vector<WiringStep> order =
        wiring_order(specs, static_cast<size_t>(wiring_first_), wiring_reverse_);
    std::vector<StripSpec> ordered;
    int turned = 0;
    int moved = 0;
    for (size_t k = 0; k < order.size(); ++k) {
        const WiringStep &step = order[k];
        ordered.push_back(step.reversed ? reversed_strip(specs[step.strip]) : specs[step.strip]);
        turned += step.reversed;
        moved += step.strip != k;
    }
    const bool unchanged = turned == 0 && moved == 0;
    if (unchanged) {
        ImGui::TextUnformatted("The order is already good for wiring: nothing to change.");
    } else {
        mono_text("Jumpers: %.2f m now, %.2f m after; %d strip%s moved, %d turned round",
                  jumper_length_m(specs), jumper_length_m(ordered), moved, moved == 1 ? "" : "s",
                  turned);
        ImGui::TextDisabled("Strips change place or direction only where that saves %.0f cm or "
                            "more of jumper.",
                            100.0 * kWiringMinGainM);
    }
    ImGui::BeginDisabled(unchanged);
    const bool apply = ImGui::Button("Apply");
    ImGui::EndDisabled();
    if (apply) {
        std::vector<ProjectStrip> made;
        for (size_t k = 0; k < order.size(); ++k) {
            made.push_back({ordered[k], strips[order[k].strip].order});
        }
        strips = std::move(made);
        polyline_texts_.clear();
        group_texts_.clear();
        for (const ProjectStrip &s : strips) {
            polyline_texts_.push_back(polyline_text(s.spec.polyline));
            group_texts_.push_back(groups_text(s.spec.groups));
        }
        changed = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RigView::draw_strips() {
    const float em = ImGui::GetFontSize();
    std::vector<ProjectStrip> &strips = project_.edit().strips;
    bool changed = false;
    int remove = -1;
    int move = -1; // The strip to swap with the one after it (the wiring order).
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
    // Horizontal scrolling needs a fixed height: the rows, the header and the scrollbar.
    const float height =
        std::min(ImGui::GetContentRegionAvail().y * 0.6f,
                 (static_cast<float>(strips.size()) + 2.5f) * ImGui::GetFrameHeightWithSpacing());
    hovered_strip_ = -1;
    if (ImGui::BeginTable("strips", 8, flags, ImVec2(0.0f, height))) {
        hovered_strip_ = ImGui::TableGetHoveredRow() - 1; // Row 0 is the header.
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 4.5f * em);
        ImGui::TableSetupColumn("Points (x,y[,z] m)", ImGuiTableColumnFlags_WidthFixed, 10.0f * em);
        ImGui::TableSetupColumn("Per m", ImGuiTableColumnFlags_WidthFixed, 2.5f * em);
        ImGui::TableSetupColumn("Pixels", ImGuiTableColumnFlags_WidthFixed, 3.0f * em);
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 2.5f * em);
        ImGui::TableSetupColumn("Groups", ImGuiTableColumnFlags_WidthFixed, 5.0f * em);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 1.0f * em);
        ImGui::TableSetupColumn("Wiring", ImGuiTableColumnFlags_WidthFixed, 3.4f * em);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < strips.size() && i < polyline_texts_.size(); ++i) {
            ProjectStrip &s = strips[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            if (static_cast<int>(i) == highlighted_strip_) { // Hovered in the preview.
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, to_u32(theme().selection_fill));
            }
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
                // Set by hand, unless set to what fits: then it follows the path again.
                const std::vector<int> &fits = project_.rig().fits;
                s.spec.fills = i < fits.size() && s.spec.count == fits[i];
                changed = true;
            }
            const std::vector<int> &fits = project_.rig().fits;
            ImGui::SetItemTooltip("%d fill the polyline at this density, joints taken out%s. "
                                  "Double-click to type; type %d to follow the path again.",
                                  i < fits.size() ? fits[i] : 0,
                                  s.spec.fills ? " (this strip follows it)" : "",
                                  i < fits.size() ? fits[i] : 0);
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
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
                move = static_cast<int>(i) - 1;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Earlier on the data line.");
            ImGui::SameLine(0.0f, 0.2f * em);
            ImGui::BeginDisabled(i + 1 == strips.size());
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                move = static_cast<int>(i);
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Later on the data line.");
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
    ImGui::SameLine();
    draw_wiring_order(strips, changed);
    if (move >= 0 && move + 1 < static_cast<int>(strips.size())) {
        const auto a = static_cast<size_t>(move);
        std::swap(strips[a], strips[a + 1]);
        std::swap(polyline_texts_[a], polyline_texts_[a + 1]);
        std::swap(group_texts_[a], group_texts_[a + 1]);
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
    const Rig &laid = project_.rig();
    for (size_t i = 0; i < project_.project().strips.size() && i < laid.fits.size(); ++i) {
        const ProjectStrip &s = project_.project().strips[i];
        const int fill = laid.fits[i];
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
    std::vector<StripSpec> specs;
    for (const ProjectStrip &s : p.strips) {
        specs.push_back(s.spec);
    }
    mono_text("%d pixels in %zu strips (%.2f m of strip), universes %d to %d, %.1f by %.1f by "
              "%.1f m",
              pixels, p.strips.size(), strip_length_m(specs),
              patch.universes.empty() ? 0 : patch.universes.front(),
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
        // Each strip's data line on the node, as Preflight > Node setup would set it up.
        const NodePlan plan = plan_node(p);
        size_t first = 0;
        for (size_t s = 0; s < p.strips.size(); ++s) {
            const auto count = static_cast<size_t>(std::max(0, p.strips[s].spec.count));
            if (count == 0 || first + count > patch.pixels.size()) {
                first += count;
                continue;
            }
            const PatchedPixel &a = patch.pixels[first];
            const PatchedPixel &b = patch.pixels[first + count - 1];
            std::string line;
            for (size_t l = 0; l < plan.lanes.size(); ++l) {
                const NodeLane &lane = plan.lanes[l];
                const int in_chain = static_cast<int>(s) - lane.first_strip;
                if (in_chain >= 0 && in_chain < lane.strips) {
                    line = "; line " + std::to_string(l + 1) + ", " +
                           node_lane_pin(static_cast<int>(l));
                    if (lane.strips > 1) {
                        line += ", " + std::to_string(in_chain + 1) + " of " +
                                std::to_string(lane.strips) + " in its chain";
                    }
                }
            }
            mono_text("%s: universe %d channel %d to universe %d channel %d (%s)%s",
                      p.strips[s].spec.name.c_str(),
                      patch.universes[static_cast<size_t>(a.universe_slot)], a.channel + 1,
                      patch.universes[static_cast<size_t>(b.universe_slot)], b.channel + 3,
                      colour_order_name(p.strips[s].order), line.c_str());
            first += count;
        }
        ImGui::TreePop();
    }
}

} // namespace psyight
