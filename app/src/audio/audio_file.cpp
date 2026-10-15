#include "audio/audio_file.h"

#include "core/paths.h"

#include <miniaudio.h>

#include <string>

namespace psyight {

namespace {

constexpr ma_uint64 kDecodeChunkFrames = 65536;

} // namespace

Result<AudioBuffer> decode_audio_file(const std::filesystem::path &path, int sample_rate_hz) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kPlaybackChannels,
                                                      static_cast<ma_uint32>(sample_rate_hz));
    config.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
    ma_decoder decoder;
#ifdef _WIN32
    const ma_result opened = ma_decoder_init_file_w(path.c_str(), &config, &decoder);
#else
    const ma_result opened = ma_decoder_init_file(path.c_str(), &config, &decoder);
#endif
    if (opened != MA_SUCCESS) {
        return Error{"cannot decode " + path_to_utf8(path) + ": " + ma_result_description(opened)};
    }

    // Read in chunks until the end: MP3 lengths are only estimates before a full scan.
    AudioBuffer buffer;
    buffer.sample_rate_hz = sample_rate_hz;
    ma_result result = MA_SUCCESS;
    while (result == MA_SUCCESS) {
        const size_t old_size = buffer.samples.size();
        buffer.samples.resize(old_size + kDecodeChunkFrames * kPlaybackChannels);
        ma_uint64 frames_read = 0;
        result = ma_decoder_read_pcm_frames(&decoder, buffer.samples.data() + old_size,
                                            kDecodeChunkFrames, &frames_read);
        buffer.samples.resize(old_size + static_cast<size_t>(frames_read) * kPlaybackChannels);
        if (frames_read == 0) {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (result != MA_SUCCESS && result != MA_AT_END) {
        return Error{"error while decoding " + path_to_utf8(path) + ": " +
                     ma_result_description(result)};
    }
    if (buffer.samples.empty()) {
        return Error{path_to_utf8(path) + " contains no audio"};
    }
    buffer.samples.shrink_to_fit();
    return buffer;
}

std::vector<float> mono_mix(const AudioBuffer &buffer) {
    std::vector<float> mono(static_cast<size_t>(buffer.frames()));
    for (size_t frame = 0; frame < mono.size(); ++frame) {
        mono[frame] = 0.5f * (buffer.samples[2 * frame] + buffer.samples[2 * frame + 1]);
    }
    return mono;
}

} // namespace psyight
