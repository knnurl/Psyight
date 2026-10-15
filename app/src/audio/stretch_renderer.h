// Pitch-preserving slow playback for auditioning (labelling at 0.5x), using Signalsmith Stretch
// (MIT, vendored in third_party).
//
// render() reads speed * frame_count input frames through the transport rules (loop wrap, stop at
// the end) and writes frame_count stretched output frames. prepare() allocates; reset() and
// render() do not, so they run on the audio thread.
// Latency: input_latency + speed * output_latency input frames. Measured with presetDefault at
// 48 kHz: 2880 + 2880 samples, so 90 ms of input at 0.5x. heard_position() compensates for it.
#pragma once

#include "audio/audio_file.h"
#include "audio/transport_render.h"

#include <memory>
#include <vector>

namespace psyight {

class StretchRenderer {
  public:
    static constexpr int kMaxBlockFrames = 4096;

    StretchRenderer();
    ~StretchRenderer();
    StretchRenderer(const StretchRenderer &) = delete;
    StretchRenderer &operator=(const StretchRenderer &) = delete;

    void prepare(int sample_rate_hz);
    void reset(); // After a seek or a speed change: starts the stretcher from silence.

    // Interleaved stereo out. Uses state.speed (0 < speed <= 1) and state.gain.
    void render(TransportState &state, const AudioBuffer &buffer, float *out,
                SampleIndex frame_count);

    // The input position currently being heard, behind state.position by the stretcher latency.
    SampleIndex heard_position(const TransportState &state) const;

  private:
    struct Engine;
    std::unique_ptr<Engine> engine_;
    std::vector<float> in_left_, in_right_, out_left_, out_right_;
    double input_remainder_ = 0.0; // Fractional input frames carried between blocks.
};

} // namespace psyight
