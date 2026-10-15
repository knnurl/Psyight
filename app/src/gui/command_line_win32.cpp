#include "gui/command_line.h"

#include <windows.h>

#include <shellapi.h>

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

} // namespace psyight
