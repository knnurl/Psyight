#include "gui/crash_guard.h"

#include "core/paths.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <windows.h>

#include <dbghelp.h>

#include <ctime>
#include <fstream>
#include <string>

namespace psyight {

namespace {

constexpr size_t kLogBytes = 5 * 1024 * 1024;
constexpr size_t kLogFiles = 3;

std::filesystem::path g_dump_dir; // Set once before the filter is installed.

LONG WINAPI write_dump(EXCEPTION_POINTERS *exception) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t name[64];
    std::wcsftime(name, 64, L"crash-%Y%m%d-%H%M%S.dmp", &local);
    const std::filesystem::path path = g_dump_dir / name;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo |
                                                     MiniDumpWithIndirectlyReferencedMemory),
                          &info, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH; // Let Windows end the process as usual.
}

} // namespace

CrashGuard::CrashGuard(std::filesystem::path working_directory)
    : marker_(working_directory / "psyight.running") {
    const std::filesystem::path logs = working_directory / "logs";
    std::error_code ignored;
    std::filesystem::create_directories(logs, ignored);
    try {
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            path_to_utf8(logs / "psyight.log"), kLogBytes, kLogFiles);
        spdlog::default_logger()->sinks().push_back(sink);
        spdlog::flush_on(spdlog::level::warn);
    } catch (const spdlog::spdlog_ex &e) {
        spdlog::warn("No log file: {}", e.what());
    }
    g_dump_dir = logs;
    SetUnhandledExceptionFilter(&write_dump);

    last_crashed_ = std::filesystem::exists(marker_);
    if (last_crashed_) {
        spdlog::warn(
            "The last session did not end cleanly (a crash or a power cut). Labels and the "
            "project autosave; a crash dump, if any, is in logs/.");
    }
    std::ofstream(marker_) << "Psyight is running. This file is removed on a clean exit.\n";
}

CrashGuard::~CrashGuard() {
    std::error_code ignored;
    std::filesystem::remove(marker_, ignored);
    spdlog::default_logger()->flush();
}

} // namespace psyight
