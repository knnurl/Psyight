#include "gui/transport_panel.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace psyight {

namespace {

constexpr float kMinVolumeDb = -60.0f; // The bottom of the slider mutes.
constexpr float kMaxVolumeDb = 6.0f;
constexpr double kDefaultLoopLengthS = 6.0; // Four bars at 160 BPM.
constexpr float kHandleGrabPixels = 6.0f;

std::string format_time(double seconds) {
    const int total_ms = static_cast<int>(std::lround(std::max(0.0, seconds) * 1000.0));
    char text[32];
    std::snprintf(text, sizeof(text), "%d:%02d.%03d", total_ms / 60000, total_ms / 1000 % 60,
                  total_ms % 1000);
    return text;
}

float volume_to_gain(float volume_db) {
    return volume_db <= kMinVolumeDb ? 0.0f : std::pow(10.0f, volume_db / 20.0f);
}

} // namespace

void TransportPanel::draw() {
    Transport &transport = session_.transport();
    ImGui::Begin("Transport");
    if (!session_.has_track()) {
        ImGui::TextUnformatted(session_.status().empty()
                                   ? "Drop a WAV, FLAC or MP3 file onto the window."
                                   : session_.status().c_str());
        ImGui::End();
        return;
    }
    const LoadedTrack &track = session_.track();
    if (shown_track_ != &track) { // A new track: the transport starts with default state.
        shown_track_ = &track;
        loop_enabled_ = false;
        transport.set_gain(volume_to_gain(volume_db_));
    }

    const bool playing = transport.playing();
    const ImGuiIO &io = ImGui::GetIO();
    if (ImGui::Button(playing ? "Pause" : "Play") ||
        (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !io.WantTextInput)) {
        playing ? transport.pause() : transport.play();
    }
    ImGui::SameLine();
    const double position_s = samples_to_seconds(transport.position(), track.sample_rate_hz);
    ImGui::Text("%s / %s   %s", format_time(position_s).c_str(),
                format_time(track.duration_s).c_str(), track.name.c_str());
    if (!session_.status().empty()) {
        ImGui::TextUnformatted(session_.status().c_str());
    }

    if (ImGui::Checkbox("Loop", &loop_enabled_)) {
        if (loop_enabled_ && loop_end_s_ <= loop_start_s_) {
            loop_start_s_ = position_s;
            loop_end_s_ = std::min(track.duration_s, position_s + kDefaultLoopLengthS);
        }
        transport.set_loop(
            loop_enabled_ ? seconds_to_samples(loop_start_s_, track.sample_rate_hz) : 0,
            loop_enabled_ ? seconds_to_samples(loop_end_s_, track.sample_rate_hz) : 0);
    }
    if (loop_enabled_) {
        ImGui::SameLine();
        ImGui::Text("%s to %s", format_time(loop_start_s_).c_str(),
                    format_time(loop_end_s_).c_str());
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(10.0f * ImGui::GetFontSize());
    if (ImGui::SliderFloat("Volume", &volume_db_, kMinVolumeDb, kMaxVolumeDb, "%.1f dB")) {
        transport.set_gain(volume_to_gain(volume_db_));
    }

    draw_overview(track);
    ImGui::End();
}

void TransportPanel::draw_overview(const LoadedTrack &track) {
    Transport &transport = session_.transport();
    if (!ImPlot::BeginPlot("##overview", ImVec2(-1, -1), ImPlotFlags_CanvasOnly)) {
        return;
    }
    // The overview always shows the whole track, so both axes are locked.
    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_Lock,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock);
    ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, track.duration_s, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, 1.0, ImPlotCond_Always);
    ImPlot::PlotShaded("##wave", track.overview_time_s.data(), track.overview_min.data(),
                       track.overview_max.data(), static_cast<int>(track.overview_time_s.size()));

    if (loop_enabled_) {
        const double before_start = loop_start_s_;
        const double before_end = loop_end_s_;
        const ImVec4 loop_colour(0.95f, 0.75f, 0.2f, 1.0f);
        ImPlot::DragLineX(1, &loop_start_s_, loop_colour, 2.0f);
        ImPlot::DragLineX(2, &loop_end_s_, loop_colour, 2.0f);
        loop_start_s_ = std::clamp(loop_start_s_, 0.0, track.duration_s);
        loop_end_s_ = std::clamp(loop_end_s_, 0.0, track.duration_s);
        if (loop_start_s_ != before_start || loop_end_s_ != before_end) {
            transport.set_loop(
                seconds_to_samples(std::min(loop_start_s_, loop_end_s_), track.sample_rate_hz),
                seconds_to_samples(std::max(loop_start_s_, loop_end_s_), track.sample_rate_hz));
        }
    }

    const double playhead_s = samples_to_seconds(transport.position(), track.sample_rate_hz);
    ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), 1.5f);
    ImPlot::PlotInfLines("##playhead", &playhead_s, 1);

    // Click to seek, unless the click is on a loop handle. (The plot itself becomes the active
    // item on click, so ImGui's active-item state cannot tell the two apart.)
    const auto near_handle = [&](double x_s) {
        return std::abs(ImPlot::PlotToPixels(x_s, 0.0).x - ImGui::GetMousePos().x) <
               kHandleGrabPixels;
    };
    if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !(loop_enabled_ && (near_handle(loop_start_s_) || near_handle(loop_end_s_)))) {
        const double target_s = std::clamp(ImPlot::GetPlotMousePos().x, 0.0, track.duration_s);
        transport.seek(seconds_to_samples(target_s, track.sample_rate_hz));
    }
    ImPlot::EndPlot();
}

} // namespace psyight
