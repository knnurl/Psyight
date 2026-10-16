#include "gui/analysis_view.h"

#include "gui/time_plot.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace psyight {

namespace {

constexpr double kMinBpm = 60.0;
constexpr double kMaxBpm = 250.0;
constexpr int kMaxPlotPoints = 4000; // Per line; longer visible ranges are decimated.
constexpr float kMinDisplayDb = -120.0f;
constexpr float kMaxDisplayDb = 0.0f;
constexpr double kFrequencyTicksHz[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
constexpr const char *kFrequencyTickLabels[] = {"50", "100", "200", "500", "1k", "2k", "5k", "10k"};
// Shares of the height for the spectrogram, band energy and flux plots, and a plot's minimum
// height in lines of text.
constexpr std::array<float, 3> kPlotRatios{0.55f, 0.25f, 0.20f};
constexpr float kMinPlotHeight = 4.0f;

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
    draw_toolbar(track);
    if (!textures_.built_for(&analysis->spectrogram) || colours_changed_) {
        textures_.build(analysis->spectrogram, analysis->hop_samples, analysis->sample_rate_hz,
                        min_db_, max_db_, implot_colour_table(ImPlotColormap_Viridis));
        colours_changed_ = false;
    }

    const double frame_s = static_cast<double>(analysis->hop_samples) / analysis->sample_rate_hz;
    const double playhead_s = session_.playhead_s();
    const Grid grid = session_.grid();

    // Each plot folds under its own header; the open ones share the height by their ratios. The
    // heights use the previous frame's open states (a one-frame lag when folding).
    float open_ratio = 0.0f;
    int open_count = 0;
    for (size_t i = 0; i < kPlotRatios.size(); ++i) {
        open_ratio += open_[i] ? kPlotRatios[i] : 0.0f;
        open_count += open_[i];
    }
    const float available =
        ImGui::GetContentRegionAvail().y -
        static_cast<float>(kPlotRatios.size()) * ImGui::GetFrameHeightWithSpacing() -
        static_cast<float>(open_count) * ImGui::GetStyle().ItemSpacing.y;
    const auto height = [&](size_t i) {
        return std::max(kMinPlotHeight * ImGui::GetFontSize(),
                        available * kPlotRatios[i] / std::max(open_ratio, 1e-3f));
    };

    const LogSpectrogram &spectrogram = analysis->spectrogram;
    open_[0] = ImGui::CollapsingHeader("Spectrogram", ImGuiTreeNodeFlags_DefaultOpen);
    if (open_[0] &&
        ImPlot::BeginPlot("##spectrogram", ImVec2(-1, height(0)), ImPlotFlags_NoLegend)) {
        setup_time_axis(session_.view);
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
        draw_grid_lines(grid, limits.X.Min, limits.X.Max);
        if (track.track_analysis != nullptr) {
            draw_sections(track.track_analysis->sections, limits.X.Min, limits.X.Max);
        }
        draw_playhead(playhead_s);
        // Dragging the downbeat is one undoable gesture from press to release.
        LabelEditor &labels = session_.labels();
        LabelGrid label_grid = labels.labels().grid;
        if (ImPlot::DragLineX(1, &label_grid.first_downbeat_s, ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                              2.0f)) {
            if (!dragging_downbeat_) {
                labels.begin_gesture();
                dragging_downbeat_ = true;
            }
            label_grid.first_downbeat_s =
                std::clamp(label_grid.first_downbeat_s, 0.0, track.duration_s);
            labels.set_grid(label_grid);
        }
        if (dragging_downbeat_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            labels.end_gesture();
            dragging_downbeat_ = false;
        }
        std::vector<double> handles = draw_loop(session_, true, 10);
        handles.push_back(label_grid.first_downbeat_s);
        seek_on_click(session_, handles);
        ImPlot::EndPlot();
    }
    open_[1] = ImGui::CollapsingHeader("Band energy", ImGuiTreeNodeFlags_DefaultOpen);
    if (open_[1] && ImPlot::BeginPlot("##band energy", ImVec2(-1, height(1)), ImPlotFlags_None)) {
        setup_time_axis(session_.view);
        ImPlot::SetupAxis(ImAxis_Y1, "dBFS");
        ImPlot::SetupAxisLimits(ImAxis_Y1, -80.0, 0.0, ImPlotCond_Once);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        for (const BandTrack &band : analysis->bands) {
            plot_series(band.band.name.c_str(), band.energy_db, frame_s, limits.X.Min,
                        limits.X.Max);
        }
        draw_grid_lines(grid, limits.X.Min, limits.X.Max);
        draw_playhead(playhead_s);
        seek_on_click(session_, draw_loop(session_, false, 20));
        ImPlot::EndPlot();
    }
    open_[2] = ImGui::CollapsingHeader("Spectral flux", ImGuiTreeNodeFlags_DefaultOpen);
    if (open_[2] &&
        ImPlot::BeginPlot("##spectral flux", ImVec2(-1, height(2)), ImPlotFlags_NoLegend)) {
        setup_time_axis(session_.view);
        ImPlot::SetupAxis(ImAxis_Y1, "flux", ImPlotAxisFlags_AutoFit);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        plot_series("total", analysis->flux_total, frame_s, limits.X.Min, limits.X.Max);
        draw_grid_lines(grid, limits.X.Min, limits.X.Max);
        draw_playhead(playhead_s);
        seek_on_click(session_, draw_loop(session_, false, 30));
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void AnalysisView::draw_toolbar(const LoadedTrack &track) {
    LabelEditor &labels = session_.labels();
    LabelGrid grid = labels.labels().grid;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(7.0f * em);
    if (ImGui::InputDouble("BPM", &grid.bpm, 0.01, 1.0, "%.2f")) {
        grid.bpm = std::clamp(grid.bpm, kMinBpm, kMaxBpm);
        labels.set_grid(grid);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(8.0f * em);
    if (ImGui::InputDouble("First downbeat (s)", &grid.first_downbeat_s, 0.001, 0.01, "%.3f")) {
        grid.first_downbeat_s = std::clamp(grid.first_downbeat_s, 0.0, track.duration_s);
        labels.set_grid(grid);
    }
    ImGui::SameLine();
    if (ImGui::Button("Set to playhead")) {
        grid.first_downbeat_s = session_.playhead_s();
        labels.set_grid(grid);
    }
    if (track.track_analysis != nullptr && track.track_analysis->beats.bpm > 0.0) {
        const BeatTrackResult &beats = track.track_analysis->beats;
        const double downbeat_s =
            samples_to_seconds(beats.grid.first_downbeat, beats.grid.sample_rate_hz);
        ImGui::SameLine();
        if (ImGui::Button("Use detected grid")) {
            labels.set_grid({beats.bpm, downbeat_s});
        }
        ImGui::SetItemTooltip("The beat tracker's grid: %.3f BPM (confidence %.2f), first "
                              "downbeat %.3f s (confidence %.2f). One undo step; check the "
                              "downbeat by ear.",
                              beats.bpm, beats.tempo_confidence, downbeat_s,
                              beats.downbeat_confidence);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow playhead", &session_.view.follow_playhead);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(8.0f * em);
    float window_s = static_cast<float>(session_.view.window_s);
    if (ImGui::SliderFloat("Window", &window_s, 2.0f, 30.0f, "%.0f s")) {
        session_.view.window_s = window_s;
        if (!session_.view.follow_playhead) {
            session_.view.max_s = session_.view.min_s + window_s;
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(12.0f * em);
    if (ImGui::DragFloatRange2("Colour range", &min_db_, &max_db_, 0.5f, kMinDisplayDb,
                               kMaxDisplayDb, "%.0f dB", "%.0f dB", ImGuiSliderFlags_AlwaysClamp)) {
        max_db_ = std::max(max_db_, min_db_ + 1.0f);
        colours_changed_ = true;
    }
}

} // namespace psyight