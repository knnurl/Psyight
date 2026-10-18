#include "gui/log_view.h"

#include "gui/theme.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <ctime>
#include <string>
#include <vector>

namespace psyight {

namespace {

constexpr size_t kKeptMessages = 1000;

ImVec4 level_colour(spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::warn:
        return theme().warning;
    case spdlog::level::err:
    case spdlog::level::critical:
        return theme().danger;
    case spdlog::level::debug:
    case spdlog::level::trace:
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    default:
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

} // namespace

LogView::LogView() : sink_(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(kKeptMessages)) {
    spdlog::default_logger()->sinks().push_back(sink_);
}

void LogView::draw() {
    if (!ImGui::Begin("Log")) {
        ImGui::End();
        return; // Hidden: no copy.
    }
    ImGui::Checkbox("Warnings and errors only", &problems_only_);
    ImGui::SameLine();
    ImGui::Checkbox("Follow the newest", &follow_);
    const std::vector<spdlog::details::log_msg_buffer> messages = sink_->last_raw();
    std::vector<const spdlog::details::log_msg_buffer *> shown;
    for (const spdlog::details::log_msg_buffer &m : messages) {
        if (!problems_only_ || m.level >= spdlog::level::warn) {
            shown.push_back(&m);
        }
    }
    if (ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const spdlog::details::log_msg_buffer &m = *shown[i];
                const std::time_t seconds = std::chrono::system_clock::to_time_t(m.time);
                std::tm local{};
                localtime_s(&local, &seconds);
                char stamp[16];
                std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
                const spdlog::string_view_t level = spdlog::level::to_string_view(m.level);
                ImGui::TextColored(level_colour(m.level), "%s %-8.*s %.*s", stamp,
                                   static_cast<int>(level.size()), level.data(),
                                   static_cast<int>(m.payload.size()), m.payload.data());
            }
        }
        if (follow_ && ImGui::GetScrollY() < ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace psyight
