#include "audio/transport_render.h"

namespace psyight {

bool next_frame(TransportState &state, const AudioBuffer &buffer, float &left, float &right) {
    left = right = 0.0f;
    const bool looping = state.loop_end > state.loop_start;
    if (state.playing && looping && state.position == state.loop_end) {
        state.position = state.loop_start;
    }
    if (!state.playing || state.position < 0 || state.position >= buffer.frames()) {
        if (state.position >= buffer.frames()) {
            state.playing = false;
        }
        return false;
    }
    const float *in = buffer.samples.data() + state.position * kPlaybackChannels;
    left = in[0];
    right = in[1];
    ++state.position;
    return true;
}

void render_transport(TransportState &state, const AudioBuffer &buffer, float *out,
                      SampleIndex frame_count) {
    for (SampleIndex frame = 0; frame < frame_count; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        next_frame(state, buffer, left, right);
        out[2 * frame] = state.gain * left;
        out[2 * frame + 1] = state.gain * right;
    }
}

} // namespace psyight
