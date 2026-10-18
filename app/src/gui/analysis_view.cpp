#include "gui/analysis_view.h"

#include "gui/theme.h"
#include "gui/time_plot.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
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
constexpr std::array<float, 4> kPlotRatios{0.55f, 0.25f, 0.20f, 0.30f};
constexpr float kMinPlotHeight = 4.0f;
int kWholeNumbers = 0; // Decimals for format_fixed_width (ImPlot takes a non-const pointer).
int kHundredths = 2;
constexpr double kMaxRestSlots = 2000.0; // Rests are shaded only when this few slots are in view.

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
                        min_db_, max_db_, colormap_table(colormap_));
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
    // Plots whose share falls below the minimum get the minimum; the rest share what is left by
    // their ratios, so the open plots always fit (unless even the minimums do not).
    std::array<float, kPlotRatios.size()> heights{};
    {
        const float minimum = kMinPlotHeight * plot_font_size(); // Lines of axis labels.
        float rest = available;
        float rest_ratio = open_ratio;
        std::array<bool, kPlotRatios.size()> fixed{};
        for (size_t i = 0; i < kPlotRatios.size(); ++i) {
            if (open_[i] && available * kPlotRatios[i] / std::max(open_ratio, 1e-3f) < minimum) {
                fixed[i] = true;
                rest -= minimum;
                rest_ratio -= kPlotRatios[i];
            }
        }
        for (size_t i = 0; i < kPlotRatios.size(); ++i) {
            heights[i] = fixed[i] ? minimum
                         : rest > minimum
                             ? rest * kPlotRatios[i] / std::max(rest_ratio, 1e-3f)
                             : available * kPlotRatios[i] / std::max(open_ratio, 1e-3f);
        }
    }
    const auto height = [&](size_t i) { return heights[i]; };

    const LogSpectrogram &spectrogram = analysis->spectrogram;
    open_[0] = ImGui::CollapsingHeader("Spectrogram", ImGuiTreeNodeFlags_DefaultOpen);
    if (open_[0] && begin_plot("##spectrogram", ImVec2(-1, height(0)), ImPlotFlags_NoLegend)) {
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
        std::vector<std::string> tick_names;
        std::vector<const char *> tick_labels;
        for (const char *name : kFrequencyTickLabels) {
            tick_names.push_back(fixed_width_label(name));
        }
        for (const std::string &name : tick_names) {
            tick_labels.push_back(name.c_str());
        }
        ImPlot::SetupAxisTicks(ImAxis_Y1, tick_rows.data(), static_cast<int>(tick_rows.size()),
                               tick_labels.data());
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        textures_.draw(limits.X.Min, limits.X.Max);
        draw_grid_lines(grid, limits.X.Min, limits.X.Max);
        if (track.track_analysis != nullptr) {
            draw_sections(track.track_analysis->sections, track.track_analysis->phrase_starts_s,
                          limits.X.Min, limits.X.Max);
        }
        draw_playhead(playhead_s);
        // Dragging the downbeat is one undoable gesture from press to release.
        LabelEditor &labels = session_.labels();
        LabelGrid label_grid = labels.labels().grid;
        if (ImPlot::DragLineX(1, &label_grid.first_downbeat_s, theme().downbeat_handle, 2.0f)) {
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
        end_plot();
    }
    open_[1] = ImGui::CollapsingHeader("Band energy", ImGuiTreeNodeFlags_DefaultOpen);
    if (open_[1] && begin_plot("##band energy", ImVec2(-1, height(1)), ImPlotFlags_None)) {
        setup_time_axis(session_.view);
        ImPlot::SetupAxis(ImAxis_Y1, "dBFS");
        ImPlot::SetupAxisFormat(ImAxis_Y1, format_fixed_width, &kWholeNumbers);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -80.0, 0.0, ImPlotCond_Once);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        const ThemeTokens &t = theme();
        const ImVec4 band_colours[] = {t.band_sub, t.band_bass, t.band_low_mid, t.band_click,
                                       t.band_hats};
        for (size_t i = 0; i < analysis->bands.size(); ++i) {
            ImPlot::SetNextLineStyle(band_colours[i % std::size(band_colours)]);
            plot_series(analysis->bands[i].band.name.c_str(), analysis->bands[i].energy_db, frame_s,
                        limits.X.Min, limits.X.Max);
        }
        draw_grid_lines(grid, limits.X.Min, limits.X.Max);
        draw_playhead(playhead_s);
        seek_on_click(session_, draw_loop(session_, false, 20));
        end_plot();
    }
    open_[2] = ImGui::CollapsingHeader("Spectral flux", ImGuiTreeNodeFlags_DefaultOpen);
    if (open_[2] && begin_plot("##spectral flux", ImVec2(-1, height(2)), ImPlotFlags_NoLegend)) {
        setup_time_axis(session_.view);
        ImPlot::SetupAxis(ImAxis_Y1, "flux", ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_Y1, format_fixed_width, &kHundredths);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        plot_series("total", analysis->flux_total, frame_s, limits.X.Min, limits.X.Max);
        draw_grid_lines(grid, limits.X.Min, limits.X.Max);
        draw_playhead(playhead_s);
        seek_on_click(session_, draw_loop(session_, false, 30));
        end_plot();
    }
    open_[3] = ImGui::CollapsingHeader("Residual (non-bass) and hats");
    if (open_[3] && begin_plot("##residual", ImVec2(-1, height(3)), ImPlotFlags_None)) {
        setup_time_axis(session_.view);
        ImPlot::SetupAxis(ImAxis_Y1, "0-1", ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, format_fixed_width, &kHundredths);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        if (track.track_analysis != nullptr) {
            draw_residual(*track.track_analysis, grid, frame_s, limits.X.Min, limits.X.Max);
        } else {
            ImPlot::PlotText("No analysis (no tempo found)", 0.5 * (limits.X.Min + limits.X.Max),
                             0.5);
        }
        draw_grid_lines(grid, limits.X.Min, limits.X.Max);
        draw_playhead(playhead_s);
        seek_on_click(session_, draw_loop(session_, false, 40));
        end_plot();
    }
    ImGui::End();
}

void AnalysisView::draw_residual(const TrackAnalysis &analysis, const Grid &grid, double frame_s,
                                 double from_s, double to_s) {
    // Bass rests: 16th slots whose middle no detected note covers (SPEC 6.3), where the residual
    // drives the light.
    ImDrawList *draw = ImPlot::GetPlotDrawList();
    const std::vector<BassNote> &notes = analysis.bass.notes;
    const double slot_s = grid.slot_length_samples() / grid.sample_rate_hz;
    const double first_s = samples_to_seconds(grid.first_downbeat, grid.sample_rate_hz);
    ImPlot::PushPlotClipRect();
    if (slot_s > 0.0 && (to_s - from_s) / slot_s < kMaxRestSlots) {
        auto note = std::lower_bound(notes.begin(), notes.end(), from_s - 4.0,
                                     [](const BassNote &n, double t) { return n.onset_s < t; });
        for (double slot = std::floor((from_s - first_s) / slot_s); first_s + slot * slot_s < to_s;
             slot += 1.0) {
            const double start_s = first_s + slot * slot_s;
            const double middle_s = start_s + 0.5 * slot_s;
            while (note != notes.end() && note->offset_s <= middle_s && note->onset_s <= middle_s) {
                ++note; // Notes ending before this slot's middle cannot cover it or later ones.
            }
            bool covered = false;
            for (auto n = note; n != notes.end() && n->onset_s <= middle_s; ++n) {
                covered = covered || n->offset_s > middle_s;
            }
            if (!covered && middle_s >= 0.0 && middle_s <= analysis.duration_s) {
                draw->AddRectFilled(ImPlot::PlotToPixels(start_s, 1.0),
                                    ImPlot::PlotToPixels(start_s + slot_s, 0.0),
                                    to_u32(theme().rest_shade));
            }
        }
    }
    ImPlot::PopPlotClipRect();

    // Level, centroid and flatness on 0 to 1: level -90 to 0 dB, centroid 0 to 10 kHz.
    const ResidualFeatures &r = analysis.residual;
    const int frames = static_cast<int>(r.energy_db.size());
    const int first = std::clamp(static_cast<int>(std::floor(from_s / frame_s)), 0, frames);
    const int end = std::clamp(static_cast<int>(std::ceil(to_s / frame_s)) + 1, first, frames);
    const int step = std::max(1, (end - first) / kMaxPlotPoints);
    std::vector<float> level, centroid, flatness;
    for (int i = first; i < end; i += step) {
        level.push_back(std::clamp((r.energy_db[i] + 90.0f) / 90.0f, 0.0f, 1.0f));
        centroid.push_back(std::clamp(r.centroid_hz[i] / 10000.0f, 0.0f, 1.0f));
        flatness.push_back(std::clamp(r.flatness[i], 0.0f, 1.0f));
    }
    const auto count = static_cast<int>(level.size());
    ImPlot::PlotLine("level", level.data(), count, frame_s * step, first * frame_s);
    ImPlot::PlotLine("centroid (10 kHz = 1)", centroid.data(), count, frame_s * step,
                     first * frame_s);
    ImPlot::PlotLine("flatness", flatness.data(), count, frame_s * step, first * frame_s);

    // Hats: ticks along the top, taller when stronger.
    ImPlot::PushPlotClipRect();
    for (const HatOnset &hat : r.hats) {
        if (hat.t_s >= from_s && hat.t_s <= to_s) {
            const double length = 0.05 + 0.1 * std::min(1.0, hat.strength);
            draw->AddLine(ImPlot::PlotToPixels(hat.t_s, 1.0),
                          ImPlot::PlotToPixels(hat.t_s, 1.0 - length), to_u32(theme().hat_tick),
                          2.0f);
        }
    }
    ImPlot::PopPlotClipRect();
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
    // Analysis source and re-analysis on the current grid.
    ImGui::SameLine();
    const bool stems = session_.stems_available();
    if (!stems) {
        source_ = AnalysisSource::mix;
    } else if (track.track_analysis != nullptr && !source_chosen_) {
        source_ = track.track_analysis->source;
    }
    ImGui::SetNextItemWidth(6.0f * em);
    if (ImGui::BeginCombo("##source", source_ == AnalysisSource::stems ? "Stems" : "Mix")) {
        if (ImGui::Selectable("Mix", source_ == AnalysisSource::mix)) {
            source_ = AnalysisSource::mix;
            source_chosen_ = true;
        }
        ImGui::BeginDisabled(!stems);
        if (ImGui::Selectable("Stems", source_ == AnalysisSource::stems)) {
            source_ = AnalysisSource::stems;
            source_chosen_ = true;
        }
        ImGui::EndDisabled();
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(stems ? "Analyse the mix, or the Demucs stems (kicks from drums, notes "
                                  "from bass)."
                                : "No stems for this track: run tools/python/separate.py.");
    ImGui::SameLine();
    ImGui::BeginDisabled(session_.reanalysing());
    if (ImGui::Button(session_.reanalysing() ? "Analysing..." : "Re-analyse")) {
        session_.reanalyse(source_);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Runs beats, kicks, bass notes and sections again on the current grid "
                          "(gate ratios and the kick prior depend on it).");
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
    ImGui::SetNextItemWidth(6.0f * em);
    if (ImGui::BeginCombo("##colormap", colormap_name(colormap_))) {
        for (const Colormap map : {Colormap::magma, Colormap::inferno, Colormap::viridis}) {
            if (ImGui::Selectable(colormap_name(map), map == colormap_)) {
                colormap_ = map;
                colours_changed_ = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The spectrogram's colours.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(12.0f * em);
    if (ImGui::DragFloatRange2("##colour range", &min_db_, &max_db_, 0.5f, kMinDisplayDb,
                               kMaxDisplayDb, "%.0f dB", "%.0f dB", ImGuiSliderFlags_AlwaysClamp)) {
        max_db_ = std::max(max_db_, min_db_ + 1.0f);
        colours_changed_ = true;
    }
    ImGui::SetItemTooltip("The spectrogram's colour range (quietest, loudest).");
}

} // namespace psyight