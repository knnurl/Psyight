#include "gui/command_line.h"

#include <windows.h>

#include <shellapi.h>

#include <cstdio>

namespace psyight {

std::vector<std::filesystem::path> command_line_paths() {
    std::vector<std::filesystem::path> paths;
    int count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) {
        return paths;
    }
    for (int i = 1; i < count; ++i) {
        paths.emplace_back(arguments[i]);
    }
    LocalFree(arguments);
    return paths;
}

void attach_parent_console() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_CHAR &&
        GetFileType(out) != FILE_TYPE_UNKNOWN) {
        return; // Output already goes to a pipe or a file: left there.
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        return; // Not started from a terminal.
    }
    FILE *stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

} // namespace psyight
