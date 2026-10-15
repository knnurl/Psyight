#include "audio/audio_file.h"
#include "audio/transport.h"

#include <doctest/doctest.h>
#include <miniaudio.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

using namespace psyight;

namespace {

// Writes a constant-valued 16-bit WAV with miniaudio's encoder.
void write_wav(const std::filesystem::path &path, int channels, int sample_rate_hz,
               const std::vector<float> &frame_values, ma_uint64 frames) {
    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32,
                                                      static_cast<ma_uint32>(channels),
                                                      static_cast<ma_uint32>(sample_rate_hz));
    ma_encoder encoder;
    REQUIRE(ma_encoder_init_file_w(path.c_str(), &config, &encoder) == MA_SUCCESS);
    std::vector<float> data;
    for (ma_uint64 i = 0; i < frames; ++i) {
        data.insert(data.end(), frame_values.begin(), frame_values.end());
    }
    ma_uint64 written = 0;
    REQUIRE(ma_encoder_write_pcm_frames(&encoder, data.data(), frames, &written) == MA_SUCCESS);
    ma_encoder_uninit(&encoder);
}

AudioBuffer ramp_buffer(SampleIndex frames) {
    AudioBuffer buffer;
    for (SampleIndex i = 0; i < frames; ++i) {
        buffer.samples.push_back(static_cast<float>(i));
        buffer.samples.push_back(static_cast<float>(-i));
    }
    return buffer;
}

std::vector<float> left_channel(const std::vector<float> &stereo) {
    std::vector<float> left;
    for (size_t i = 0; i < stereo.size(); i += 2) {
        left.push_back(stereo[i]);
    }
    return left;
}

bool wait_until(const std::function<bool()> &condition, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 5) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return condition();
}

} // namespace

TEST_CASE("audio file: 44.1 kHz stereo WAV decodes to 48 kHz, mono mix is (L + R) / 2") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "psyight_audio_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / L"tést ♫.wav"; // Non-ASCII name.
    write_wav(path, 2, 44100, {0.5f, -0.25f}, 44100);

    const Result<AudioBuffer> decoded = decode_audio_file(path);
    REQUIRE(decoded.ok());
    const AudioBuffer &buffer = decoded.value();
    CHECK(buffer.sample_rate_hz == 48000);
    CHECK(buffer.frames() == 48000);
    const size_t middle = 24000;
    CHECK(buffer.samples[2 * middle] == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(buffer.samples[2 * middle + 1] == doctest::Approx(-0.25f).epsilon(1e-4));
    CHECK(mono_mix(buffer)[middle] == doctest::Approx(0.125f).epsilon(1e-3));
    std::filesystem::remove_all(dir);
}

TEST_CASE("audio file: mono files play on both channels; missing files are errors") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "psyight_audio_mono";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "mono.wav";
    write_wav(path, 1, 48000, {0.25f}, 4800);
    const Result<AudioBuffer> decoded = decode_audio_file(path);
    REQUIRE(decoded.ok());
    CHECK(decoded.value().frames() == 4800);
    CHECK(decoded.value().samples[200] == decoded.value().samples[201]);
    CHECK(mono_mix(decoded.value())[100] == doctest::Approx(0.25f).epsilon(1e-4));

    const Result<AudioBuffer> missing = decode_audio_file(dir / "missing.flac");
    CHECK_FALSE(missing.ok());
    std::filesystem::remove_all(dir);
}

TEST_CASE("transport render: plays with gain, pauses without advancing, stops at the end") {
    const AudioBuffer buffer = ramp_buffer(10);
    TransportState state;
    state.playing = true;
    state.gain = 0.5f;
    std::vector<float> out(2 * 4);
    render_transport(state, buffer, out.data(), 4);
    CHECK(left_channel(out) == std::vector<float>{0.0f, 0.5f, 1.0f, 1.5f});
    CHECK(out[3] == -0.5f);
    CHECK(state.position == 4);

    state.playing = false;
    render_transport(state, buffer, out.data(), 4);
    CHECK(left_channel(out) == std::vector<float>(4, 0.0f));
    CHECK(state.position == 4);

    state.playing = true;
    state.gain = 1.0f;
    std::vector<float> tail(2 * 8);
    render_transport(state, buffer, tail.data(), 8);
    CHECK(left_channel(tail) == std::vector<float>{4, 5, 6, 7, 8, 9, 0, 0});
    CHECK_FALSE(state.playing);
    CHECK(state.position == 10);
}

TEST_CASE("transport render: the loop region wraps exactly at its end") {
    const AudioBuffer buffer = ramp_buffer(20);
    TransportState state;
    state.playing = true;
    state.position = 2;
    state.loop_start = 4;
    state.loop_end = 7;
    std::vector<float> out(2 * 10);
    render_transport(state, buffer, out.data(), 10);
    CHECK(left_channel(out) == std::vector<float>{2, 3, 4, 5, 6, 4, 5, 6, 4, 5});

    // A position already beyond the loop (after a seek) plays on to the end of the file.
    state.position = 15;
    render_transport(state, buffer, out.data(), 10);
    CHECK(left_channel(out) == std::vector<float>{15, 16, 17, 18, 19, 0, 0, 0, 0, 0});
}

TEST_CASE("transport: a device thread plays, pauses, seeks, loops and stops (null backend)") {
    Transport transport({true});
    REQUIRE(transport.open(ramp_buffer(48000 * 4)).ok()); // 4 s.
    CHECK(transport.position() == 0);
    CHECK(transport.device_period_frames() > 0);

    transport.play();
    REQUIRE(wait_until([&] { return transport.position() > 4800; }, 2000));
    transport.pause();
    REQUIRE(wait_until([&] { return !transport.playing(); }, 1000));
    const SampleIndex paused_at = transport.position();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(transport.position() == paused_at);

    transport.seek(48000 * 2);
    REQUIRE(wait_until([&] { return transport.position() == 48000 * 2; }, 1000));

    transport.set_loop(48000, 48000 + 2400); // 50 ms loop.
    transport.seek(48000);
    transport.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const SampleIndex looping_at = transport.position();
    CHECK(looping_at >= 48000);
    CHECK(looping_at <= 48000 + 2400);

    transport.set_loop(0, 0);
    transport.seek(48000 * 4 - 480); // 10 ms before the end.
    REQUIRE(wait_until([&] { return !transport.playing(); }, 2000));
    CHECK(transport.position() == 48000 * 4);
    transport.close();
    CHECK_FALSE(transport.is_open());
}
