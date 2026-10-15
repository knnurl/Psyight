#include "gui/transport_panel.h"

#include "core/paths.h"

#include <imgui.h>
#include <implot.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace psyight {

namespace {

constexpr int kOverviewBuckets = 4000;
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

void TransportPanel::open_file(const std::filesystem::path &path) {
    if (loading_.valid()) {
        status_ = "Still loading " + loading_name_;
        return;
    }
    loading_name_ = path_to_utf8(path.filename());
    status_ = "Loading " + loading_name_ + "...";
    loading_ = std::async(std::launch::async, &TransportPanel::load, path);
}

TransportPanel::LoadedFile TransportPanel::load(std::filesystem::path path) {
    LoadedFile file;
    file.audio = decode_audio_file(path);
    if (!file.audio) {
        return file;
    }
    const AudioBuffer &buffer = file.audio.value();
    const SampleIndex frames = buffer.frames();
    const int buckets = static_cast<int>(std::min<SampleIndex>(kOverviewBuckets, frames));
    for (int b = 0; b < buckets; ++b) {
        const SampleIndex begin = frames * b / buckets;
        const SampleIndex end = frames * (b + 1) / buckets;
        float low = 0.0f;
        float high = 0.0f;
        for (SampleIndex i = begin; i < end; ++i) {
            const float mono = 0.5f * (buffer.samples[2 * i] + buffer.samples[2 * i + 1]);
            low = std::min(low, mono);
            high = std::max(high, mono);
        }
        file.overview_time_s.push_back(
            samples_to_seconds((begin + end) / 2, buffer.sample_rate_hz));
        file.overview_min.push_back(low);
        file.overview_max.push_back(high);
    }
    return file;
}

void TransportPanel::finish_loading() {
    if (!loading_.valid() ||
        loading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    LoadedFile file = loading_.get();
    if (!file.audio) {
        status_ = file.audio.error().message;
        spdlog::error("{}", status_);
        return;
    }
    duration_s_ =
        samples_to_seconds(file.audio.value().frames(), file.audio.value().sample_rate_hz);
    const Result<void> opened = transport_.open(std::move(file.audio.value()));
    if (!opened) {
        status_ = opened.error().message;
        spdlog::error("{}", status_);
        return;
    }
    overview_time_s_ = std::move(file.overview_time_s);
    overview_min_ = std::move(file.overview_min);
    overview_max_ = std::move(file.overview_max);
    file_name_ = loading_name_;
    loop_enabled_ = false;
    transport_.set_gain(volume_to_gain(volume_db_));
    status_.clear();
    spdlog::info("Loaded {} ({:.2f} s), device period {} frames", file_name_, duration_s_,
                 transport_.device_period_frames());
}

void TransportPanel::draw() {
    finish_loading();
    const float em = ImGui::GetFontSize(); // Sizes in font units follow the DPI scale.
    ImGui::SetNextWindowSize(ImVec2(80.0f * em, 14.0f * em), ImGuiCond_FirstUseEver);
    ImGui::Begin("Transport");
    if (!transport_.is_open()) {
        ImGui::TextUnformatted(status_.empty() ? "Drop a WAV, FLAC or MP3 file onto the window."
                                               : status_.c_str());
        ImGui::End();
        return;
    }

    const int sample_rate_hz = transport_.buffer().sample_rate_hz;
    const bool playing = transport_.playing();
    const ImGuiIO &io = ImGui::GetIO();
    if (ImGui::Button(playing ? "Pause" : "Play") ||
        (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !io.WantTextInput)) {
        playing ? transport_.pause() : transport_.play();
    }
    ImGui::SameLine();
    const double position_s = samples_to_seconds(transport_.position(), sample_rate_hz);
    ImGui::Text("%s / %s   %s", format_time(position_s).c_str(), format_time(duration_s_).c_str(),
                file_name_.c_str());
    if (!status_.empty()) {
        ImGui::TextUnformatted(status_.c_str());
    }

    if (ImGui::Checkbox("Loop", &loop_enabled_)) {
        if (loop_enabled_ && loop_end_s_ <= loop_start_s_) {
            loop_start_s_ = position_s;
            loop_end_s_ = std::min(duration_s_, position_s + kDefaultLoopLengthS);
        }
        transport_.set_loop(loop_enabled_ ? seconds_to_samples(loop_start_s_, sample_rate_hz) : 0,
                            loop_enabled_ ? seconds_to_samples(loop_end_s_, sample_rate_hz) : 0);
    }
    if (loop_enabled_) {
        ImGui::SameLine();
        ImGui::Text("%s to %s", format_time(loop_start_s_).c_str(),
                    format_time(loop_end_s_).c_str());
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Volume", &volume_db_, kMinVolumeDb, kMaxVolumeDb, "%.1f dB")) {
        transport_.set_gain(volume_to_gain(volume_db_));
    }

    draw_overview();
    ImGui::End();
}

void TransportPanel::draw_overview() {
    const int sample_rate_hz = transport_.buffer().sample_rate_hz;
    if (!ImPlot::BeginPlot("##overview", ImVec2(-1, -1), ImPlotFlags_CanvasOnly)) {
        return;
    }
    // The overview always shows the whole track, so both axes are locked.
    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_Lock,
                      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock);
    ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, duration_s_, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, 1.0, ImPlotCond_Always);
    ImPlot::PlotShaded("##wave", overview_time_s_.data(), overview_min_.data(),
                       overview_max_.data(), static_cast<int>(overview_time_s_.size()));

    if (loop_enabled_) {
        const double before_start = loop_start_s_;
        const double before_end = loop_end_s_;
        const ImVec4 loop_colour(0.95f, 0.75f, 0.2f, 1.0f);
        ImPlot::DragLineX(1, &loop_start_s_, loop_colour, 2.0f);
        ImPlot::DragLineX(2, &loop_end_s_, loop_colour, 2.0f);
        loop_start_s_ = std::clamp(loop_start_s_, 0.0, duration_s_);
        loop_end_s_ = std::clamp(loop_end_s_, 0.0, duration_s_);
        if (loop_start_s_ != before_start || loop_end_s_ != before_end) {
            transport_.set_loop(
                seconds_to_samples(std::min(loop_start_s_, loop_end_s_), sample_rate_hz),
                seconds_to_samples(std::max(loop_start_s_, loop_end_s_), sample_rate_hz));
        }
    }

    const double playhead_s = samples_to_seconds(transport_.position(), sample_rate_hz);
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
        const double target_s = std::clamp(ImPlot::GetPlotMousePos().x, 0.0, duration_s_);
        transport_.seek(seconds_to_samples(target_s, sample_rate_hz));
    }
    ImPlot::EndPlot();
}

} // namespace psyight
