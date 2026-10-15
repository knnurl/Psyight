// Smoke tests for the M1 libraries the app does not use yet (SPEC section 18, A10): each one
// builds, links and does real work under the project's compile settings. Replace them with unit
// tests of the modules that use these libraries as those modules land.

#include <doctest/doctest.h>
#include <miniaudio.h>
#include <nlohmann/json.hpp>
#include <pffft/pffft.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

// Builds a mono 16-bit PCM WAV file in memory.
std::vector<std::uint8_t> make_wav_mono_s16(const std::vector<std::int16_t> &samples,
                                            std::uint32_t sample_rate_hz) {
    std::vector<std::uint8_t> bytes;
    const auto put_tag = [&bytes](std::string_view tag) {
        for (const char c : tag) {
            bytes.push_back(static_cast<std::uint8_t>(c));
        }
    };
    const auto put_le = [&bytes](std::uint32_t value, int byte_count) {
        for (int i = 0; i < byte_count; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    };
    const auto data_bytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    put_tag("RIFF");
    put_le(36 + data_bytes, 4);
    put_tag("WAVEfmt ");
    put_le(16, 4);                 // fmt chunk size
    put_le(1, 2);                  // PCM
    put_le(1, 2);                  // mono
    put_le(sample_rate_hz, 4);     // frames per second
    put_le(sample_rate_hz * 2, 4); // bytes per second
    put_le(2, 2);                  // bytes per frame
    put_le(16, 2);                 // bits per sample
    put_tag("data");
    put_le(data_bytes, 4);
    for (const std::int16_t sample : samples) {
        put_le(static_cast<std::uint16_t>(sample), 2);
    }
    return bytes;
}

} // namespace

TEST_CASE("pffft: the spectrum of a unit impulse is flat") {
    constexpr int kFftSizeSamples = 1024;
    PFFFT_Setup *setup = pffft_new_setup(kFftSizeSamples, PFFFT_REAL);
    REQUIRE(setup != nullptr);
    auto *input = static_cast<float *>(pffft_aligned_malloc(kFftSizeSamples * sizeof(float)));
    auto *output = static_cast<float *>(pffft_aligned_malloc(kFftSizeSamples * sizeof(float)));
    std::fill(input, input + kFftSizeSamples, 0.0f);
    input[0] = 1.0f;

    pffft_transform_ordered(setup, input, output, nullptr, PFFFT_FORWARD);

    // Ordered real output: DC, Nyquist, then (re, im) for bins 1 to N/2 - 1.
    float max_error = std::max(std::abs(output[0] - 1.0f), std::abs(output[1] - 1.0f));
    for (int bin = 1; bin < kFftSizeSamples / 2; ++bin) {
        const float magnitude = std::hypot(output[2 * bin], output[2 * bin + 1]);
        max_error = std::max(max_error, std::abs(magnitude - 1.0f));
    }
    CHECK(max_error < 1e-6f);

    pffft_aligned_free(output);
    pffft_aligned_free(input);
    pffft_destroy_setup(setup);
}

TEST_CASE("miniaudio: decodes an in-memory WAV and resamples 44.1 kHz to 48 kHz") {
    constexpr std::uint32_t kSourceRateHz = 44100;
    constexpr std::uint32_t kTargetRateHz = 48000;
    constexpr std::int16_t kHalfScale = 16384; // 0.5 as a float sample
    const std::vector<std::uint8_t> wav =
        make_wav_mono_s16(std::vector<std::int16_t>(kSourceRateHz / 10, kHalfScale), kSourceRateHz);

    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, kTargetRateHz);
    ma_decoder decoder;
    REQUIRE(ma_decoder_init_memory(wav.data(), wav.size(), &config, &decoder) == MA_SUCCESS);
    std::vector<float> samples(kTargetRateHz / 5); // twice the expected length
    ma_uint64 frames_read = 0;
    const ma_result result =
        ma_decoder_read_pcm_frames(&decoder, samples.data(), samples.size(), &frames_read);
    ma_decoder_uninit(&decoder);

    CHECK((result == MA_SUCCESS || result == MA_AT_END));
    CHECK(frames_read == kTargetRateHz / 10);
    CHECK(samples[frames_read / 2] == doctest::Approx(0.5f));
}

TEST_CASE("nlohmann-json: parses an excerpt of the SPEC 6.10 labels example") {
    const auto labels = nlohmann::json::parse(R"({
        "schema": 1,
        "grid": { "bpm": 160.0, "first_downbeat_s": 0.000 },
        "bass_notes": [ { "onset_s": 60.105, "offset_s": 60.171, "midi": 29, "cents": 0 } ]
    })");
    CHECK(labels.at("schema").get<int>() == 1);
    CHECK(labels.at("grid").at("bpm").get<double>() == 160.0);
    CHECK(labels.at("bass_notes").at(0).at("midi").get<int>() == 29);
}
