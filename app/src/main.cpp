// Psyight entry point: opens the main window with a Dear ImGui docking workspace.

#include "gui/frame_pacing.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h> // Brings in the system OpenGL header for glClear and glViewport.

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

} // namespace

int main() {
    glfwSetErrorCallback(log_glfw_error);
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kGlVersionMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kGlVersionMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE); // The scaled size can exceed small screens.
    GLFWwindow *window =
        glfwCreateWindow(kInitialWidthPixels, kInitialHeightPixels, "Psyight", nullptr, nullptr);
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
    ImGui::StyleColorsDark();

    // Scale text and widget sizes to the monitor's DPI setting.
    float dpi_scale = 1.0f;
    glfwGetWindowContentScale(window, &dpi_scale, nullptr);
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

    bool show_imgui_demo = false;
    bool show_implot_demo = false;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(kMinimisedSleepMs); // Swap does not throttle a minimised window.
            continue;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("ImGui demo", nullptr, &show_imgui_demo);
                ImGui::MenuItem("ImPlot demo", nullptr, &show_implot_demo);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        ImGui::DockSpaceOverViewport();
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
        glViewport(0, 0, framebuffer_width_pixels, framebuffer_height_pixels);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        psyight::wait_for_display_refresh();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
