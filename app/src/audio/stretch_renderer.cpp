#include "audio/stretch_renderer.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr long kStretchSeed = 1; // Fixed so audition output is repeatable.

} // namespace

struct StretchRenderer::Engine {
    signalsmith::stretch::SignalsmithStretch<float> stretch{kStretchSeed};
};

StretchRenderer::StretchRenderer() : engine_(std::make_unique<Engine>()) {}
StretchRenderer::~StretchRenderer() = default;

void StretchRenderer::prepare(int sample_rate_hz) {
    engine_->stretch.presetDefault(kPlaybackChannels, static_cast<float>(sample_rate_hz));
    for (std::vector<float> *channel : {&in_left_, &in_right_, &out_left_, &out_right_}) {
        channel->assign(kMaxBlockFrames, 0.0f);
    }
    reset();
}

void StretchRenderer::reset() {
    engine_->stretch.reset();
    input_remainder_ = 0.0;
}

void StretchRenderer::render(TransportState &state, const AudioBuffer &buffer, float *out,
                             SampleIndex frame_count) {
    for (SampleIndex done = 0; done < frame_count;) {
        const int block =
            static_cast<int>(std::min<SampleIndex>(kMaxBlockFrames, frame_count - done));
        // Input for this block: speed * block frames, carrying the fraction to the next block.
        const double wanted = input_remainder_ + state.speed * block;
        const int input_frames = std::min(kMaxBlockFrames, static_cast<int>(std::floor(wanted)));
        input_remainder_ = wanted - input_frames;
        for (int i = 0; i < input_frames; ++i) {
            next_frame(state, buffer, in_left_[i], in_right_[i]);
        }
        float *inputs[kPlaybackChannels] = {in_left_.data(), in_right_.data()};
        float *outputs[kPlaybackChannels] = {out_left_.data(), out_right_.data()};
        engine_->stretch.process(inputs, input_frames, outputs, block);
        for (int i = 0; i < block; ++i) {
            out[2 * (done + i)] = state.gain * out_left_[i];
            out[2 * (done + i) + 1] = state.gain * out_right_[i];
        }
        done += block;
    }
}

SampleIndex StretchRenderer::heard_position(const TransportState &state) const {
    const double latency =
        engine_->stretch.inputLatency() + state.speed * engine_->stretch.outputLatency();
    return std::max<SampleIndex>(0,
                                 state.position - static_cast<SampleIndex>(std::lround(latency)));
}

} // namespace psyight
