cmake_minimum_required(VERSION 3.25)

# vcpkg builds the dependencies inside project(). Point it at the developer shell's Visual Studio
# instance so dependencies and app use the same compiler: MSVC cannot link static libraries built
# by a newer toolset. vcpkg rejects the trailing backslash that VSINSTALLDIR carries.
if(DEFINED ENV{VSINSTALLDIR} AND NOT DEFINED ENV{VCPKG_VISUAL_STUDIO_PATH})
    string(REGEX REPLACE "\\\\$" "" vs_install_dir "$ENV{VSINSTALLDIR}")
    set(ENV{VCPKG_VISUAL_STUDIO_PATH} "${vs_install_dir}")
endif()

project(psyight VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Psyight builds for x64 only. Configure from an x64 developer shell.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
# One folder per configuration, so the app runs from build/windows-msvc/release/psyight.exe.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>")

find_package(doctest CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(pffft CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)

# Compile settings for all first-party C++ targets. /fp:precise is the MSVC default, stated
# explicitly because analysis output must be bit-identical between runs (no fast-math).
add_library(psyight_options INTERFACE)
target_compile_options(psyight_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus
                                                 /fp:precise)
target_compile_definitions(psyight_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN
                                                     PSYIGHT_VERSION="${PROJECT_VERSION}")

# miniaudio is a single-header library: its implementation is compiled once here, as third-party
# code outside the warning policy.
add_library(psyight_miniaudio STATIC src/audio/miniaudio_impl.c)
target_include_directories(psyight_miniaudio SYSTEM PUBLIC "${MINIAUDIO_INCLUDE_DIR}")
set_target_properties(psyight_miniaudio PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# Signalsmith Stretch 1.4.0 with Signalsmith Linear 0.6.4 (the version it pins), MIT, header-only,
# vendored: see third_party/README.md. SYSTEM keeps the project warning policy off their headers.
add_library(signalsmith_stretch INTERFACE)
target_include_directories(signalsmith_stretch SYSTEM INTERFACE
                           third_party/signalsmith-stretch-1.4.0/include
                           third_party/signalsmith-linear-0.6.4/include)

# Platform-independent app code, shared by the app and the tests.
add_library(psyight_lib STATIC src/core/params.cpp src/core/pitch.cpp src/io/labels.cpp
                               src/io/label_editor.cpp src/io/note_pattern.cpp
                               src/analysis/scoring.cpp
                               src/audio/audio_file.cpp src/audio/transport.cpp
                               src/audio/transport_render.cpp src/audio/stretch_renderer.cpp
                               src/analysis/stft.cpp src/analysis/filterbank.cpp
                               src/analysis/front_end.cpp src/analysis/bass_tracker.cpp
                               src/analysis/beat_tracker.cpp src/analysis/sections.cpp
                               src/analysis/track_analysis.cpp src/io/stems.cpp
                               src/analysis/align.cpp src/analysis/residual.cpp
                               src/analysis/bass_aggregates.cpp src/engine/rig.cpp
                               src/engine/colour.cpp src/engine/inputs.cpp
                               src/engine/scene.cpp src/engine/limiters.cpp
                               src/engine/automation.cpp src/engine/modulation.cpp
                               src/engine/effect_spec.cpp
                               src/io/text_file.cpp src/io/session_record.cpp
                               src/output/recorder.cpp src/analysis/student_data.cpp
                               src/io/project.cpp
                               src/output/patch.cpp src/output/packets.cpp
                               src/output/sender.cpp src/output/virtual_node.cpp
                               src/output/show.cpp src/output/udp_socket_win32.cpp
                               src/output/precise_sleep_win32.cpp src/output/art_poll.cpp
                               src/output/discovery.cpp src/output/preflight.cpp)
target_include_directories(psyight_lib PUBLIC src)
target_link_libraries(psyight_lib PRIVATE psyight_options nlohmann_json::nlohmann_json
                                          psyight_miniaudio pffft::pffft signalsmith_stretch
                                          ws2_32 iphlpapi)

add_executable(psyight src/main.cpp src/gui/frame_pacing_win32.cpp src/gui/command_line_win32.cpp
                       src/gui/session.cpp src/gui/layout.cpp src/gui/transport_panel.cpp
                       src/gui/analysis_view.cpp src/gui/bass_roll_view.cpp src/gui/time_plot.cpp
                       src/gui/spectrogram_textures.cpp
                       src/gui/spectrogram_image.cpp src/gui/preview_view.cpp
                       src/gui/project_state.cpp src/gui/rig_view.cpp
                       src/gui/preflight_view.cpp src/gui/tier.cpp)
target_include_directories(psyight PRIVATE src)
target_link_libraries(psyight PRIVATE psyight_options psyight_lib imgui::imgui implot::implot
                                      glfw OpenGL::GL spdlog::spdlog dwmapi shell32)

# Scores the bass tracker against a labels file (local reference tracks; never run in CI).
add_executable(psyight_eval src/eval_main.cpp)
target_link_libraries(psyight_eval PRIVATE psyight_options psyight_lib)

# Reports the file-mode analysis of unlabelled tracks: tempo, grid, kicks, bass statistics.
add_executable(psyight_analyse src/analyse_main.cpp)
target_link_libraries(psyight_analyse PRIVATE psyight_options psyight_lib)

# Renders whole tracks as shows offline and reports flashes, current, contrast and render cost.
add_executable(psyight_show_check src/show_check_main.cpp)
target_link_libraries(psyight_show_check PRIVATE psyight_options psyight_lib)

# Exports one track's M4 training data (student features, teacher targets) or its fingerprint.
add_executable(psyight_teacher src/teacher_main.cpp)
target_link_libraries(psyight_teacher PRIVATE psyight_options psyight_lib nlohmann_json::nlohmann_json)

# Drafts a labels file from the analysis, for a person to check (the region starts incomplete).
add_executable(psyight_label_draft src/label_draft_main.cpp)
target_link_libraries(psyight_label_draft PRIVATE psyight_options psyight_lib)

# Replays a recorded session and checks it reproduces byte for byte.
add_executable(psyight_replay src/replay_main.cpp)
target_link_libraries(psyight_replay PRIVATE psyight_options psyight_lib)

enable_testing()
add_executable(psyight_tests tests/test_main.cpp tests/dependency_smoke_test.cpp tests/core_test.cpp
                             tests/audio_test.cpp tests/front_end_test.cpp
                             tests/labels_test.cpp tests/scoring_test.cpp
                             tests/synth_generator.cpp tests/synth_generator_test.cpp
                             tests/gui_logic_test.cpp src/gui/spectrogram_image.cpp
                             tests/label_editor_test.cpp tests/note_pattern_test.cpp
                             tests/synthetic_set.cpp tests/bass_tracker_test.cpp
                             tests/beat_tracker_test.cpp tests/sections_test.cpp
                             tests/align_test.cpp tests/residual_test.cpp
                             tests/engine_test.cpp tests/limiters_test.cpp
                             tests/output_test.cpp tests/show_test.cpp
                             tests/automation_test.cpp tests/project_test.cpp
                             tests/preflight_test.cpp tests/modulation_test.cpp
                             tests/recorder_test.cpp tests/student_data_test.cpp
                             tests/node_firmware_test.cpp tests/playhead_clock_test.cpp
                             ../firmware/node-f767/src/core/protocol.c
                             ../firmware/node-f767/src/core/node_core.c
                             ../firmware/node-f767/src/core/node_config.c)
target_link_libraries(psyight_tests PRIVATE psyight_options psyight_lib psyight_miniaudio
                                            doctest::doctest pffft::pffft)
# The node firmware's portable core, tested against the app's packets (firmware/node-f767).
target_include_directories(psyight_tests PRIVATE ../firmware/node-f767/src/core)
# sscanf and strcpy are plain C on the node; MSVC would flag them as deprecated (C4996).
set_source_files_properties(../firmware/node-f767/src/core/node_config.c
                            PROPERTIES COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
add_test(NAME psyight_tests COMMAND psyight_tests)
