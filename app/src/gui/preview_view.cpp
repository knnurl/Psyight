#include "gui/preview_view.h"

#include "engine/effect_spec.h"

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

void PreviewView::set_ladder(Ladder step) {
    ladder_ = step;
    config_.send_to_network = step >= Ladder::ambient;
    if (step == Ladder::ambient) {
        config_.manual_scene = ambient_scene();
    } else if (step == Ladder::react && config_.manual_scene < 0) {
        config_.manual_scene = playing_scene_;
    } else if (step == Ladder::show) {
        config_.manual_scene = -1;
    }
    dirty_ = true;
}

int PreviewView::ambient_scene() const {
    for (const char *label : {"breakdown", "intro"}) {
        for (size_t i = 0; i < config_.scenes.size(); ++i) {
            const std::vector<std::string> &s = config_.scenes[i].sections;
            if (std::find(s.begin(), s.end(), label) != s.end()) {
                return static_cast<int>(i);
            }
        }
    }
    return 0;
}

void PreviewView::draw_top_bar() {
    struct Step {
        Ladder step;
        const char *name;
        const char *meaning;
    };
    static constexpr Step kSteps[] = {
        {Ladder::preflight, "Preflight", "Checks; nothing is sent to the network."},
        {Ladder::preview, "Preview", "The engine runs for the preview (virtual node) only."},
        {Ladder::ambient, "Ambient", "Sends to the network: the ambient scene only."},
        {Ladder::react, "React", "Sends to the network: scenes chosen by hand."},
        {Ladder::show, "Show", "Sends to the network: scenes follow the sections."},
    };
    ImGui::TextDisabled("|");
    for (const Step &s : kSteps) {
        ImGui::SameLine();
        if (ImGui::RadioButton(s.name, ladder_ == s.step)) {
            set_ladder(s.step);
        }
        ImGui::SetItemTooltip("%s", s.meaning);
    }
    // Blackout and Hold: fixed at the right, with hotkeys, no confirmation (SPEC 9.1).
    const ImGuiStyle &style = ImGui::GetStyle();
    const float buttons = ImGui::CalcTextSize("Blackout (B)").x +
                          ImGui::CalcTextSize("Hold (H)").x + 4.0f * style.FramePadding.x +
                          style.ItemSpacing.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - buttons - style.WindowPadding.x);
    const auto toggle = [this](const char *label, bool &on, const ImVec4 &colour) {
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, colour);
        }
        if (ImGui::Button(label)) {
            on = !on;
            dirty_ = true;
        }
        if (on) {
            ImGui::PopStyleColor();
        }
    };
    toggle("Blackout (B)", config_.blackout, ImVec4(0.8f, 0.15f, 0.1f, 1.0f));
    ImGui::SameLine();
    toggle("Hold (H)", config_.hold, ImVec4(0.8f, 0.55f, 0.1f, 1.0f));
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
            config_.blackout = !config_.blackout;
            dirty_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
            config_.hold = !config_.hold;
            dirty_ = true;
        }
    }
}

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
    config_.scenes = p.scenes;
    config_.palettes = p.palettes;
    if (config_.manual_scene >= static_cast<int>(config_.scenes.size())) {
        config_.manual_scene = -1;
    }
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
    playing_scene_ = std::max(0, frame->mix.to);

    draw_controls(*frame);
    ImGui::Text("%s%s, beat %d of the bar, %.1f A estimated%s",
                config_.blackout ? "Blackout: "
                : config_.hold   ? "Hold: "
                                 : "",
                frame->section.c_str(), frame->beat_in_bar + 1, frame->report.current_a,
                frame->report.power_scale < 1.0 ? " (power limited)" : "");
    draw_rig(*frame);
    ImGui::End();
}

void PreviewView::draw_controls(const ShowFrame &frame) {
    const float em = ImGui::GetFontSize();
    draw_scene_cards(frame);
    ImGui::SetNextItemWidth(7.0f * em);
    float latency_ms = static_cast<float>(project_.project().latency_offset_ms);
    if (ImGui::DragFloat("Latency offset (ms)", &latency_ms, 1.0f, -200.0f, 200.0f, "%.0f")) {
        project_.edit().latency_offset_ms = latency_ms;
        project_.changed();
    }
    ImGui::SetItemTooltip("Render time minus playhead. About 2.9 ms per metre from the speakers "
                          "(SPEC 4.3).");
    std::vector<Scene> &scenes = project_.edit().scenes;
    edit_scene_ = std::clamp(edit_scene_, 0, static_cast<int>(scenes.size()) - 1);
    const std::string scene_label =
        "Scene: " + scenes[static_cast<size_t>(edit_scene_)].name + "###scene";
    if (ImGui::TreeNode(scene_label.c_str())) {
        draw_scene_editor(scenes);
        ImGui::TreePop();
    }
    if (scene_edited_) {
        project_.changed();
        scene_edited_ = false;
    }
    draw_palettes();
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
        ImGui::Text("Flashes held back so far: %lld",
                    static_cast<long long>(frame.flashes_limited));
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Output")) {
        draw_output(frame);
        ImGui::TreePop();
    }
}

void PreviewView::draw_scene_editor(std::vector<Scene> &scenes) {
    const float em = ImGui::GetFontSize();
    Scene &scene = scenes[static_cast<size_t>(edit_scene_)];
    ImGui::SetNextItemWidth(10.0f * em);
    scene_edited_ |= ImGui::InputText("Name", &scene.name);
    const std::vector<Palette> &palettes = project_.project().palettes;
    scene.palette = std::clamp(scene.palette, 0, static_cast<int>(palettes.size()) - 1);
    ImGui::SetNextItemWidth(9.0f * em);
    if (ImGui::BeginCombo("Palette (base)",
                          palettes[static_cast<size_t>(scene.palette)].name.c_str())) {
        for (int i = 0; i < static_cast<int>(palettes.size()); ++i) {
            if (ImGui::Selectable(palettes[static_cast<size_t>(i)].name.c_str(),
                                  i == scene.palette)) {
                scene.palette = i;
                scene_edited_ = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Automation shifts it by one every %d phrases.",
                          project_.project().automation.palette_every_phrases);
    // Section tags: which sections the automation may pick this scene for; wraps like the cards.
    ImGui::TextUnformatted("For");
    const ImGuiStyle &style = ImGui::GetStyle();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    for (const char *label : {"intro", "groove", "breakdown", "build", "drop", "outro"}) {
        const float width =
            ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
        if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width < right) {
            ImGui::SameLine();
        }
        auto it = std::find(scene.sections.begin(), scene.sections.end(), label);
        bool tagged = it != scene.sections.end();
        if (ImGui::Checkbox(label, &tagged)) {
            if (tagged) {
                scene.sections.emplace_back(label);
            } else {
                scene.sections.erase(it);
            }
            scene_edited_ = true;
        }
    }

    // Layers, bottom first.
    int remove = -1;
    int swap_down = -1;
    for (size_t i = 0; i < scene.layers.size(); ++i) {
        Layer &layer = scene.layers[i];
        ImGui::PushID(static_cast<int>(i));
        scene_edited_ |= ImGui::Checkbox("##on", &layer.enabled);
        ImGui::SameLine();
        const bool open = ImGui::TreeNode(layer.effect->name());
        ImGui::SameLine(15.5f * em);
        ImGui::SetNextItemWidth(5.0f * em);
        scene_edited_ |= ImGui::SliderFloat("##opacity", &layer.opacity, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("Opacity");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(5.0f * em);
        if (ImGui::BeginCombo("##blend", blend_mode_id(layer.blend))) {
            for (const BlendMode m : kBlendModes) {
                if (ImGui::Selectable(blend_mode_id(m), m == layer.blend)) {
                    layer.blend = m;
                    scene_edited_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("v") && i + 1 < scene.layers.size()) {
            swap_down = static_cast<int>(i);
        }
        ImGui::SetItemTooltip("Move down (earlier, under the layers above).");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            remove = static_cast<int>(i);
        }
        ImGui::SetItemTooltip("Remove this layer and its routes.");
        if (open) {
            // Group mask: the rig's group tags.
            std::vector<std::string> groups;
            for (const ProjectStrip &strip : project_.project().strips) {
                for (const std::string &g : strip.spec.groups) {
                    if (std::find(groups.begin(), groups.end(), g) == groups.end()) {
                        groups.push_back(g);
                    }
                }
            }
            ImGui::SetNextItemWidth(8.0f * em);
            if (ImGui::BeginCombo("Strips", layer.group.empty() ? "All" : layer.group.c_str())) {
                if (ImGui::Selectable("All", layer.group.empty())) {
                    layer.group.clear();
                    scene_edited_ = true;
                }
                for (const std::string &g : groups) {
                    if (ImGui::Selectable(g.c_str(), g == layer.group)) {
                        layer.group = g;
                        scene_edited_ = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Group mask: only the strips tagged with this group (Rig tab).");
            const std::vector<EffectParamInfo> infos = effect_param_infos(layer.spec.type());
            bool params_changed = false;
            for (size_t k = 0; k < infos.size(); ++k) {
                const EffectParamInfo &info = infos[k];
                ImGui::SetNextItemWidth(8.0f * em);
                const std::string label =
                    std::string(info.label) +
                    (info.unit[0] != 0 ? std::string(" (") + info.unit + ")" : std::string());
                if (info.integer) {
                    int v = static_cast<int>(std::lround(effect_param(layer.spec, k)));
                    if (ImGui::SliderInt(label.c_str(), &v, static_cast<int>(info.min),
                                         static_cast<int>(info.max))) {
                        set_effect_param(layer.spec, k, v);
                        params_changed = true;
                    }
                } else {
                    float v = static_cast<float>(effect_param(layer.spec, k));
                    if (ImGui::SliderFloat(label.c_str(), &v, static_cast<float>(info.min),
                                           static_cast<float>(info.max), "%.3g")) {
                        set_effect_param(layer.spec, k, v);
                        params_changed = true;
                    }
                }
            }
            if (params_changed) {
                layer.effect = make_effect(layer.spec);
                scene_edited_ = true;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (swap_down >= 0) {
        swap_layers(scene, static_cast<size_t>(swap_down), static_cast<size_t>(swap_down) + 1);
        scene_edited_ = true;
    }
    if (remove >= 0) {
        remove_layer(scene, static_cast<size_t>(remove));
        scene_edited_ = true;
    }
    ImGui::SetNextItemWidth(9.0f * em);
    if (ImGui::BeginCombo("##add layer", "Add layer")) {
        for (const EffectType t : kEffectTypes) {
            const Layer preview_layer = make_layer(default_spec(t), BlendMode::add);
            if (ImGui::Selectable(preview_layer.effect->name())) {
                scene.layers.push_back(preview_layer);
                scene_edited_ = true;
            }
        }
        ImGui::EndCombo();
    }
    draw_routes(scene);

    // The scene list.
    if (ImGui::Button("Duplicate scene")) {
        Scene copy = scene;
        copy.name += " (copy)";
        scenes.insert(scenes.begin() + edit_scene_ + 1, std::move(copy));
        ++edit_scene_;
        scene_edited_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(scenes.size() < 2);
    if (ImGui::Button("Delete scene")) {
        scenes.erase(scenes.begin() + edit_scene_);
        edit_scene_ = std::max(0, edit_scene_ - 1);
        config_.manual_scene = -1;
        dirty_ = true;
        scene_edited_ = true;
    }
    ImGui::EndDisabled();
}

void PreviewView::draw_palettes() {
    if (!ImGui::TreeNode("Palettes")) {
        return;
    }
    Project &project = project_.edit();
    std::vector<Palette> &palettes = project.palettes;
    bool changed = false;
    int remove = -1;
    for (size_t p = 0; p < palettes.size(); ++p) {
        Palette &palette = palettes[p];
        ImGui::PushID(static_cast<int>(p));
        ImGui::SetNextItemWidth(7.0f * ImGui::GetFontSize());
        changed |= ImGui::InputText("##name", &palette.name);
        int remove_colour = -1;
        for (size_t c = 0; c < palette.colours.size(); ++c) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(c));
            float rgb[3] = {palette.colours[c].r, palette.colours[c].g, palette.colours[c].b};
            if (ImGui::ColorEdit3("##colour", rgb, ImGuiColorEditFlags_NoInputs)) {
                palette.colours[c] = {rgb[0], rgb[1], rgb[2]};
                changed = true;
            }
            if (ImGui::BeginPopupContextItem("colour menu")) {
                if (ImGui::MenuItem("Remove colour", nullptr, false, palette.colours.size() > 1)) {
                    remove_colour = static_cast<int>(c);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (remove_colour >= 0) {
            palette.colours.erase(palette.colours.begin() + remove_colour);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) {
            palette.colours.push_back(palette.colours.back());
            changed = true;
        }
        ImGui::SetItemTooltip("Add a colour. Right-click a colour to remove it.");
        ImGui::SameLine();
        ImGui::BeginDisabled(palettes.size() < 2);
        if (ImGui::SmallButton("x")) {
            remove = static_cast<int>(p);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Delete this palette (scenes using it move to the first).");
        ImGui::PopID();
    }
    if (remove >= 0) {
        palettes.erase(palettes.begin() + remove);
        for (Scene &scene : project.scenes) {
            if (scene.palette == remove) {
                scene.palette = 0;
            } else if (scene.palette > remove) {
                --scene.palette;
            }
        }
        changed = true;
    }
    if (ImGui::Button("Add palette")) {
        palettes.push_back({"Palette " + std::to_string(palettes.size() + 1),
                            {{0.5f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.6f}}});
        changed = true;
    }
    ImGui::TextDisabled("Keep white out: it is for kicks and drops (SPEC 7.5).");
    if (changed) {
        project_.changed();
    }
    ImGui::TreePop();
}

void PreviewView::draw_routes(Scene &scene) {
    if (!ImGui::TreeNode("Modulation")) {
        return;
    }
    const float em = ImGui::GetFontSize();
    int remove = -1;
    for (size_t i = 0; i < scene.routes.size(); ++i) {
        ModRoute &r = scene.routes[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(8.0f * em);
        if (ImGui::BeginCombo("##source", mod_source_name(r.source))) {
            for (const ModSource s : kModSources) {
                if (ImGui::Selectable(mod_source_name(s), s == r.source)) {
                    r.source = s;
                    scene_edited_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(6.0f * em);
        if (ImGui::BeginCombo("##curve", mod_curve_name(r.curve))) {
            for (const ModCurve c : kModCurves) {
                if (ImGui::Selectable(mod_curve_name(c), c == r.curve)) {
                    r.curve = c;
                    scene_edited_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        scene_edited_ |= ImGui::Checkbox("invert", &r.invert);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            remove = static_cast<int>(i);
        }
        ImGui::SetItemTooltip("Remove this route.");
        ImGui::Indent();
        ImGui::SetNextItemWidth(7.0f * em);
        if (ImGui::BeginCombo("##target", mod_target_name(r.target))) {
            for (const ModTarget t : kModTargets) {
                if (ImGui::Selectable(mod_target_name(t), t == r.target)) {
                    r.target = t;
                    r.amount = t == ModTarget::hue_shift ? 60.0 : 0.5;
                    scene_edited_ = true;
                }
            }
            ImGui::EndCombo();
        }
        if (r.target != ModTarget::brightness && !scene.layers.empty()) {
            r.layer = std::clamp(r.layer, 0, static_cast<int>(scene.layers.size()) - 1);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(8.0f * em);
            if (ImGui::BeginCombo("##layer",
                                  scene.layers[static_cast<size_t>(r.layer)].effect->name())) {
                for (int k = 0; k < static_cast<int>(scene.layers.size()); ++k) {
                    if (ImGui::Selectable(scene.layers[static_cast<size_t>(k)].effect->name(),
                                          k == r.layer)) {
                        r.layer = k;
                        scene_edited_ = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(6.0f * em);
        float amount = static_cast<float>(r.amount);
        const bool hue = r.target == ModTarget::hue_shift;
        if (ImGui::SliderFloat("amount", &amount, hue ? -180.0f : -1.0f, hue ? 180.0f : 1.0f,
                               hue ? "%.0f deg" : "%.2f")) {
            r.amount = amount;
            scene_edited_ = true;
        }
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (remove >= 0) {
        scene.routes.erase(scene.routes.begin() + remove);
        scene_edited_ = true;
    }
    if (ImGui::Button("Add route")) {
        scene.routes.push_back({});
        scene_edited_ = true;
    }
    ImGui::SetItemTooltip("Source, curve and amount onto a layer's opacity or hue, or the "
                          "scene's brightness (SPEC 7.4). Routes to one target add up.");
    ImGui::TreePop();
}

void PreviewView::draw_scene_cards(const ShowFrame &frame) {
    const ImGuiStyle &style = ImGui::GetStyle();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const bool manual = config_.manual_scene >= 0;
    ImGui::BeginDisabled(ladder_ == Ladder::ambient); // Ambient plays the ambient scene only.
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
            if (ladder_ == Ladder::show) {
                ladder_ = Ladder::react; // A scene held by hand.
            }
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
        if (ladder_ == Ladder::react && automatic) {
            ladder_ = Ladder::show;
        } else if (ladder_ == Ladder::show && !automatic) {
            ladder_ = Ladder::react;
        }
        dirty_ = true;
    }
    ImGui::EndDisabled();
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
    if (config_.send_to_network) {
        ImGui::TextColored(kWarning, "Sending to the network (top bar: Ambient, React or Show).");
    } else {
        ImGui::TextUnformatted("Not sending: the virtual node (this preview) only. The top bar's "
                               "Ambient, React and Show send.");
    }
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
            address_text_ = ipv4_text(o.address); // Loaded or reset elsewhere.
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
                           static_cast<long long>(frame.send_errors), frame.network_error.c_str());
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
