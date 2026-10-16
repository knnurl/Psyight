// Offline Demucs stems (SPEC 6.2 and 6.8, M2): written by tools/python/separate.py as
// "<audio folder>/stems/htdemucs/<audio name without extension>/<stem>.wav".
#pragma once

#include <filesystem>

namespace psyight {

struct StemPaths {
    std::filesystem::path drums;
    std::filesystem::path bass;
    std::filesystem::path other;
    std::filesystem::path vocals;
};

StemPaths stem_paths_for(const std::filesystem::path &audio_path);

// True when the drums and bass stems exist (the ones the analysis uses).
bool analysis_stems_exist(const StemPaths &stems);

} // namespace psyight
