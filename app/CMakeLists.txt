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
target_compile_definitions(psyight_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)

# miniaudio is a single-header library: its implementation is compiled once here, as third-party
# code outside the warning policy.
add_library(psyight_miniaudio STATIC src/audio/miniaudio_impl.c)
target_include_directories(psyight_miniaudio SYSTEM PUBLIC "${MINIAUDIO_INCLUDE_DIR}")
set_target_properties(psyight_miniaudio PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# Platform-independent app code, shared by the app and the tests.
add_library(psyight_lib STATIC src/io/labels.cpp src/analysis/scoring.cpp)
target_include_directories(psyight_lib PUBLIC src)
target_link_libraries(psyight_lib PRIVATE psyight_options nlohmann_json::nlohmann_json)

add_executable(psyight src/main.cpp src/gui/frame_pacing_win32.cpp)
target_include_directories(psyight PRIVATE src)
target_link_libraries(psyight PRIVATE psyight_options imgui::imgui implot::implot glfw
                                      OpenGL::GL spdlog::spdlog dwmapi)

enable_testing()
add_executable(psyight_tests tests/test_main.cpp tests/dependency_smoke_test.cpp
                             tests/labels_test.cpp tests/scoring_test.cpp)
target_link_libraries(psyight_tests PRIVATE psyight_options psyight_lib psyight_miniaudio
                                            doctest::doctest pffft::pffft)
add_test(NAME psyight_tests COMMAND psyight_tests)
