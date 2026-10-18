#include "gui/transport_panel.h"

#include "gui/theme.h"

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
constexpr float kAuditionSpeed = 0.5f; // Labelling audition: 16ths at 160 BPM become 188 ms.

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
        transport.set_gain(volume_to_gain(volume_db_));
    }
    LoopRegion loop = session_.loop();

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

    if (ImGui::Checkbox("Loop", &loop.enabled)) {
        if (loop.enabled && loop.end_s <= loop.start_s) {
            loop.start_s = position_s;
            loop.end_s = std::min(track.duration_s, position_s + kDefaultLoopLengthS);
        }
        session_.set_loop(loop);
    }
    if (loop.enabled) {
        ImGui::SameLine();
        ImGui::Text("%s to %s", format_time(loop.start_s).c_str(), format_time(loop.end_s).c_str());
    }
    ImGui::SameLine();
    bool half_speed = transport.speed() < 1.0f;
    if (ImGui::Checkbox("Half speed (pitch kept)", &half_speed)) {
        transport.set_speed(half_speed ? kAuditionSpeed : 1.0f);
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
    if (!begin_plot("##overview", ImVec2(-1, -1), ImPlotFlags_CanvasOnly)) {
        return;
    }
    // The overview always shows the whole track, so both axes are locked.
    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_Lock,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock);
    ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, track.duration_s, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, 1.0, ImPlotCond_Always);
    // The played part (left of the playhead) a little brighter than the rest.
    const double played_s = samples_to_seconds(transport.position(), track.sample_rate_hz);
    const auto played = static_cast<int>(
        std::upper_bound(track.overview_time_s.begin(), track.overview_time_s.end(), played_s) -
        track.overview_time_s.begin());
    const int buckets = static_cast<int>(track.overview_time_s.size());
    // A shaded plot needs two points or more (ImPlot draws count - 1 segments); the two parts
    // share the bucket at the playhead so there is no gap between them.
    if (played >= 2) {
        ImPlot::SetNextFillStyle(theme().waveform_played);
        ImPlot::PlotShaded("##played", track.overview_time_s.data(), track.overview_min.data(),
                           track.overview_max.data(), played);
    }
    const int from = std::max(0, played - 1);
    if (buckets - from >= 2) {
        ImPlot::SetNextFillStyle(theme().waveform);
        ImPlot::PlotShaded("##unplayed", track.overview_time_s.data() + from,
                           track.overview_min.data() + from, track.overview_max.data() + from,
                           buckets - from);
    }

    LoopRegion loop = session_.loop();
    if (loop.enabled) {
        const bool moved = ImPlot::DragLineX(1, &loop.start_s, theme().loop, 2.0f) |
                           ImPlot::DragLineX(2, &loop.end_s, theme().loop, 2.0f);
        if (moved) {
            loop.start_s = std::clamp(loop.start_s, 0.0, track.duration_s);
            loop.end_s = std::clamp(loop.end_s, 0.0, track.duration_s);
            session_.set_loop(loop);
        }
    }
    // The visible range of the other plots, so it is easy to see where they are looking.
    double view_bottom = -1.0;
    double view_top = 1.0;
    ImPlot::DragRect(3, &session_.view.min_s, &view_bottom, &session_.view.max_s, &view_top,
                     theme().view_rect, ImPlotDragToolFlags_NoInputs);

    const double playhead_s = samples_to_seconds(transport.position(), track.sample_rate_hz);
    ImPlot::SetNextLineStyle(theme().playhead, 2.0f);
    ImPlot::PlotInfLines("##playhead", &playhead_s, 1);

    // Click to seek, unless the click is on a loop handle. (The plot itself becomes the active
    // item on click, so ImGui's active-item state cannot tell the two apart.)
    const auto near_handle = [&](double x_s) {
        return std::abs(ImPlot::PlotToPixels(x_s, 0.0).x - ImGui::GetMousePos().x) <
               kHandleGrabPixels;
    };
    if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !(loop.enabled && (near_handle(loop.start_s) || near_handle(loop.end_s)))) {
        const double target_s = std::clamp(ImPlot::GetPlotMousePos().x, 0.0, track.duration_s);
        transport.seek(seconds_to_samples(target_s, track.sample_rate_hz));
    }
    end_plot();
}

} // namespace psyight
