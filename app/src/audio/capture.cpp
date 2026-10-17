#include "audio/capture.h"

#include <miniaudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace psyight {

namespace {

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

struct Capture::Impl {
    ma_context context{};
    ma_device device{};
    bool context_ready = false;
    bool device_ready = false;
};

Capture::Capture() = default;

Capture::~Capture() { close(); }

Result<std::vector<std::string>> Capture::input_devices() {
    ma_context context;
    if (const ma_result r = ma_context_init(nullptr, 0, nullptr, &context); r != MA_SUCCESS) {
        return Error{std::string("audio context: ") + ma_result_description(r)};
    }
    ma_device_info *playback = nullptr;
    ma_device_info *capture = nullptr;
    ma_uint32 playback_count = 0;
    ma_uint32 capture_count = 0;
    std::vector<std::string> names;
    if (ma_context_get_devices(&context, &playback, &playback_count, &capture, &capture_count) ==
        MA_SUCCESS) {
        for (ma_uint32 i = 0; i < capture_count; ++i) {
            names.emplace_back(capture[i].name);
        }
    }
    ma_context_uninit(&context);
    return names;
}

Result<void> Capture::open(const CaptureOptions &options) {
    close();
    impl_ = std::make_unique<Impl>();
    sample_rate_hz_ = options.sample_rate_hz;
    captured_.store(0, std::memory_order_release);
    dropped_.store(0, std::memory_order_release);
    stamp_time_ns_.store(0, std::memory_order_release);
    float discarded = 0.0f;
    while (ring_.try_pop(discarded)) {
    }

    const ma_backend null_backend = ma_backend_null;
    const ma_result context_result =
        options.null_backend ? ma_context_init(&null_backend, 1, nullptr, &impl_->context)
                             : ma_context_init(nullptr, 0, nullptr, &impl_->context);
    if (context_result != MA_SUCCESS) {
        impl_.reset();
        return Error{std::string("audio context: ") + ma_result_description(context_result)};
    }
    impl_->context_ready = true;

    const bool loopback = options.source == CaptureSource::loopback && !options.null_backend;
    ma_device_config config =
        ma_device_config_init(loopback ? ma_device_type_loopback : ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1; // miniaudio mixes the device's channels down.
    config.sampleRate = static_cast<ma_uint32>(options.sample_rate_hz);
    config.dataCallback = &Capture::audio_callback;
    config.pUserData = this;
    ma_device_id chosen{};
    if (options.source == CaptureSource::input && !options.device_name.empty() &&
        !options.null_backend) {
        ma_device_info *playback = nullptr;
        ma_device_info *capture = nullptr;
        ma_uint32 playback_count = 0;
        ma_uint32 capture_count = 0;
        bool found = false;
        if (ma_context_get_devices(&impl_->context, &playback, &playback_count, &capture,
                                   &capture_count) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < capture_count && !found; ++i) {
                if (options.device_name == capture[i].name) {
                    chosen = capture[i].id;
                    found = true;
                }
            }
        }
        if (!found) {
            close();
            return Error{"no input device named \"" + options.device_name + "\""};
        }
        config.capture.pDeviceID = &chosen;
    }
    const ma_result device_result = ma_device_init(&impl_->context, &config, &impl_->device);
    if (device_result != MA_SUCCESS) {
        close();
        return Error{std::string(loopback ? "loopback capture: " : "audio input: ") +
                     ma_result_description(device_result)};
    }
    impl_->device_ready = true;
    if (const ma_result r = ma_device_start(&impl_->device); r != MA_SUCCESS) {
        close();
        return Error{std::string("audio start: ") + ma_result_description(r)};
    }
    return {};
}

void Capture::close() {
    if (!impl_) {
        return;
    }
    if (impl_->device_ready) {
        ma_device_uninit(&impl_->device); // Stops the device and joins its thread.
    }
    if (impl_->context_ready) {
        ma_context_uninit(&impl_->context);
    }
    impl_.reset();
}

bool Capture::is_open() const { return impl_ != nullptr; }

int Capture::device_period_frames() const {
    return impl_ && impl_->device_ready
               ? static_cast<int>(impl_->device.capture.internalPeriodSizeInFrames)
               : 0;
}

size_t Capture::read(std::span<float> out) {
    size_t n = 0;
    while (n < out.size() && ring_.try_pop(out[n])) {
        ++n;
    }
    return n;
}

SampleIndex Capture::captured_now() const {
    SampleIndex at = 0;
    std::int64_t time_ns = 0;
    for (;;) {
        const std::uint32_t before = stamp_sequence_.load(std::memory_order_acquire);
        if (before & 1u) {
            continue;
        }
        at = stamp_captured_.load(std::memory_order_relaxed);
        time_ns = stamp_time_ns_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (stamp_sequence_.load(std::memory_order_relaxed) == before) {
            break;
        }
    }
    if (time_ns == 0 || !is_open()) {
        return captured();
    }
    const double ahead =
        std::clamp(static_cast<double>(now_ns() - time_ns) * 1e-9 * sample_rate_hz_, 0.0,
                   2.0 * device_period_frames());
    return at + static_cast<SampleIndex>(ahead);
}

void Capture::audio_callback(ma_device *device, void * /*out*/, const void *in,
                             unsigned int frame_count) {
    static_cast<Capture *>(device->pUserData)
        ->on_audio(static_cast<const float *>(in), frame_count);
}

void Capture::on_audio(const float *in, unsigned int frame_count) {
    long long lost = 0;
    float peak = 0.0f;
    for (unsigned int i = 0; i < frame_count; ++i) {
        const float s = in != nullptr ? in[i] : 0.0f;
        peak = std::max(peak, std::abs(s));
        if (!ring_.try_push(s)) {
            ++lost;
        }
    }
    if (lost > 0) {
        dropped_.fetch_add(lost, std::memory_order_acq_rel);
    }
    peak_.store(peak, std::memory_order_release);
    const SampleIndex total = captured_.load(std::memory_order_relaxed) + frame_count;
    captured_.store(total, std::memory_order_release);
    const std::uint32_t sequence = stamp_sequence_.load(std::memory_order_relaxed);
    stamp_sequence_.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    stamp_captured_.store(total, std::memory_order_relaxed);
    stamp_time_ns_.store(now_ns(), std::memory_order_relaxed);
    stamp_sequence_.store(sequence + 2, std::memory_order_release);
}

} // namespace psyight
