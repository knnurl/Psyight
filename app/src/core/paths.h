// Path text for messages and logs. path::string() converts to the Windows ANSI code page and can
// throw for characters it cannot represent; UTF-8 is lossless.
#pragma once

#include <filesystem>
#include <string>

namespace psyight {

inline std::string path_to_utf8(const std::filesystem::path &path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

} // namespace psyight
