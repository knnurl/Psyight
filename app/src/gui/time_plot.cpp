#include "gui/time_plot.h"

#include "gui/theme.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace psyight {

namespace {

constexpr size_t kMaxGridLines = 400;    // Finer levels are dropped beyond this.
constexpr float kClickSlopPixels = 3.0f; // Mouse travel below this is a click, not a pan.
constexpr float kHandleGrabPixels = 6.0f;

ImU32 section_colour(const std::string &label) {
    const ThemeTokens &t = theme();
    if (label == "groove") {
        return to_u32(t.section_groove);
    }
    if (label == "breakdown") {
        return to_u32(t.section_breakdown);
    }
    if (label == "build") {
        return to_u32(t.section_build);
    }
    if (label == "drop") {
        return to_u32(t.section_drop);
    }
    return to_u32(t.section_other); // Intro and outro.
}

} // namespace

std::string fixed_width_label(std::string_view text) {
    std::string label(text);
    if (label.size() < static_cast<size_t>(kValueLabelChars)) {
        label.insert(0, kValueLabelChars - label.size(), ' ');
    }
    return label;
}

int format_fixed_width(double value, char *buffer, int size, void *decimals) {
    return std::snprintf(buffer, static_cast<size_t>(size), "%*.*f", kValueLabelChars,
                         *static_cast<int *>(decimals), value);
}

void setup_time_axis(TimeView &view) {
    ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoLabel);
    ImPlot::SetupAxisLinks(ImAxis_X1, &view.min_s, &view.max_s);
}

void draw_grid_lines(const Grid &grid, double from_s, double to_s) {
    std::vector<double> bars;
    std::vector<double> beats;
    std::vector<double> slots;
    for (const GridLine &line :
         grid.lines(seconds_to_samples(from_s, grid.sample_rate_hz),
                    seconds_to_samples(to_s, grid.sample_rate_hz), kMaxGridLines)) {
        const double t_s = samples_to_seconds(line.sample, grid.sample_rate_hz);
        (line.level == GridLevel::bar    ? bars
         : line.level == GridLevel::beat ? beats
                                         : slots)
            .push_back(t_s);
    }
    const auto plot_lines = [](const char *label, const std::vector<double> &times, float alpha,
                               float weight) {
        ImPlot::SetNextLineStyle(with_alpha(theme().text, alpha), weight);
        ImPlot::PlotInfLines(label, times.data(), static_cast<int>(times.size()));
    };
    const ThemeTokens &t = theme();
    plot_lines("##slots", slots, t.grid_slot_alpha, 1.0f);
    plot_lines("##beats", beats, t.grid_beat_alpha, 1.0f);
    plot_lines("##bars", bars, t.grid_bar_alpha, 1.5f);
}

void draw_sections(const std::vector<DetectedSection> &sections,
                   const std::vector<double> &phrase_starts_s, double from_s, double to_s,
                   std::span<const float> riser, double riser_frame_s) {
    ImDrawList *draw = ImPlot::GetPlotDrawList();
    const ImVec2 origin = ImPlot::GetPlotPos();
    const float height = ImGui::GetTextLineHeight() + 2.0f;
    ImPlot::PushPlotClipRect();
    for (const DetectedSection &section : sections) {
        if (section.end_s < from_s || section.start_s > to_s) {
            continue;
        }
        const float left = ImPlot::PlotToPixels(section.start_s, 0.0).x;
        const float right = ImPlot::PlotToPixels(section.end_s, 0.0).x;
        draw->AddRectFilled(ImVec2(left, origin.y), ImVec2(right, origin.y + height),
                            section_colour(section.label));
        draw->AddLine(ImVec2(left, origin.y), ImVec2(left, origin.y + height),
                      to_u32(theme().section_boundary), 2.0f);
        const float text_left = std::max(left, origin.x) + 3.0f;
        const float label_w = ImGui::CalcTextSize(section.label.c_str()).x;
        if (right - text_left > label_w + 3.0f) {
            draw->AddText(ImVec2(text_left, origin.y + 1.0f), to_u32(theme().section_text),
                          section.label.c_str());
        }
        // Energy: three steps of rising height, lit up to the section's level.
        constexpr float kStepW = 3.0f;
        constexpr float kStepGap = 1.0f;
        const float meter_w = 3.0f * kStepW + 2.0f * kStepGap;
        if (right - text_left > label_w + meter_w + 12.0f) {
            for (int k = 0; k < 3; ++k) {
                const float x = right - 4.0f - meter_w + k * (kStepW + kStepGap);
                const float h = (height - 6.0f) * (k + 1) / 3.0f;
                const ImU32 colour = to_u32(theme().section_text);
                const ImVec2 a(x, origin.y + height - 3.0f - h);
                const ImVec2 b(x + kStepW, origin.y + height - 3.0f);
                if (k <= section.energy) {
                    draw->AddRectFilled(a, b, colour);
                } else {
                    draw->AddRect(a, b, colour);
                }
            }
        }
        // The riser through a build: a line from the strip's foot to its top.
        if (section.label == "build" && !riser.empty() && riser_frame_s > 0.0) {
            std::vector<ImVec2> points;
            const float x0 = std::max(left, origin.x);
            const float x1 = std::min(right, origin.x + ImPlot::GetPlotSize().x);
            for (float x = x0; x <= x1; x += 2.0f) {
                const double t_s = ImPlot::PixelsToPlot(ImVec2(x, origin.y)).x;
                const auto frame = static_cast<size_t>(std::clamp(
                    std::lround(t_s / riser_frame_s), 0L, static_cast<long>(riser.size()) - 1));
                points.emplace_back(x, origin.y + height - 1.0f - (height - 2.0f) * riser[frame]);
            }
            if (points.size() >= 2) {
                draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                                  to_u32(theme().section_text), ImDrawFlags_None, 1.5f);
            }
        }
    }
    for (const double t_s : phrase_starts_s) {
        if (t_s >= from_s && t_s <= to_s) {
            const float x = ImPlot::PlotToPixels(t_s, 0.0).x;
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height),
                          to_u32(theme().phrase_tick), 3.0f);
        }
    }
    ImPlot::PopPlotClipRect();
}

void draw_playhead(double playhead_s) {
    ImPlot::SetNextLineStyle(theme().playhead, 2.0f);
    ImPlot::PlotInfLines("##playhead", &playhead_s, 1);
}

std::vector<double> draw_loop(Session &session, bool editable, int first_drag_id) {
    LoopRegion loop = session.loop();
    if (!loop.enabled || !session.has_track()) {
        return {};
    }
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    const double start_s = std::min(loop.start_s, loop.end_s);
    const double end_s = std::max(loop.start_s, loop.end_s);
    ImPlot::PushPlotClipRect();
    ImPlot::GetPlotDrawList()->AddRectFilled(ImPlot::PlotToPixels(start_s, limits.Y.Max),
                                             ImPlot::PlotToPixels(end_s, limits.Y.Min),
                                             to_u32(theme().loop_shade));
    ImPlot::PopPlotClipRect();
    if (!editable) {
        const double edges_s[] = {start_s, end_s};
        ImPlot::SetNextLineStyle(theme().loop, 2.0f);
        ImPlot::PlotInfLines("##loop edges", edges_s, 2);
    } else {
        const bool moved = ImPlot::DragLineX(first_drag_id, &loop.start_s, theme().loop, 2.0f) |
                           ImPlot::DragLineX(first_drag_id + 1, &loop.end_s, theme().loop, 2.0f);
        if (moved) {
            const double duration_s = session.track().duration_s;
            loop.start_s = std::clamp(loop.start_s, 0.0, duration_s);
            loop.end_s = std::clamp(loop.end_s, 0.0, duration_s);
            session.set_loop(loop);
        }
    }
    return {loop.start_s, loop.end_s};
}

void seek_on_click(Session &session, const std::vector<double> &handle_times_s) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!session.has_track() || !ImPlot::IsPlotHovered() ||
        !ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] > kClickSlopPixels * kClickSlopPixels) {
        return;
    }
    const float mouse_x = ImGui::GetMousePos().x;
    for (const double t_s : handle_times_s) {
        if (std::abs(ImPlot::PlotToPixels(t_s, 0.0).x - mouse_x) < kHandleGrabPixels) {
            return;
        }
    }
    const LoadedTrack &track = session.track();
    const double target_s = std::clamp(ImPlot::GetPlotMousePos().x, 0.0, track.duration_s);
    session.transport().seek(seconds_to_samples(target_s, track.sample_rate_hz));
}

} // namespace psyight
