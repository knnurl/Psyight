// Psyight entry point: opens the main window with a Dear ImGui docking workspace.

#include "core/paths.h"
#include "gui/analysis_view.h"
#include "gui/bass_roll_view.h"
#include "gui/capture.h"
#include "gui/command_line.h"
#include "gui/crash_guard.h"
#include "gui/file_dialog.h"
#include "gui/frame_pacing.h"
#include "gui/layout.h"
#include "gui/log_view.h"
#include "gui/preflight_view.h"
#include "gui/preview_view.h"
#include "gui/project_state.h"
#include "gui/rig_view.h"
#include "gui/scoring_view.h"
#include "gui/session.h"
#include "gui/settings_view.h"
#include "gui/setup_assistant.h"
#include "gui/status_bar.h"
#include "gui/theme.h"
#include "gui/tier.h"
#include "gui/transport_panel.h"
#include "io/png.h"
#include "io/recent_files.h"
#include "io/user_settings.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h> // DockBuilderGetNode, to detect a missing saved layout.
#include <implot.h>
#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h> // Brings in the system OpenGL header for glClear and glViewport.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h> // The window handle, as the file dialog's owner.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kInitialWidthPixels = 1600; // Size when restored from the initial maximised state.
constexpr int kInitialHeightPixels = 900;
constexpr int kMinimisedSleepMs = 10;
// OpenGL 3.2 or later lets the ImGui backend use vertex offsets, which large ImPlot draws (such as
// spectrograms) need with ImGui's 16-bit indices.
constexpr int kGlVersionMajor = 3;
constexpr int kGlVersionMinor = 3;
constexpr const char *kGlslVersion = "#version 330 core";
constexpr const char *kSettingsFile = "psyight.settings.json"; // In the working directory.

// The interface depth's name in the settings file.
const char *tier_id(psyight::Tier tier) {
    return tier == psyight::Tier::operate    ? "operate"
           : tier == psyight::Tier::engineer ? "engineer"
                                             : "tune";
}

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
void save_labels(psyight::Session &session) {
    if (const psyight::Result<void> saved = session.save_labels(); saved) {
        spdlog::info("Saved {}", psyight::path_to_utf8(session.labels_path().filename()));
    } else {
        spdlog::error("{}", saved.error().message);
    }
}

// What the status bar shows: the worst preflight failure (or the warnings) and short facts.
psyight::StatusInfo status_info(psyight::Session &session, const psyight::PreviewView &preview,
                                const psyight::PreflightView &preflight) {
    using psyight::ChipTone;
    psyight::StatusInfo info;
    for (const psyight::PreflightCheck &c : preflight.checks()) {
        if (c.level == psyight::CheckLevel::fail) {
            if (info.failures++ == 0) {
                info.fault = c.area + ": " + c.message;
            }
        } else if (c.level == psyight::CheckLevel::warn) {
            ++info.warnings;
        }
    }
    const auto text = [](const char *format, auto... values) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), format, values...);
        return std::string(buffer);
    };
    if (const std::shared_ptr<const psyight::LiveInput> live = session.live_input(); live) {
        const psyight::LiveState s = live->state();
        info.chips.push_back({s.tempo_known ? text("Live %.1f BPM, %s", 60.0 / s.period_s,
                                                   s.locked ? "locked" : "acquiring")
                                            : std::string("Live, listening"),
                              "Live input: the show follows what it hears.",
                              s.locked ? ChipTone::good : ChipTone::warn});
        info.chips.push_back({text("%.0f ms delay", live->analysis_delay_ms()),
                              "From the audio to the decisions about it.", ChipTone::plain});
    } else if (session.has_track() && session.track().track_analysis != nullptr) {
        const psyight::BeatTrackResult &b = session.track().track_analysis->beats;
        info.chips.push_back(
            {text("%.2f BPM", session.grid().bpm),
             text("The grid's tempo; the beat tracker's confidence %.2f.", b.tempo_confidence),
             ChipTone::plain});
    }
    if (psyight::Transport &t = session.transport(); t.is_open()) {
        const int rate = t.buffer().sample_rate_hz;
        info.chips.push_back(
            {text("%.0f kHz, %.1f ms", rate / 1000.0, 1e3 * t.device_period_frames() / rate),
             "Audio device: sample rate and period (the playback latency).", ChipTone::dim});
    }
    if (session.has_track()) {
        info.chips.push_back({std::string("Detections: ") + session.detection_name(),
                              "Where the bass roll's and scoring's notes come from.",
                              ChipTone::dim});
    }
    if (preview.test_pattern().kind != psyight::TestPatternKind::off) {
        info.chips.push_back({std::string("Test pattern: ") +
                                  psyight::test_pattern_name(preview.test_pattern().kind),
                              "Preflight test pattern on.",
                              preview.test_pattern().send ? ChipTone::bad : ChipTone::warn});
    }
    info.chips.push_back(
        preview.sending() ? psyight::StatusChip{text("Sending, %.0f fps", preview.measured_fps()),
                                                "The show goes to the network.", ChipTone::accent}
                          : psyight::StatusChip{"Preview only",
                                                "Nothing goes to the lights (Preflight or "
                                                "Preview on the top bar).",
                                                ChipTone::dim});
    if (const auto &nodes = preflight.nodes(); nodes.has_value()) {
        info.chips.push_back({text("%zu nodes", nodes->size()),
                              "Art-Net nodes that answered the last discovery (Preflight).",
                              nodes->empty() ? ChipTone::warn : ChipTone::good});
    } else {
        info.chips.push_back({"Nodes not checked",
                              "Preflight: Discover Art-Net nodes, on the show network.",
                              ChipTone::dim});
    }
    if (preview.recording()) {
        info.chips.push_back({"REC", "Recording the show (top bar).", ChipTone::bad});
    }
    return info;
}

// A show panel has the keyboard focus: undo and redo act on the project (scenes, rig,
// modulation, settings), not the labels.
bool show_panel_focused() {
    const ImGuiWindow *window = ImGui::GetCurrentContext()->NavWindow;
    if (window == nullptr || window->RootWindow == nullptr) {
        return false;
    }
    for (const char *name : {"Show", "Rig", "Preview", "Stage", "Perform", "Settings"}) {
        if (std::strcmp(window->RootWindow->Name, name) == 0) {
            return true;
        }
    }
    return false;
}

void handle_shortcuts(psyight::Session &session, psyight::ProjectState &project) {
    constexpr ImGuiInputFlags kGlobal = ImGuiInputFlags_RouteGlobal;
    const bool show = show_panel_focused();
    if (session.has_track() && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, kGlobal)) {
        save_labels(session);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, kGlobal)) {
        if (show) {
            project.undo();
        } else if (session.has_track()) {
            session.labels().undo();
        }
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, kGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, kGlobal)) {
        if (show) {
            project.redo();
        } else if (session.has_track()) {
            session.labels().redo();
        }
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

    // Fonts, colours and sizes (gui/theme.h), scaled to the monitor's DPI (1.0 for captures).
    float dpi_scale = 1.0f;
    if (!capture) {
        glfwGetWindowContentScale(window, &dpi_scale, nullptr);
    }
    psyight::init_theme(dpi_scale);
    psyight::apply_psyight_theme(capture && capture->theme == "show" ? psyight::ThemeMode::show
                                                                     : psyight::ThemeMode::desk);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(kGlslVersion);

    { // App objects own GL textures, so they must go before the GL context does.
        psyight::LogView log_view; // First, so it keeps every message after start-up.
        std::optional<psyight::CrashGuard> crash_guard; // Log file, dumps, unclean-exit marker.
        if (!capture) {
            crash_guard.emplace();
        } else {
            psyight::install_crash_handler("logs"); // A crash still leaves its stack.
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
        psyight::SettingsView settings_view(project);
        psyight::SetupAssistant assistant(project, rig_view, preview_view, preflight_view);
        if (!capture && !std::filesystem::exists(project.path())) {
            assistant.open(); // No project yet: a first setup.
        }
        psyight::RecentFiles recent;
        // The operator's preferences: loaded at start, saved when they change (not in capture
        // mode, which always renders the defaults). prefs holds the ones main and the settings
        // window own (show mode); the views keep theirs.
        psyight::UserSettings settings;
        if (!capture) {
            settings = psyight::load_user_settings(kSettingsFile);
            psyight::apply_psyight_theme(settings.theme == "show" ? psyight::ThemeMode::show
                                                                  : psyight::ThemeMode::desk);
            for (const psyight::Tier t :
                 {psyight::Tier::operate, psyight::Tier::tune, psyight::Tier::engineer}) {
                if (settings.interface_depth == tier_id(t)) {
                    psyight::set_current_tier(t);
                }
            }
            transport_panel.apply_settings(settings);
            analysis_view.apply_settings(settings);
            bass_roll_view.apply_settings(settings);
            preview_view.apply_settings(settings);
        }
        psyight::UserSettings prefs = settings;
        // Show mode (React or Show on the top bar): the Show theme and the editing panels hidden,
        // as the settings say; leaving it restores the theme from before.
        bool show_mode = false;
        psyight::ThemeMode theme_before_show = psyight::theme_mode();
        std::filesystem::path recent_path; // The track last added to the recent list.
        bool show_settings = false;
        bool perform = false; // The Perform view (F11) over the workspace.
        bool open_requested = false;
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
        bool show_style_editor = false;
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
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Open track...", "Ctrl+O")) {
                        open_requested = true;
                    }
                    if (ImGui::BeginMenu("Open recent", !recent.items().empty())) {
                        std::optional<std::filesystem::path> chosen;
                        for (const std::filesystem::path &track : recent.items()) {
                            ImGui::PushID(psyight::path_to_utf8(track).c_str());
                            if (ImGui::MenuItem(psyight::path_to_utf8(track.filename()).c_str())) {
                                chosen = track;
                            }
                            ImGui::SetItemTooltip("%s", psyight::path_to_utf8(track).c_str());
                            ImGui::PopID();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Clear the list")) {
                            recent.clear();
                        }
                        ImGui::EndMenu();
                        if (chosen && std::filesystem::exists(*chosen)) {
                            session.open_file(*chosen);
                        } else if (chosen) {
                            spdlog::warn("{} is gone; removed from the recent list",
                                         psyight::path_to_utf8(*chosen));
                            recent.remove(*chosen);
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Save labels", "Ctrl+S", false, session.has_track())) {
                        save_labels(session);
                    }
                    ImGui::Separator();
                    ImGui::MenuItem("Settings...", nullptr, &show_settings);
                    if (ImGui::MenuItem("Setup assistant...")) {
                        assistant.open();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit", "Alt+F4")) {
                        glfwSetWindowShouldClose(window, GLFW_TRUE);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit")) {
                    // Ctrl+Z and Ctrl+Y act on whichever has the focus: a show panel (Show,
                    // Rig, Preview) or the labels (everywhere else).
                    psyight::LabelEditor &labels = session.labels();
                    if (ImGui::MenuItem("Undo label edit", "Ctrl+Z", false,
                                        session.has_track() && labels.can_undo())) {
                        labels.undo();
                    }
                    if (ImGui::MenuItem("Redo label edit", "Ctrl+Y", false,
                                        session.has_track() && labels.can_redo())) {
                        labels.redo();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Undo show edit", "Ctrl+Z", false, project.can_undo())) {
                        project.undo();
                    }
                    if (ImGui::MenuItem("Redo show edit", "Ctrl+Y", false, project.can_redo())) {
                        project.redo();
                    }
                    ImGui::SetItemTooltip("Scenes, layers, modulation, macros, palettes, the "
                                          "rig and settings. The keys act on show edits while "
                                          "the Show, Rig or Preview panel has the focus.");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Reset layout", nullptr, &reset_layout);
                    ImGui::MenuItem("Rig preview", nullptr, &preview_view.preview_open());
                    ImGui::MenuItem("Perform view", "F11", &perform);
                    if (ImGui::BeginMenu("Theme")) {
                        const psyight::ThemeMode mode = psyight::theme_mode();
                        if (ImGui::MenuItem("Desk", nullptr, mode == psyight::ThemeMode::desk)) {
                            psyight::apply_psyight_theme(psyight::ThemeMode::desk);
                        }
                        if (ImGui::MenuItem("Show", nullptr, mode == psyight::ThemeMode::show)) {
                            psyight::apply_psyight_theme(psyight::ThemeMode::show);
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::MenuItem("Style editor", "F12", &show_style_editor);
                    ImGui::Separator();
                    ImGui::MenuItem("ImGui demo", nullptr, &show_imgui_demo);
                    ImGui::MenuItem("ImPlot demo", nullptr, &show_implot_demo);
                    ImGui::EndMenu();
                }
                preview_view.draw_top_bar();
                ImGui::EndMainMenuBar();
            }
            // The status bar (chips and the fault banner), before the dock space so it makes room.
            preflight_view.update();
            if (psyight::draw_status_bar(status_info(session, preview_view, preflight_view))) {
                ImGui::SetWindowFocus("Preflight");
            }
            // Bump the version when panels are added, so saved layouts get the new default once.
            const ImGuiID dockspace_id = ImGui::GetID("Main dock space v7");
            if (reset_layout || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
                psyight::apply_default_layout(dockspace_id); // First run, or View > Reset layout.
                reset_layout = false;
                focus_preview = true;
            }
            ImGui::DockSpaceOverViewport(dockspace_id);
            session.update();
            if (!capture && session.has_track() && session.track().path != recent_path) {
                recent_path = session.track().path; // Loaded (from the menu, a drop or the
                recent.add(recent_path);            // command line): first in the recent list.
            }
            if (preview_view.performing() != show_mode) {
                show_mode = preview_view.performing();
                if (show_mode) {
                    theme_before_show = psyight::theme_mode();
                }
                if (prefs.show_mode_theme) {
                    psyight::apply_psyight_theme(show_mode ? psyight::ThemeMode::show
                                                           : theme_before_show);
                }
            }
            transport_panel.draw();
            if (!(show_mode && prefs.show_mode_hides_editing)) {
                analysis_view.draw();
                bass_roll_view.draw_roll();
                bass_roll_view.draw_inspector();
            }
            scoring_view.draw();
            // Before the log: the Show tab comes first in its group.
            preview_view.draw(show_mode && prefs.show_mode_hides_editing);
            // A strip hovered in one of the preview and the Rig tab is highlighted in the other.
            preview_view.set_highlighted_strip(rig_view.hovered_strip());
            rig_view.set_highlighted_strip(preview_view.hovered_strip());
            log_view.draw();
            rig_view.draw();
            preflight_view.draw();
            if (focus_preview) { // Show the show controls' tab, not the rig's, after a new layout.
                ImGui::SetWindowFocus("Show");
                focus_preview = false;
            }
            handle_shortcuts(session, project);
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
                open_requested = true;
            }
            settings_view.draw(&show_settings, reset_layout, prefs);
            assistant.draw();
            if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
                perform = !perform;
            }
            if (perform) {
                preview_view.draw_perform(&perform);
            }
            project.autosave();
            if (!capture && !ImGui::IsAnyItemActive()) { // Not on every step of a drag.
                psyight::UserSettings now = prefs;
                // In show mode the theme to keep is the one show mode will restore.
                const psyight::ThemeMode kept_theme =
                    show_mode && prefs.show_mode_theme ? theme_before_show : psyight::theme_mode();
                now.theme = kept_theme == psyight::ThemeMode::show ? "show" : "desk";
                now.interface_depth = tier_id(psyight::current_tier());
                transport_panel.store_settings(now);
                analysis_view.store_settings(now);
                bass_roll_view.store_settings(now);
                preview_view.store_settings(now);
                if (now != settings) {
                    settings = now;
                    psyight::save_user_settings(kSettingsFile, settings);
                }
            }
            session.end_frame();
            if (show_imgui_demo) {
                ImGui::ShowDemoWindow(&show_imgui_demo);
            }
            if (show_implot_demo) {
                ImPlot::ShowDemoWindow(&show_implot_demo);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
                show_style_editor = !show_style_editor; // Dev only (RESTYLE Phase 1).
            }
            psyight::draw_style_editor(&show_style_editor);

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
                if (open_requested) { // After the frame is shown, not halfway through it.
                    open_requested = false;
                    if (const auto chosen =
                            psyight::open_audio_file_dialog(glfwGetWin32Window(window));
                        chosen) {
                        session.open_file(*chosen);
                    }
                }
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
