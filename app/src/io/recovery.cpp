#include "io/recovery.h"

#include <chrono>

namespace psyight {

std::filesystem::path recovery_path(const std::filesystem::path &file) {
    std::filesystem::path copy = file;
    copy += ".autosave";
    return copy;
}

bool recovery_newer(const std::filesystem::path &file) {
    std::error_code error;
    const auto copy_time = std::filesystem::last_write_time(recovery_path(file), error);
    if (error) {
        return false; // No copy.
    }
    const auto file_time = std::filesystem::last_write_time(file, error);
    return error || copy_time > file_time;
}

std::string recovery_age(const std::filesystem::path &file) {
    std::error_code error;
    const auto copy_time = std::filesystem::last_write_time(recovery_path(file), error);
    if (error) {
        return {};
    }
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(
                             std::filesystem::file_time_type::clock::now() - copy_time)
                             .count();
    if (minutes < 1) {
        return "less than a minute ago";
    }
    if (minutes < 60) {
        return std::to_string(minutes) + (minutes == 1 ? " minute ago" : " minutes ago");
    }
    const auto hours = minutes / 60;
    if (hours < 24) {
        return std::to_string(hours) + (hours == 1 ? " hour ago" : " hours ago");
    }
    const auto days = hours / 24;
    return days == 1 ? "yesterday" : std::to_string(days) + " days ago";
}

void remove_recovery(const std::filesystem::path &file) {
    std::error_code error;
    std::filesystem::remove(recovery_path(file), error);
}

} // namespace psyight
