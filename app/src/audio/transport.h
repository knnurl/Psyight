// File-mode playback transport (SPEC section 5): play, pause, seek and loop region over a decoded
// AudioBuffer, played through miniaudio (WASAPI on Windows).
//
// Threads: the GUI thread calls the public methods; the audio callback only drains a command
// queue, renders and publishes the position. The callback never allocates, locks or logs.
// Latency: one device period (reported by device_period_frames()).
#pragma once

#include "audio/audio_file.h"
#include "core/result.h"
#include "core/spsc_queue.h"
#include "core/timebase.h"

#include <atomic>
#include <memory>

struct ma_device; // miniaudio's device type, kept out of this header.

namespace psyight {

// Playback state owned by the audio thread.
struct TransportState {
    SampleIndex position = 0;
    bool playing = false;
    SampleIndex loop_start = 0;
    SampleIndex loop_end = 0; // No loop unless loop_end > loop_start.
    float gain = 1.0f;
};

// Renders frame_count stereo frames into out (interleaved) and advances the state. Inside an
// active loop region, playback wraps from loop_end to loop_start. At the end of the buffer it
// outputs silence and stops. Pure: no I/O, so it is unit-tested directly.
void render_transport(TransportState &state, const AudioBuffer &buffer, float *out,
                      SampleIndex frame_count);

struct TransportOptions {
    bool null_backend = false; // Silent device that still runs callbacks in real time (tests).
};

class Transport {
  public:
    explicit Transport(TransportOptions options = {});
    ~Transport();
    Transport(const Transport &) = delete;
    Transport &operator=(const Transport &) = delete;

    // Stops any current playback, takes the buffer and starts the output device paused at 0.
    Result<void> open(AudioBuffer buffer);
    void close();
    bool is_open() const;
    const AudioBuffer &buffer() const; // Precondition: is_open().

    void play();
    void pause();
    void seek(SampleIndex position);
    void set_loop(SampleIndex start, SampleIndex end); // end <= start clears the loop.
    void set_gain(float gain);

    // Published by the audio thread after each callback.
    SampleIndex position() const { return position_.load(std::memory_order_acquire); }
    bool playing() const { return playing_.load(std::memory_order_acquire); }
    int device_period_frames() const;

  private:
    struct Impl;
    struct Command {
        enum class Kind { play, pause, seek, set_loop, set_gain } kind = Kind::pause;
        SampleIndex a = 0;
        SampleIndex b = 0;
        float gain = 1.0f;
    };

    void send(const Command &command);
    void on_audio(float *out, SampleIndex frame_count);
    // Exactly miniaudio's ma_device_data_proc signature (ma_uint32 is unsigned int).
    static void audio_callback(ma_device *device, void *out, const void *in,
                               unsigned int frame_count);

    TransportOptions options_;
    std::unique_ptr<Impl> impl_;
    SpscQueue<Command> commands_{256};
    TransportState state_; // Audio thread only, after open().
    std::atomic<SampleIndex> position_{0};
    std::atomic<bool> playing_{false};
};

} // namespace psyight
