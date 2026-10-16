#include "io/stems.h"

namespace psyight {

StemPaths stem_paths_for(const std::filesystem::path &audio_path) {
    const std::filesystem::path dir =
        audio_path.parent_path() / "stems" / "htdemucs" / audio_path.stem();
    return {dir / "drums.wav", dir / "bass.wav", dir / "other.wav", dir / "vocals.wav"};
}

bool analysis_stems_exist(const StemPaths &stems) {
    std::error_code error;
    return std::filesystem::exists(stems.drums, error) &&
           std::filesystem::exists(stems.bass, error);
}

} // namespace psyight
