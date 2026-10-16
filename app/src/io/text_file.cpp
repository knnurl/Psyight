#include "io/text_file.h"

#include "core/paths.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace psyight {

Result<std::string> read_text_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Error{"cannot open " + path_to_utf8(path)};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

Result<void> write_text_file_atomic(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::path temp_path = path;
    temp_path += ".tmp";
    std::error_code error;
    {
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return Error{"cannot write " + path_to_utf8(temp_path)};
        }
        file << text;
        file.close();
        if (!file) {
            std::filesystem::remove(temp_path, error);
            return Error{"failed writing " + path_to_utf8(temp_path)};
        }
    }
    std::filesystem::rename(temp_path, path, error);
    if (error) {
        const std::string reason = error.message();
        std::filesystem::remove(temp_path, error);
        return Error{"cannot replace " + path_to_utf8(path) + ": " + reason};
    }
    return {};
}

} // namespace psyight
