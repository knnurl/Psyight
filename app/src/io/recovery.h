// Crash recovery copies. The app never writes a project or labels file without a save; while one
// has unsaved changes, a copy goes to "<file>.autosave" beside it (at most once a minute, and when
// the app closes without the save prompt). When the file is opened again, a copy newer than it is
// offered for restoring; saving, discarding or restoring removes the copy.
#pragma once

#include <filesystem>
#include <string>

namespace psyight {

std::filesystem::path recovery_path(const std::filesystem::path &file);

// The recovery copy exists and is newer than the file (or the file is missing).
bool recovery_newer(const std::filesystem::path &file);

// How long ago the recovery copy was written, for a prompt: "2 minutes ago", "yesterday"...
// Empty when there is none.
std::string recovery_age(const std::filesystem::path &file);

void remove_recovery(const std::filesystem::path &file);

} // namespace psyight
