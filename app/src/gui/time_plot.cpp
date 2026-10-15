#include "gui/time_plot.h"

#include <implot.h>

#include <vector>

namespace psyight {

namespace {

constexpr size_t kMaxGridLines = 400; // Finer levels are dropped beyond this.

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

} // namespace psyight
