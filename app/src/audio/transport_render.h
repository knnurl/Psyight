// The transport's playback rules as pure functions (no device, no I/O), shared by normal and
// stretched playback and unit-tested directly.
#pragma once

#include "audio/audio_file.h"
#include "core/timebase.h"

namespace psyight {

// Playback state owned by the audio thread.
struct TransportState {
    SampleIndex position = 0;
    bool playing = false;
    SampleIndex loop_start = 0;
    SampleIndex loop_end = 0; // No loop unless loop_end > loop_start.
    float gain = 1.0f;
    float speed = 1.0f; // 1, or slower for pitch-preserving audition (StretchRenderer).
};

// Reads the frame at the playhead and advances it. Inside an active loop region, playback wraps
// from loop_end to loop_start; a position already beyond the loop plays on. At the end of the
// buffer playback stops. Returns false (and silence) when not playing.
bool next_frame(TransportState &state, const AudioBuffer &buffer, float &left, float &right);

// Renders frame_count interleaved stereo frames at normal speed, applying the gain.
void render_transport(TransportState &state, const AudioBuffer &buffer, float *out,
                      SampleIndex frame_count);

} // namespace psyight
