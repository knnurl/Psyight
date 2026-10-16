#include "gui/time_plot.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace psyight {

namespace {

constexpr size_t kMaxGridLines = 400;    // Finer levels are dropped beyond this.
constexpr float kClickSlopPixels = 3.0f; // Mouse travel below this is a click, not a pan.
constexpr float kHandleGrabPixels = 6.0f;
const ImVec4 kLoopShade(0.95f, 0.75f, 0.2f, 0.10f);
const ImVec4 kLoopEdge(0.95f, 0.75f, 0.2f, 1.0f); // As in the transport overview.

} // namespace

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
        ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, alpha), weight);
        ImPlot::PlotInfLines(label, times.data(), static_cast<int>(times.size()));
    };
    plot_lines("##slots", slots, 0.10f, 1.0f);
    plot_lines("##beats", beats, 0.30f, 1.0f);
    plot_lines("##bars", bars, 0.60f, 1.5f);
}

void draw_playhead(double playhead_s) {
    ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, 0.95f), 2.0f);
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
                                             ImGui::ColorConvertFloat4ToU32(kLoopShade));
    ImPlot::PopPlotClipRect();
    if (!editable) {
        const double edges_s[] = {start_s, end_s};
        ImPlot::SetNextLineStyle(kLoopEdge, 2.0f);
        ImPlot::PlotInfLines("##loop edges", edges_s, 2);
    } else {
        const bool moved = ImPlot::DragLineX(first_drag_id, &loop.start_s, kLoopEdge, 2.0f) |
                           ImPlot::DragLineX(first_drag_id + 1, &loop.end_s, kLoopEdge, 2.0f);
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
