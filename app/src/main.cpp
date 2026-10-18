// Psyight entry point: opens the main window with a Dear ImGui docking workspace.

#include "core/paths.h"
#include "gui/analysis_view.h"
#include "gui/bass_roll_view.h"
#include "gui/capture.h"
#include "gui/command_line.h"
#include "gui/crash_guard.h"
#include "gui/frame_pacing.h"
#include "gui/layout.h"
#include "gui/log_view.h"
#include "gui/preflight_view.h"
#include "gui/preview_view.h"
#include "gui/project_state.h"
#include "gui/rig_view.h"
#include "gui/scoring_view.h"
#include "gui/session.h"
#include "gui/transport_panel.h"
#include "io/png.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h> // DockBuilderGetNode, to detect a missing saved layout.
#include <implot.h>
#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h> // Brings in the system OpenGL header for glClear and glViewport.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kInitialWidthPixels = 1600; // Size when restored from the initial maximised state.
constexpr int kInitialHeightPixels = 900;
constexpr float kDefaultFontSizePixels = 13.0f;
constexpr int kMinimisedSleepMs = 10;
// OpenGL 3.2 or later lets the ImGui backend use vertex offsets, which large ImPlot draws (such as
// spectrograms) need with ImGui's 16-bit indices.
constexpr int kGlVersionMajor = 3;
constexpr int kGlVersionMinor = 3;
constexpr const char *kGlslVersion = "#version 330 core";

void log_glfw_error(int code, const char *description) {
    spdlog::error("GLFW error {}: {}", code, description);
}

// Opens the first file dropped onto the window. GLFW passes UTF-8 paths.
void on_files_dropped(GLFWwindow *window, int count, const char *paths[]) {
    if (count > 0) {
        auto *session = static_cast<psyight::Session *>(glfwGetWindowUserPointer(window));
        session->open_file(std::u8string(reinterpret_cast<const char8_t *>(paths[0])));
    }
}

// Global shortcuts. Text fields keep Ctrl+Z for their own undo: global routing gives way to them.
void handle_shortcuts(psyight::Session &session) {
    if (!session.has_track()) {
        return;
    }
    constexpr ImGuiInputFlags kGlobal = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, kGlobal)) {
        if (const psyight::Result<void> saved = session.save_labels(); saved) {
            spdlog::info("Saved {}", psyight::path_to_utf8(session.labels_path().filename()));
        } else {
            spdlog::error("{}", saved.error().message);
        }
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, kGlobal)) {
        session.labels().undo();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, kGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, kGlobal)) {
        session.labels().redo();
    }
}

} // namespace

int main() {
    // Capture mode (gui/capture.h): a hidden fixed-size window rendered offscreen to a PNG.
    std::optional<psyight::CaptureArgs> capture;
    if (auto parsed = psyight::parse_capture_args(psyight::command_line_paths()); parsed) {
        if (!*parsed) {
            std::fprintf(stderr, "%s\n", parsed->error().message.c_str());
            return 2;
        }
        capture = parsed->value();
    }
    glfwSetErrorCallback(log_glfw_error);
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kGlVersionMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kGlVersionMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (capture) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    } else {
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE); // The scaled size can exceed small screens.
    }
    GLFWwindow *window = capture ? glfwCreateWindow(psyight::kCaptureWidth, psyight::kCaptureHeight,
                                                    "Psyight", nullptr, nullptr)
                                 : glfwCreateWindow(kInitialWidthPixels, kInitialHeightPixels,
                                                    "Psyight", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(psyight::kGuiSwapInterval);
    spdlog::info("OpenGL {} on {}", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char *>(glGetString(GL_RENDERER)));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (capture) {
        io.IniFilename = nullptr; // The default layout, and the owner's imgui.ini untouched.
    }
    ImGui::StyleColorsDark();

    // Scale text and widget sizes to the monitor's DPI setting (1.0 for captures).
    float dpi_scale = 1.0f;
    if (!capture) {
        glfwGetWindowContentScale(window, &dpi_scale, nullptr);
    }
    ImFontConfig font_config;
    font_config.SizePixels = kDefaultFontSizePixels * dpi_scale;
    // AddFontDefault sets these only when given no config; they keep the pixel font sharp.
    font_config.OversampleH = 1;
    font_config.OversampleV = 1;
    font_config.PixelSnapH = true;
    io.Fonts->AddFontDefault(&font_config);
    ImGui::GetStyle().ScaleAllSizes(dpi_scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(kGlslVersion);

    { // App objects own GL textures, so they must go before the GL context does.
        psyight::LogView log_view; // First, so it keeps every message after start-up.
        std::optional<psyight::CrashGuard> crash_guard; // Log file, dumps, unclean-exit marker.
        if (!capture) {
            crash_guard.emplace();
        }
        psyight::Session session;
        psyight::TransportPanel transport_panel(session);
        psyight::AnalysisView analysis_view(session);
        psyight::BassRollView bass_roll_view(session);
        psyight::ScoringView scoring_view(session);
        psyight::ProjectState project;
        psyight::PreviewView preview_view(session, project);
        psyight::RigView rig_view(project);
        psyight::PreflightView preflight_view(session, project, preview_view);
        glfwSetWindowUserPointer(window, &session);
        glfwSetDropCallback(window, on_files_dropped);
        psyight::CaptureTarget capture_target;
        enum class CaptureStage {
            loading,
            seeking,
            rendering
        } capture_stage = CaptureStage::loading;
        psyight::SampleIndex capture_sample = 0;
        int capture_frames = 0;
        std::vector<double> frame_ms;
        const auto capture_started = std::chrono::steady_clock::now();
        if (capture) {
            if (const psyight::Result<void> made =
                    capture_target.create(psyight::kCaptureWidth, psyight::kCaptureHeight);
                !made) {
                std::fprintf(stderr, "%s\n", made.error().message.c_str());
                return 1;
            }
            session.open_file(capture->audio);
        } else if (const auto paths = psyight::command_line_paths(); !paths.empty()) {
            session.open_file(paths.front());
        }
        int capture_status = 0;
        bool reset_layout = false;
        bool focus_preview = false;

        bool show_imgui_demo = false;
        bool show_implot_demo = false;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
                ImGui_ImplGlfw_Sleep(
                    kMinimisedSleepMs); // Swap does not throttle a minimised window.
                continue;
            }
            const auto frame_started = std::chrono::steady_clock::now();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            if (capture) {
                io.DeltaTime = 1.0f / 60.0f; // Fixed steps: the same frames on every run.
                // Windows may shrink a hidden window to fit the screen: lay out for the capture.
                io.DisplaySize = ImVec2(static_cast<float>(psyight::kCaptureWidth),
                                        static_cast<float>(psyight::kCaptureHeight));
                io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
            }
            ImGui::NewFrame();

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Reset layout", nullptr, &reset_layout);
                    ImGui::Separator();
                    ImGui::MenuItem("ImGui demo", nullptr, &show_imgui_demo);
                    ImGui::MenuItem("ImPlot demo", nullptr, &show_implot_demo);
                    ImGui::EndMenu();
                }
                preview_view.draw_top_bar();
                ImGui::EndMainMenuBar();
            }
            // Bump the version when panels are added, so saved layouts get the new default once.
            const ImGuiID dockspace_id = ImGui::GetID("Main dock space v6");
            if (reset_layout || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
                psyight::apply_default_layout(dockspace_id); // First run, or View > Reset layout.
                reset_layout = false;
                focus_preview = true;
            }
            ImGui::DockSpaceOverViewport(dockspace_id);
            session.update();
            transport_panel.draw();
            analysis_view.draw();
            bass_roll_view.draw_roll();
            bass_roll_view.draw_inspector();
            scoring_view.draw();
            log_view.draw();
            preview_view.draw();
            rig_view.draw();
            preflight_view.draw();
            if (focus_preview) { // Show the preview's tab, not the rig's, after a new layout.
                ImGui::SetWindowFocus("Preview");
                focus_preview = false;
            }
            handle_shortcuts(session);
            project.autosave();
            session.end_frame();
            if (show_imgui_demo) {
                ImGui::ShowDemoWindow(&show_imgui_demo);
            }
            if (show_implot_demo) {
                ImPlot::ShowDemoWindow(&show_implot_demo);
            }

            ImGui::Render();
            int framebuffer_width_pixels = 0;
            int framebuffer_height_pixels = 0;
            glfwGetFramebufferSize(window, &framebuffer_width_pixels, &framebuffer_height_pixels);
            if (capture) {
                capture_target.bind();
                framebuffer_width_pixels = psyight::kCaptureWidth;
                framebuffer_height_pixels = psyight::kCaptureHeight;
            }
            glViewport(0, 0, framebuffer_width_pixels, framebuffer_height_pixels);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (!capture) {
                glfwSwapBuffers(window);
                psyight::wait_for_display_refresh();
                continue;
            }
            glFinish();
            capture_target.unbind();
            // Capture: wait for the analysis, seek, wait for the transport, then count frames.
            if (std::chrono::steady_clock::now() - capture_started > std::chrono::minutes(5)) {
                std::fprintf(stderr, "capture: timed out (%s)\n", session.status().c_str());
                capture_status = 1;
                break;
            }
            if (capture_stage == CaptureStage::loading) {
                if (session.has_track() && !session.loading() && !session.reanalysing()) {
                    const psyight::LoadedTrack &track = session.track();
                    capture_sample = psyight::seconds_to_samples(
                        std::clamp(capture->time_s, 0.0, track.duration_s), track.sample_rate_hz);
                    session.transport().seek(capture_sample);
                    capture_stage = CaptureStage::seeking;
                } else if (!session.loading() && !session.has_track()) {
                    std::fprintf(stderr, "capture: %s\n", session.status().c_str());
                    capture_status = 1;
                    break;
                }
            } else if (capture_stage == CaptureStage::seeking) {
                if (session.transport().position() == capture_sample) {
                    capture_stage = CaptureStage::rendering;
                }
            } else {
                frame_ms.push_back(std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - frame_started)
                                       .count());
                if (++capture_frames >= capture->frames) {
                    const std::vector<std::uint8_t> pixels = capture_target.read_rgba();
                    if (const psyight::Result<void> written = psyight::write_png_rgba(
                            capture->out, psyight::kCaptureWidth, psyight::kCaptureHeight, pixels);
                        !written) {
                        std::fprintf(stderr, "%s\n", written.error().message.c_str());
                        capture_status = 1;
                        break;
                    }
                    std::vector<double> sorted = frame_ms;
                    std::sort(sorted.begin(), sorted.end());
                    double mean = 0.0;
                    for (const double ms : sorted) {
                        mean += ms;
                    }
                    mean /= static_cast<double>(sorted.size());
                    std::printf("capture: %s (theme %s, %.3f s, %d frames); frame time mean %.2f "
                                "ms, p95 %.2f ms, max %.2f ms\n",
                                psyight::path_to_utf8(capture->out).c_str(), capture->theme.c_str(),
                                capture->time_s, capture->frames, mean,
                                sorted[sorted.size() * 95 / 100], sorted.back());
                    break;
                }
            }
        }
        if (capture_status != 0) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            glfwDestroyWindow(window);
            glfwTerminate();
            return capture_status;
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
