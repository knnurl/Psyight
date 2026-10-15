#include "audio/transport.h"

#include "audio/stretch_renderer.h"

#include <miniaudio.h>

#include <algorithm>

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
    if (state_.speed < 1.0f) {
        impl_->stretch.render(state_, impl_->buffer, out, frame_count);
        position_.store(state_.playing ? impl_->stretch.heard_position(state_) : state_.position,
                        std::memory_order_release);
    } else {
        render_transport(state_, impl_->buffer, out, frame_count);
        position_.store(state_.position, std::memory_order_release);
    }
    speed_.store(state_.speed, std::memory_order_release);
    playing_.store(state_.playing, std::memory_order_release);
}

} // namespace psyight
