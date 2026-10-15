// Command-line arguments as paths, without losing non-ASCII characters (on Windows the narrow
// argv is in the ANSI code page).
#pragma once

#include <filesystem>
#include <vector>

namespace psyight {

// Arguments after the program name.
std::vector<std::filesystem::path> command_line_paths();

} // namespace psyight
