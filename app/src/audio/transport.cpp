#include "audio/transport.h"

#include "audio/stretch_renderer.h"

#include <miniaudio.h>

#include <algorithm>
#include <chrono>

namespace psyight {

namespace {

constexpr float kMinSpeed = 0.25f;

} // namespace

struct Transport::Impl {
    AudioBuffer buffer;
    StretchRenderer stretch;
    ma_context context{};
    ma_device device{};
    bool context_ready = false;
    bool device_ready = false;
};

Transport::Transport(TransportOptions options) : options_(options) {}

Transport::~Transport() { close(); }

Result<void> Transport::open(AudioBuffer buffer) {
    close();
    impl_ = std::make_unique<Impl>();
    impl_->buffer = std::move(buffer);
    frames_.store(impl_->buffer.frames(), std::memory_order_release);
    impl_->stretch.prepare(impl_->buffer.sample_rate_hz); // Allocates: before the device starts.
    state_ = TransportState{};
    position_.store(0, std::memory_order_release);
    playing_.store(false, std::memory_order_release);
    speed_.store(1.0f, std::memory_order_release);
    Command discarded;
    while (commands_.try_pop(discarded)) {
    }

    const ma_backend null_backend = ma_backend_null;
    const ma_result context_result =
        options_.null_backend ? ma_context_init(&null_backend, 1, nullptr, &impl_->context)
                              : ma_context_init(nullptr, 0, nullptr, &impl_->context);
    if (context_result != MA_SUCCESS) {
        impl_.reset();
        return Error{std::string("audio context: ") + ma_result_description(context_result)};
    }
    impl_->context_ready = true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kPlaybackChannels;
    config.sampleRate = static_cast<ma_uint32>(impl_->buffer.sample_rate_hz);
    config.dataCallback = &Transport::audio_callback;
    config.pUserData = this;
    const ma_result device_result = ma_device_init(&impl_->context, &config, &impl_->device);
    if (device_result != MA_SUCCESS) {
        close();
        return Error{std::string("audio device: ") + ma_result_description(device_result)};
    }
    impl_->device_ready = true;
    const ma_result start_result = ma_device_start(&impl_->device);
    if (start_result != MA_SUCCESS) {
        close();
        return Error{std::string("audio start: ") + ma_result_description(start_result)};
    }
    return {};
}

void Transport::close() {
    clear_next();
    free_retired();
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
    playing_.store(false, std::memory_order_release);
}

bool Transport::is_open() const { return impl_ != nullptr; }

const AudioBuffer &Transport::buffer() const { return impl_->buffer; }

SampleIndex Transport::position_now() const {
    if (!playing() || !is_open()) {
        return position();
    }
    SampleIndex at = 0;
    std::int64_t time_ns = 0;
    for (;;) {
        const std::uint32_t before = stamp_sequence_.load(std::memory_order_acquire);
        if (before & 1u) {
            continue; // The callback is writing.
        }
        at = stamp_position_.load(std::memory_order_relaxed);
        time_ns = stamp_time_ns_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (stamp_sequence_.load(std::memory_order_relaxed) == before) {
            break;
        }
    }
    if (time_ns == 0) {
        return position(); // No callback yet.
    }
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
    const double rate = impl_->buffer.sample_rate_hz * static_cast<double>(speed());
    const double ahead = std::clamp(static_cast<double>(now_ns - time_ns) * 1e-9 * rate, 0.0,
                                    2.0 * device_period_frames());
    return std::min<SampleIndex>(at + static_cast<SampleIndex>(ahead),
                                 frames_.load(std::memory_order_acquire));
}

int Transport::device_period_frames() const {
    return impl_ && impl_->device_ready
               ? static_cast<int>(impl_->device.playback.internalPeriodSizeInFrames)
               : 0;
}

void Transport::play() { send({Command::Kind::play}); }
void Transport::pause() { send({Command::Kind::pause}); }
void Transport::seek(SampleIndex position) { send({Command::Kind::seek, position}); }
void Transport::set_loop(SampleIndex start, SampleIndex end) {
    send({Command::Kind::set_loop, start, end});
}
void Transport::set_gain(float gain) { send({Command::Kind::set_gain, 0, 0, gain}); }
void Transport::set_speed(float speed) { send({Command::Kind::set_speed, 0, 0, speed}); }

bool Transport::queue_next(AudioBuffer buffer) {
    free_retired();
    if (!impl_ || buffer.sample_rate_hz != impl_->buffer.sample_rate_hz || buffer.frames() == 0) {
        return false;
    }
    delete next_.exchange(new AudioBuffer(std::move(buffer)), std::memory_order_acq_rel);
    return true;
}

void Transport::clear_next() { delete next_.exchange(nullptr, std::memory_order_acq_rel); }

void Transport::free_retired() { delete retired_.exchange(nullptr, std::memory_order_acq_rel); }

void Transport::send(const Command &command) {
    // The queue holds 256 commands and the audio thread drains it every period; a full queue
    // means the device has stalled, so dropping the command is the safe choice.
    (void)commands_.try_push(command);
}

void Transport::audio_callback(ma_device *device, void *out, const void *,
                               unsigned int frame_count) {
    auto *self = static_cast<Transport *>(device->pUserData);
    self->on_audio(static_cast<float *>(out), frame_count);
}

void Transport::on_audio(float *out, SampleIndex frame_count) {
    Command command;
    while (commands_.try_pop(command)) {
        switch (command.kind) {
        case Command::Kind::play:
            if (state_.position >= impl_->buffer.frames()) {
                state_.position = 0; // Play after the end restarts from the beginning.
            }
            state_.playing = true;
            break;
        case Command::Kind::pause:
            state_.playing = false;
            break;
        case Command::Kind::seek:
            state_.position = std::clamp<SampleIndex>(command.a, 0, impl_->buffer.frames());
            impl_->stretch.reset(); // Do not smear the old position into the new one.
            break;
        case Command::Kind::set_loop:
            state_.loop_start = command.a;
            state_.loop_end = command.b;
            break;
        case Command::Kind::set_gain:
            state_.gain = command.value;
            break;
        case Command::Kind::set_speed:
            state_.speed = std::clamp(command.value, kMinSpeed, 1.0f);
            impl_->stretch.reset();
            break;
        }
    }
    SampleIndex published = 0;
    if (state_.speed < 1.0f) {
        impl_->stretch.render(state_, impl_->buffer, out, frame_count);
        published = state_.playing ? impl_->stretch.heard_position(state_) : state_.position;
    } else {
        SampleIndex done = 0;
        const bool looping = state_.loop_end > state_.loop_start;
        // The current buffer ends inside this callback: play it out, then continue into the next
        // one (gapless), if one is queued.
        const SampleIndex left = impl_->buffer.frames() - state_.position;
        if (state_.playing && !looping && left >= 0 && left < frame_count &&
            next_.load(std::memory_order_acquire) != nullptr) {
            render_transport(state_, impl_->buffer, out, left);
            if (AudioBuffer *next = next_.exchange(nullptr, std::memory_order_acq_rel)) {
                std::swap(impl_->buffer.samples, next->samples); // Pointers only.
                frames_.store(impl_->buffer.frames(), std::memory_order_release);
                // The old samples go back to the GUI thread to be freed; one not yet collected
                // (two switches between its polls) is freed here, which never happens in a set.
                delete retired_.exchange(next, std::memory_order_acq_rel);
                state_.position = 0;
                state_.playing = true;
                switches_.fetch_add(1, std::memory_order_acq_rel);
                done = left;
            }
        }
        render_transport(state_, impl_->buffer, out + 2 * done, frame_count - done);
        published = state_.position;
    }
    position_.store(published, std::memory_order_release);
    // Seqlock: odd while writing; the release fence orders the data after the odd count.
    const std::uint32_t sequence = stamp_sequence_.load(std::memory_order_relaxed);
    stamp_sequence_.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    stamp_position_.store(published, std::memory_order_relaxed);
    stamp_time_ns_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count(),
                         std::memory_order_relaxed);
    stamp_sequence_.store(sequence + 2, std::memory_order_release);
    speed_.store(state_.speed, std::memory_order_release);
    playing_.store(state_.playing, std::memory_order_release);
}

} // namespace psyight
