// Live audio input (SPEC section 5, M5): WASAPI loopback (whatever the laptop plays) or an input
// device (an interface carrying the mixer's booth output), through miniaudio, as mono 48 kHz
// float. miniaudio converts the device's format, channels and rate.
//
// Threads: the audio callback pushes samples into a lock-free ring and publishes the capture
// clock; one consumer thread reads them. The callback never allocates, locks or logs; when the
// ring is full it drops samples and counts them. The capture clock (samples captured since open)
// is the master clock in live mode (SPEC 4.1). Latency: one device period of buffering.
#pragma once

#include "core/result.h"
#include "core/spsc_queue.h"
#include "core/timebase.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct ma_device;

namespace psyight {

enum class CaptureSource { loopback, input };

struct CaptureOptions {
    CaptureSource source = CaptureSource::loopback;
    std::string device_name; // Input devices: empty for the default.
    int sample_rate_hz = 48000;
    bool null_backend = false; // Silence at real-time pace (tests).
};

class Capture {
  public:
    Capture();
    ~Capture();
    Capture(const Capture &) = delete;
    Capture &operator=(const Capture &) = delete;

    Result<void> open(const CaptureOptions &options);
    void close();
    bool is_open() const;

    // Input device names (for CaptureSource::input). Loopback uses the default output device.
    static Result<std::vector<std::string>> input_devices();

    // Consumer thread: up to out.size() captured samples, oldest first.
    size_t read(std::span<float> out);
    // Samples captured since open (published after each callback), and extrapolated to now from
    // the last callback's time like Transport::position_now (at most two periods ahead).
    SampleIndex captured() const { return captured_.load(std::memory_order_acquire); }
    SampleIndex captured_now() const;
    long long dropped() const { return dropped_.load(std::memory_order_acquire); }
    int sample_rate_hz() const { return sample_rate_hz_; }
    int device_period_frames() const;
    // Peak of the last callback's block, 0 to 1 (a level meter).
    float peak() const { return peak_.load(std::memory_order_acquire); }

  private:
    struct Impl;
    void on_audio(const float *in, unsigned int frame_count);
    static void audio_callback(ma_device *device, void *out, const void *in,
                               unsigned int frame_count);

    std::unique_ptr<Impl> impl_;
    SpscQueue<float> ring_{1 << 17}; // 2.7 s at 48 kHz.
    int sample_rate_hz_ = 48000;
    std::atomic<SampleIndex> captured_{0};
    std::atomic<long long> dropped_{0};
    std::atomic<float> peak_{0.0f};
    std::atomic<std::uint32_t> stamp_sequence_{0};
    std::atomic<SampleIndex> stamp_captured_{0};
    std::atomic<std::int64_t> stamp_time_ns_{0};
};

} // namespace psyight
