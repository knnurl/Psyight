#include "gui/preview_view.h"

#include "engine/inputs.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace psyight {

namespace {

constexpr std::uint64_t kSeed = 1;
constexpr double kSeekResetS = 0.5; // A jump this large restarts the limiters' history.
constexpr float kMarginPixels = 12.0f;
constexpr ImU32 kBackground = IM_COL32(8, 8, 12, 255);
const ImVec4 kWarning(1.0f, 0.45f, 0.35f, 1.0f);

ImU32 to_imgui(const Rgb &c) {
    return IM_COL32(static_cast<int>(std::lround(255.0f * std::clamp(c.r, 0.0f, 1.0f))),
                    static_cast<int>(std::lround(255.0f * std::clamp(c.g, 0.0f, 1.0f))),
                    static_cast<int>(std::lround(255.0f * std::clamp(c.b, 0.0f, 1.0f))), 255);
}

} // namespace

void PreviewView::draw() {
    ImGui::Begin("Preview");
    if (!session_.has_track() || session_.track().track_analysis == nullptr) {
        ImGui::TextUnformatted(session_.has_track() ? "No analysis (no tempo found)."
                                                    : "No track loaded.");
        ImGui::End();
        return;
    }
    const TrackAnalysis &analysis = *session_.track().track_analysis;
    const double t_s = session_.playhead_s() + 1e-3 * latency_offset_ms_;
    if (std::abs(t_s - last_render_s_) > kSeekResetS) {
        limiters_.reset();
    }
    last_render_s_ = t_s;
    const EngineInputs inputs = inputs_at(analysis, session_.grid(), t_s);
    frame_ = render_scene(scene_, rig_, inputs, palettes_, kSeed);
    report_ = limiters_.process(frame_, t_s);
    flashes_limited_ += report_.flash_limited;

    draw_controls();
    ImGui::Text("%s, beat %d of the bar, %zu pixels, %.1f A estimated%s", inputs.section.c_str(),
                inputs.beat_in_bar + 1, rig_.pixels.size(), report_.current_a,
                report_.power_scale < 1.0 ? " (power limited)" : "");
    draw_rig();
    ImGui::End();
}

void PreviewView::draw_controls() {
    const float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(9.0f * em);
    if (ImGui::BeginCombo("Palette", palettes_[static_cast<size_t>(scene_.palette)].name.c_str())) {
        for (int i = 0; i < static_cast<int>(palettes_.size()); ++i) {
            if (ImGui::Selectable(palettes_[static_cast<size_t>(i)].name.c_str(),
                                  i == scene_.palette)) {
                scene_.palette = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(7.0f * em);
    ImGui::DragFloat("Latency offset (ms)", &latency_offset_ms_, 1.0f, -200.0f, 200.0f, "%.0f");
    ImGui::SetItemTooltip("Render time minus playhead. About 2.9 ms per metre from the speakers "
                          "(SPEC 4.3).");
    if (ImGui::TreeNode("Layers")) {
        for (size_t i = 0; i < scene_.layers.size(); ++i) {
            Layer &layer = scene_.layers[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Checkbox(layer.effect->name(), &layer.enabled);
            ImGui::SameLine(14.0f * em);
            ImGui::SetNextItemWidth(8.0f * em);
            ImGui::SliderFloat("opacity", &layer.opacity, 0.0f, 1.0f, "%.2f");
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Limiters")) {
        LimiterParams p = limiters_.params();
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
        if (!p.flash_limiter) {
            ImGui::TextColored(kWarning, "Flash limiter off: photosensitive epilepsy risk "
                                         "(SPEC 7.7).");
        }
        ImGui::Text("Flashes held back so far: %d", flashes_limited_);
        if (changed) {
            limiters_.set_params(p);
        }
        ImGui::TreePop();
    }
}

void PreviewView::draw_rig() {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 2.0f * kMarginPixels || size.y < 2.0f * kMarginPixels) {
        return;
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), kBackground);
    const double width_m = std::max(1e-3, rig_.max.x - rig_.min.x);
    const double height_m = std::max(1e-3, rig_.max.y - rig_.min.y);
    const double scale = std::min((size.x - 2.0f * kMarginPixels) / width_m,
                                  (size.y - 2.0f * kMarginPixels) / height_m);
    const float radius = std::max(1.5f, static_cast<float>(0.45 * scale / 60.0));
    const float left = origin.x + 0.5f * (size.x - static_cast<float>(width_m * scale));
    const float bottom = origin.y + 0.5f * (size.y + static_cast<float>(height_m * scale));
    for (size_t i = 0; i < rig_.pixels.size() && i < frame_.size(); ++i) {
        const Vec3 &q = rig_.pixels[i].position;
        const ImVec2 at(left + static_cast<float>((q.x - rig_.min.x) * scale),
                        bottom - static_cast<float>((q.y - rig_.min.y) * scale)); // y up.
        draw->AddCircleFilled(at, radius, to_imgui(frame_[i]));
    }
    ImGui::Dummy(size);
}

} // namespace psyight
