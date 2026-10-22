#include "io/user_settings.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <type_traits>

namespace psyight {

namespace {

template <typename T> void read(const nlohmann::json &root, const char *key, T &value) {
    const auto found = root.find(key);
    if (found == root.end()) {
        return;
    }
    if constexpr (std::is_same_v<T, bool>) {
        if (found->is_boolean()) {
            value = found->get<bool>();
        }
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (found->is_string()) {
            value = found->get<std::string>();
        }
    } else if (found->is_number()) {
        value = found->get<T>();
    }
}

// An array of N positive, finite numbers, or nothing (the default stays).
template <size_t N>
void read_shares(const nlohmann::json &root, const char *key, std::array<float, N> &value) {
    const auto found = root.find(key);
    if (found == root.end() || !found->is_array() || found->size() != N) {
        return;
    }
    std::array<float, N> shares{};
    for (size_t i = 0; i < N; ++i) {
        const nlohmann::json &item = (*found)[i];
        if (!item.is_number()) {
            return;
        }
        shares[i] = item.get<float>();
        if (!std::isfinite(shares[i]) || shares[i] <= 0.0f) {
            return;
        }
    }
    value = shares;
}

} // namespace

UserSettings load_user_settings(const std::filesystem::path &file) {
    UserSettings settings;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return settings;
    }
    const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
    if (!root.is_object()) {
        return settings;
    }
    read(root, "theme", settings.theme);
    read(root, "interface_depth", settings.interface_depth);
    read(root, "spectrogram_colours", settings.spectrogram_colours);
    read(root, "spectrogram_min_db", settings.spectrogram_min_db);
    read(root, "spectrogram_max_db", settings.spectrogram_max_db);
    read_shares(root, "analysis_plot_shares", settings.analysis_plot_shares);
    read(root, "volume_db", settings.volume_db);
    read(root, "show_detections", settings.show_detections);
    read(root, "show_pitch", settings.show_pitch);
    read(root, "snap", settings.snap);
    read(root, "preview_open", settings.preview_open);
    read(root, "show_mode_theme", settings.show_mode_theme);
    read(root, "show_mode_hides_editing", settings.show_mode_hides_editing);
    return settings;
}

void save_user_settings(const std::filesystem::path &file, const UserSettings &settings) {
    const nlohmann::json root = {
        {"theme", settings.theme},
        {"interface_depth", settings.interface_depth},
        {"spectrogram_colours", settings.spectrogram_colours},
        {"spectrogram_min_db", settings.spectrogram_min_db},
        {"spectrogram_max_db", settings.spectrogram_max_db},
        {"analysis_plot_shares", settings.analysis_plot_shares},
        {"volume_db", settings.volume_db},
        {"show_detections", settings.show_detections},
        {"show_pitch", settings.show_pitch},
        {"snap", settings.snap},
        {"preview_open", settings.preview_open},
        {"show_mode_theme", settings.show_mode_theme},
        {"show_mode_hides_editing", settings.show_mode_hides_editing},
    };
    std::ofstream(file, std::ios::binary) << root.dump(2) << "\n";
}

} // namespace psyight
