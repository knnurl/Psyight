// Whole-file text reads and crash-safe writes, shared by the labels and project files.
#pragma once

#include "core/result.h"

#include <filesystem>
#include <string>

namespace psyight {

Result<std::string> read_text_file(const std::filesystem::path &path);

// Writes to "<path>.tmp", then renames over the target, so a failed write never truncates an
// existing file.
Result<void> write_text_file_atomic(const std::filesystem::path &path, const std::string &text);

} // namespace psyight
