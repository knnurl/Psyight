// Audio file decoding for file mode (SPEC section 5): WAV, FLAC or MP3, decoded completely into
// memory, resampled to the analysis rate and kept in stereo for playback. Analysis uses mono_mix.
#pragma once

#include "core/result.h"
#include "core/timebase.h"

#include <filesystem>
#include <vector>

namespace psyight {

inline constexpr int kAnalysisSampleRateHz = 48000;
inline constexpr int kPlaybackChannels = 2;

struct AudioBuffer {
    int sample_rate_hz = kAnalysisSampleRateHz;
    std::vector<float> samples; // Interleaved stereo; mono files are duplicated to both channels.

    SampleIndex frames() const {
        return static_cast<SampleIndex>(samples.size() / kPlaybackChannels);
    }
};

// Decodes and resamples with miniaudio's linear resampler and its anti-aliasing low-pass at the
// maximum order (8; the default is 1).
Result<AudioBuffer> decode_audio_file(const std::filesystem::path &path,
                                      int sample_rate_hz = kAnalysisSampleRateHz);

// (left + right) / 2 per frame (SPEC section 5: stereo summed to mono for analysis).
std::vector<float> mono_mix(const AudioBuffer &buffer);

} // namespace psyight
