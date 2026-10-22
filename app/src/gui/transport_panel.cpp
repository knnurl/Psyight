#include "gui/transport_panel.h"

#include "gui/icons.h"
#include "gui/layout.h"
#include "gui/theme.h"
#include "gui/widgets.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace psyight {

namespace {

constexpr float kMinVolumeDb = -60.0f; // The bottom of the slider mutes.
constexpr float kMaxVolumeDb = 6.0f;
constexpr double kDefaultLoopLengthS = 6.0; // Four bars at 160 BPM.
constexpr float kHandleGrabPixels = 6.0f;
constexpr float kAuditionSpeed = 0.5f;    // Labelling audition: 16ths at 160 BPM become 188 ms.
constexpr float kControlsWidthEm = 22.0f; // The controls beside the overview.
constexpr float kVolumeWidthEm = 6.0f;

// An icon button's label with a stable ID (the icon may change).
std::string icon_label(const char *icon, const char *id) { return std::string(icon) + "###" + id; }

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

void TransportPanel::apply_settings(const UserSettings &settings) {
    volume_db_ = std::clamp(settings.volume_db, kMinVolumeDb, kMaxVolumeDb);
}

void TransportPanel::draw() {
    Transport &transport = session_.transport();
    ImGui::SetNextWindowClass(&transport_window_class()); // A pinned strip without a tab.
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
    const float em = ImGui::GetFontSize();

    // The controls, two rows, beside the overview (RESTYLE Phase 4).
    ImGui::BeginGroup();
    ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                        ImVec2(ImGui::GetCursorScreenPos().x + kControlsWidthEm * em, 1e9f), true);
    // Row 1: play, loop, half speed, the playhead time.
    const bool playing = transport.playing();
    const ImGuiIO &io = ImGui::GetIO();
    const bool play_clicked =
        primary_button(icon_label(playing ? kIconPause : kIconPlay, "play").c_str());
    ImGui::SetItemTooltip(playing ? "Pause (Space)" : "Play (Space)");
    if (play_clicked ||
        (keys_enabled_ && ImGui::IsKeyPressed(ImGuiKey_Space, false) && !io.WantTextInput)) {
        playing ? transport.pause() : transport.play();
    }
    ImGui::SameLine();
    const double position_s = samples_to_seconds(transport.position(), track.sample_rate_hz);
    if (toggle_button(icon_label(kIconLoop, "loop").c_str(), loop.enabled)) {
        loop.enabled = !loop.enabled;
        if (loop.enabled && loop.end_s <= loop.start_s) {
            loop.start_s = position_s;
            loop.end_s = std::min(track.duration_s, position_s + kDefaultLoopLengthS);
        }
        session_.set_loop(loop);
    }
    ImGui::SetItemTooltip("Loop. Drag its ends on the waveform.");
    ImGui::SameLine();
    const bool half_speed = transport.speed() < 1.0f;
    if (toggle_button(icon_label(kIconHalfSpeed, "half speed").c_str(), half_speed)) {
        transport.set_speed(half_speed ? 1.0f : kAuditionSpeed);
    }
    ImGui::SetItemTooltip("Half speed, pitch kept (for labelling).");
    ImGui::SameLine();
    ImGui::PushFont(theme_font(ThemeFont::timecode));
    ImGui::TextUnformatted(format_time(position_s).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    mono_text("/ %s", format_time(track.duration_s).c_str());

    // Row 2: volume, the loop, the track and the session's status.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", kIconVolume);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(kVolumeWidthEm * em);
    if (mono_field("##volume", [&](const char *id) {
            return ImGui::DragFloat(id, &volume_db_, 0.2f, kMinVolumeDb, kMaxVolumeDb,
                                    volume_db_ <= kMinVolumeDb ? "Muted" : "%.1f dB",
                                    ImGuiSliderFlags_AlwaysClamp);
        })) {
        transport.set_gain(volume_to_gain(volume_db_));
    }
    ImGui::SetItemTooltip("Volume. Drag, or double-click to type; the bottom mutes.");
    if (loop.enabled) {
        ImGui::SameLine();
        mono_text("%s to %s", format_time(loop.start_s).c_str(), format_time(loop.end_s).c_str());
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(track.name.c_str());
    ImGui::SetItemTooltip("%s", track.name.c_str());
    if (!session_.status().empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", session_.status().c_str());
        ImGui::SetItemTooltip("%s", session_.status().c_str());
    }
    ImGui::PopClipRect();
    ImGui::EndGroup();

    ImGui::SameLine(kControlsWidthEm * em + ImGui::GetStyle().WindowPadding.x);
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

    // Click to seek, and hold to scrub: the playhead follows the mouse until the button is
    // released, also outside the overview. Not when the click is on a loop handle. (The plot
    // itself becomes the active item on click, so ImGui's active-item state cannot tell the two
    // apart.)
    const auto near_handle = [&](double x_s) {
        return std::abs(ImPlot::PlotToPixels(x_s, 0.0).x - ImGui::GetMousePos().x) <
               kHandleGrabPixels;
    };
    if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !(loop.enabled && (near_handle(loop.start_s) || near_handle(loop.end_s)))) {
        scrubbing_ = true;
        scrub_x_ = -1.0f; // Seek now.
    }
    if (scrubbing_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        scrubbing_ = false;
    }
    if (scrubbing_ && ImGui::GetMousePos().x != scrub_x_) { // Only when the mouse moved.
        scrub_x_ = ImGui::GetMousePos().x;
        const double target_s = std::clamp(ImPlot::GetPlotMousePos().x, 0.0, track.duration_s);
        transport.seek(seconds_to_samples(target_s, track.sample_rate_hz));
    }
    end_plot();
}

} // namespace psyight
