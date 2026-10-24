// Command-line arguments as paths, without losing non-ASCII characters (on Windows the narrow
// argv is in the ANSI code page).
#pragma once

#include <filesystem>
#include <vector>

namespace psyight {

// Arguments after the program name.
std::vector<std::filesystem::path> command_line_paths();

// The app is a windowed program (no console of its own); started from a terminal, its standard
// output and error go to that terminal (capture mode's report, errors). Started from Explorer, a
// shortcut or the zip, nothing changes: the Log tab shows the log.
void attach_parent_console();

} // namespace psyight
