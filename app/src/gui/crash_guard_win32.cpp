#include "gui/crash_guard.h"

#include "core/paths.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <windows.h>

#include <dbghelp.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>

namespace psyight {

namespace {

constexpr size_t kLogBytes = 5 * 1024 * 1024;
constexpr size_t kLogFiles = 3;

std::filesystem::path g_dump_dir; // Set once before the filter is installed.

// The crashing thread's stack as text (function, file and line from the .pdb files beside the
// exe), to stderr and a file: readable without a debugger.
void write_stack(EXCEPTION_POINTERS *exception, const std::filesystem::path &path) {
    FILE *out = nullptr;
    _wfopen_s(&out, path.c_str(), L"w");
    const auto line = [&](const char *text) {
        std::fputs(text, stderr);
        if (out != nullptr) {
            std::fputs(text, out);
        }
    };
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "Psyight crashed: exception 0x%08lX at %p\n",
                  exception->ExceptionRecord->ExceptionCode,
                  exception->ExceptionRecord->ExceptionAddress);
    line(buffer);
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char symbol_storage[sizeof(SYMBOL_INFO) + 256];
    for (int depth = 0; depth < 48; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbol_storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        const bool named = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol);
        IMAGEHLP_LINE64 source{};
        source.SizeOfStruct = sizeof(source);
        DWORD column = 0;
        const bool lined = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &column, &source);
        std::snprintf(buffer, sizeof(buffer), "  %2d %s (%s:%lu)\n", depth,
                      named ? symbol->Name : "?", lined ? source.FileName : "?",
                      lined ? source.LineNumber : 0ul);
        line(buffer);
    }
    SymCleanup(process);
    if (out != nullptr) {
        std::fclose(out);
    }
}

LONG WINAPI write_dump(EXCEPTION_POINTERS *exception) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t stamp[32];
    std::wcsftime(stamp, 32, L"crash-%Y%m%d-%H%M%S", &local);
    std::error_code ignored;
    std::filesystem::create_directories(g_dump_dir, ignored);
    write_stack(exception, g_dump_dir / (std::wstring(stamp) + L".txt"));
    const std::filesystem::path path = g_dump_dir / (std::wstring(stamp) + L".dmp");
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

void install_crash_handler(const std::filesystem::path &dump_dir) {
    g_dump_dir = dump_dir;
    SetUnhandledExceptionFilter(&write_dump);
}

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
    install_crash_handler(logs);

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
