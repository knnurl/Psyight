#include "gui/analysis_view.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace psyight {

namespace {

constexpr double kMinBpm = 60.0;
constexpr double kMaxBpm = 250.0;
constexpr double kPlayheadLead = 0.3; // Fraction of the window shown before the playhead.
constexpr size_t kMaxGridLines = 400; // Finer levels are dropped beyond this.
constexpr int kMaxPlotPoints = 4000;  // Per line; longer visible ranges are decimated.
constexpr float kMinDisplayDb = -120.0f;
constexpr float kMaxDisplayDb = 0.0f;
constexpr double kFrequencyTicksHz[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
constexpr const char *kFrequencyTickLabels[] = {"50", "100", "200", "500", "1k", "2k", "5k", "10k"};

ColourTable viridis_table() {
    ColourTable table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = ImGui::ColorConvertFloat4ToU32(
            ImPlot::SampleColormap(static_cast<float>(i) / 255.0f, ImPlotColormap_Viridis));
    }
    return table;
}

// Plots the visible part of a per-frame series, decimated to at most kMaxPlotPoints.
void plot_series(const char *label, const std::vector<float> &values, double frame_s, double from_s,
                 double to_s) {
    const int frames = static_cast<int>(values.size());
    const int first = std::clamp(static_cast<int>(std::floor(from_s / frame_s)), 0, frames);
    const int end = std::clamp(static_cast<int>(std::ceil(to_s / frame_s)) + 1, first, frames);
    const int step = std::max(1, (end - first) / kMaxPlotPoints);
    const int count = (end - first) / step;
    ImPlot::PlotLine(label, values.data() + first, count, frame_s * step, first * frame_s, 0, 0,
                     static_cast<int>(step * sizeof(float)));
}

} // namespace

void AnalysisView::draw() {
    ImGui::Begin("Analysis");
    if (!session_.has_track()) {
        ImGui::TextUnformatted("No track loaded.");
        ImGui::End();
        return;
    }
    const LoadedTrack &track = session_.track();
    const FrontEndResult *analysis = track.analysis.get();
    if (analysis == nullptr) {
        ImGui::TextUnformatted("Analysis failed; see the log.");
        ImGui::End();
        return;
    }
    session_.grid.sample_rate_hz = track.sample_rate_hz;
    draw_toolbar(track);
    if (!textures_.built_for(analysis) || colours_changed_) {
        textures_.build(*analysis, min_db_, max_db_, viridis_table());
        colours_changed_ = false;
    }

    const double frame_s = static_cast<double>(analysis->hop_samples) / analysis->sample_rate_hz;
    const double playhead_s =
        samples_to_seconds(session_.transport().position(), track.sample_rate_hz);
    const double follow_from_s = playhead_s - kPlayheadLead * window_s_;
    const auto setup_time_axis = [&] {
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoLabel);
        if (follow_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, follow_from_s, follow_from_s + window_s_,
                                    ImPlotCond_Always);
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, window_s_, ImPlotCond_Once);
        }
    };

    float row_ratios[] = {0.55f, 0.25f, 0.20f};
    if (!ImPlot::BeginSubplots("##analysis", 3, 1, ImVec2(-1, -1), ImPlotSubplotFlags_LinkAllX,
                               row_ratios)) {
        ImGui::End();
        return;
    }

    const LogSpectrogram &spectrogram = analysis->spectrogram;
    if (ImPlot::BeginPlot("Spectrogram", ImVec2(), ImPlotFlags_NoLegend)) {
        setup_time_axis();
        ImPlot::SetupAxis(ImAxis_Y1, "Hz", ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, spectrogram.rows, ImPlotCond_Always);
        // Row r has centre frequency min * (max / min)^(r / (rows - 1)) and spans [r, r + 1).
        std::vector<double> tick_rows;
        const double log_range = std::log(spectrogram.row_hz.back() / spectrogram.row_hz.front());
        for (const double hz : kFrequencyTicksHz) {
            tick_rows.push_back(std::log(hz / spectrogram.row_hz.front()) / log_range *
                                    (spectrogram.rows - 1) +
                                0.5);
        }
        ImPlot::SetupAxisTicks(ImAxis_Y1, tick_rows.data(), static_cast<int>(tick_rows.size()),
                               kFrequencyTickLabels);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        textures_.draw(limits.X.Min, limits.X.Max);
        draw_grid_lines(limits.X.Min, limits.X.Max);
        draw_playhead(playhead_s);
        double downbeat_s = samples_to_seconds(session_.grid.first_downbeat, track.sample_rate_hz);
        if (ImPlot::DragLineX(1, &downbeat_s, ImVec4(1.0f, 0.4f, 0.3f, 1.0f), 2.0f)) {
            session_.grid.first_downbeat = seconds_to_samples(
                std::clamp(downbeat_s, 0.0, track.duration_s), track.sample_rate_hz);
        }
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("Band energy", ImVec2(), ImPlotFlags_None)) {
        setup_time_axis();
        ImPlot::SetupAxis(ImAxis_Y1, "dBFS");
        ImPlot::SetupAxisLimits(ImAxis_Y1, -80.0, 0.0, ImPlotCond_Once);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        for (const BandTrack &band : analysis->bands) {
            plot_series(band.band.name.c_str(), band.energy_db, frame_s, limits.X.Min,
                        limits.X.Max);
        }
        draw_grid_lines(limits.X.Min, limits.X.Max);
        draw_playhead(playhead_s);
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("Spectral flux", ImVec2(), ImPlotFlags_NoLegend)) {
        setup_time_axis();
        ImPlot::SetupAxis(ImAxis_Y1, "flux", ImPlotAxisFlags_AutoFit);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        plot_series("total", analysis->flux_total, frame_s, limits.X.Min, limits.X.Max);
        draw_grid_lines(limits.X.Min, limits.X.Max);
        draw_playhead(playhead_s);
        ImPlot::EndPlot();
    }
    ImPlot::EndSubplots();
    ImGui::End();
}

void AnalysisView::draw_toolbar(const LoadedTrack &track) {
    Grid &grid = session_.grid;
    const float em = ImGui::GetFontSize();
    double bpm = grid.bpm;
    ImGui::SetNextItemWidth(7.0f * em);
    if (ImGui::InputDouble("BPM", &bpm, 0.01, 1.0, "%.2f")) {
        grid.bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
    }
    ImGui::SameLine();
    double downbeat_s = samples_to_seconds(grid.first_downbeat, track.sample_rate_hz);
    ImGui::SetNextItemWidth(8.0f * em);
    if (ImGui::InputDouble("First downbeat (s)", &downbeat_s, 0.001, 0.01, "%.3f")) {
        grid.first_downbeat =
            seconds_to_samples(std::clamp(downbeat_s, 0.0, track.duration_s), track.sample_rate_hz);
    }
    ImGui::SameLine();
    if (ImGui::Button("Set to playhead")) {
        grid.first_downbeat = session_.transport().position();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow playhead", &follow_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(8.0f * em);
    ImGui::SliderFloat("Window", &window_s_, 2.0f, 30.0f, "%.0f s");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(12.0f * em);
    if (ImGui::DragFloatRange2("Colour range", &min_db_, &max_db_, 0.5f, kMinDisplayDb,
                               kMaxDisplayDb, "%.0f dB", "%.0f dB", ImGuiSliderFlags_AlwaysClamp)) {
        max_db_ = std::max(max_db_, min_db_ + 1.0f);
        colours_changed_ = true;
    }
}

void AnalysisView::draw_grid_lines(double from_s, double to_s) const {
    const Grid &grid = session_.grid;
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

void AnalysisView::draw_playhead(double playhead_s) const {
    ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, 0.95f), 2.0f);
    ImPlot::PlotInfLines("##playhead", &playhead_s, 1);
}

} // namespace psyight
