// Section detection on a synthetic arrangement with known sections.

#include "analysis/beat_tracker.h"
#include "analysis/front_end.h"
#include "analysis/sections.h"
#include "synth_generator.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;

struct Expected {
    int start_bar;
    const char *label;
};

// 40 bars of rolling bass. Kicks in bars 4 to 11 and 24 to 35; a noise riser in bars 20 to 23.
// Expected: intro 0, groove 4, breakdown 12, build 20, drop 24 (8 bars), groove 32, outro 36.
std::vector<float> arrangement(double bpm, double &downbeat_s) {
    synth::SynthParams params;
    params.bpm = bpm;
    params.bars = 40;
    synth::SynthParams bass_params = params;
    bass_params.kick = false;
    synth::SynthOutput mix =
        synth::render(bass_params, synth::make_pattern(synth::Pattern::rolling, 40, 29, 0.7));
    synth::SynthParams kick_params = params;
    kick_params.bass_level = 0.0;
    const synth::SynthOutput kicks = synth::render(kick_params, {});
    downbeat_s = mix.labels.downbeats.front().t_s;
    const double bar_s = 4.0 * 60.0 / bpm;
    std::uint32_t noise = 12345;
    for (size_t i = 0; i < mix.samples.size(); ++i) {
        const double bar = (static_cast<double>(i) / kSampleRateHz - downbeat_s) / bar_s;
        if ((bar >= 4.0 && bar < 12.0) || (bar >= 24.0 && bar < 36.0)) {
            mix.samples[i] += kicks.samples[i];
        }
        if (bar >= 20.0 && bar < 24.0) {
            noise = noise * 1664525u + 1013904223u; // Deterministic white noise.
            const double white = static_cast<double>(noise) / 4294967296.0 * 2.0 - 1.0;
            const double gain_db = -60.0 + 45.0 * (bar - 20.0) / 4.0; // -60 to -15 dB.
            mix.samples[i] += static_cast<float>(white * std::pow(10.0, gain_db / 20.0));
        }
    }
    return mix.samples;
}

} // namespace

TEST_CASE("sections: intro, groove, breakdown, build, drop and outro of a synthetic track") {
    const Expected expected[] = {{0, "intro"}, {4, "groove"},  {12, "breakdown"}, {20, "build"},
                                 {24, "drop"}, {32, "groove"}, {36, "outro"}};
    for (const double bpm : {150.0, 160.0}) {
        CAPTURE(bpm);
        double downbeat_s = 0.0;
        const std::vector<float> audio = arrangement(bpm, downbeat_s);
        const Result<FrontEndResult> features = analyse_front_end(audio, kSampleRateHz, {});
        REQUIRE(features.ok());
        const Result<BeatTrackResult> beats = track_beats(features.value(), {});
        REQUIRE(beats.ok());
        const Result<std::vector<DetectedSection>> sections =
            detect_sections(features.value(), beats.value(), {});
        REQUIRE(sections.ok());
        const std::vector<DetectedSection> &found = sections.value();
        const double bar_s = 4.0 * 60.0 / bpm;
        std::string summary;
        for (const DetectedSection &s : found) {
            summary += s.label + "@" + std::to_string((s.start_s - downbeat_s) / bar_s) + " ";
        }
        CAPTURE(summary);
        REQUIRE(found.size() == std::size(expected));
        for (size_t i = 0; i < found.size(); ++i) {
            CAPTURE(i);
            CHECK(found[i].label == expected[i].label);
            if (i > 0) { // The first section starts at 0 s, before the first downbeat.
                CHECK(found[i].start_s ==
                      doctest::Approx(downbeat_s + expected[i].start_bar * bar_s).epsilon(0.001));
            }
            CHECK(found[i].confidence > 0.9);
        }
        CHECK(found.front().start_s == 0.0);
    }
}

TEST_CASE("sections: a flatness riser marks the build, not hats entering mid-breakdown") {
    // 40 bars of rolling bass at 150 BPM. Kicks in bars 4 to 11 and 28 to 35. In the breakdown
    // (bars 12 to 27) hats enter at bar 16, a white noise riser climbs in bars 22 to 25, and
    // bars 26 and 27 are a gap before the drop.
    constexpr double kBpm = 150.0;
    synth::SynthParams params;
    params.bpm = kBpm;
    params.bars = 40;
    synth::SynthParams bass_params = params;
    bass_params.kick = false;
    synth::SynthOutput mix =
        synth::render(bass_params, synth::make_pattern(synth::Pattern::rolling, 40, 29, 0.7));
    synth::SynthParams kick_params = params;
    kick_params.bass_level = 0.0;
    const synth::SynthOutput kicks = synth::render(kick_params, {});
    synth::SynthParams hat_params = kick_params;
    hat_params.kick = false;
    hat_params.hats = true;
    const synth::SynthOutput hats = synth::render(hat_params, {});
    const double downbeat_s = mix.labels.downbeats.front().t_s;
    const double bar_s = 4.0 * 60.0 / kBpm;
    std::uint32_t noise = 12345;
    for (size_t i = 0; i < mix.samples.size(); ++i) {
        const double bar = (static_cast<double>(i) / kSampleRateHz - downbeat_s) / bar_s;
        if ((bar >= 4.0 && bar < 12.0) || (bar >= 28.0 && bar < 36.0)) {
            mix.samples[i] += kicks.samples[i];
        }
        if (bar >= 16.0) {
            mix.samples[i] += hats.samples[i];
        }
        if (bar >= 22.0 && bar < 26.0) {
            noise = noise * 1664525u + 1013904223u; // Deterministic white noise.
            const double white = static_cast<double>(noise) / 4294967296.0 * 2.0 - 1.0;
            const double gain_db = -50.0 + 40.0 * (bar - 22.0) / 4.0; // -50 to -10 dB.
            mix.samples[i] += static_cast<float>(white * std::pow(10.0, gain_db / 20.0));
        }
    }
    const Result<FrontEndResult> features = analyse_front_end(mix.samples, kSampleRateHz, {});
    REQUIRE(features.ok());
    const Result<BeatTrackResult> beats = track_beats(features.value(), {});
    REQUIRE(beats.ok());
    const Result<ResidualFeatures> residual = analyse_residual(features.value(), {});
    REQUIRE(residual.ok());
    const auto build_bar = [&](const ResidualFeatures *with) {
        const Result<std::vector<DetectedSection>> found =
            detect_sections(features.value(), beats.value(), {}, with);
        REQUIRE(found.ok());
        std::string summary;
        double start_bar = -1.0;
        for (const DetectedSection &s : found.value()) {
            const double bar = (s.start_s - downbeat_s) / bar_s;
            summary += s.label + "@" + std::to_string(bar) + " ";
            if (s.label == "build") {
                start_bar = bar;
                CHECK(s.end_s == doctest::Approx(downbeat_s + 28.0 * bar_s).epsilon(0.001));
            }
        }
        CAPTURE(summary);
        CHECK(start_bar >= 0.0);
        return start_bar;
    };
    const double riser = build_bar(&residual.value());
    CHECK(riser >= 21.0);
    CHECK(riser <= 24.0);
    const double hats_only = build_bar(nullptr); // The high-band rule: hats entering at bar 16.
    CHECK(hats_only <= 17.0);
}

TEST_CASE("sections: phrases restart at section changes") {
    // 150 BPM: a bar is 1.6 s. Sections start at bars 0, 4, 12 and 13 of a 40-bar track.
    const Grid grid{150.0, seconds_to_samples(0.5, kSampleRateHz), kSampleRateHz};
    const double bar_s = 1.6;
    const auto at_bar = [&](double bar) { return 0.5 + bar * bar_s; };
    const std::vector<DetectedSection> sections = {{0.0, at_bar(4), "intro", 1.0},
                                                   {at_bar(4), at_bar(12), "groove", 1.0},
                                                   {at_bar(12), at_bar(13), "breakdown", 1.0},
                                                   {at_bar(13), 66.5, "groove", 1.0}};
    const std::vector<double> starts = phrase_starts(grid, sections, 8, 0, 66.5);
    const std::vector<double> expected = {at_bar(0),  at_bar(4),  at_bar(12), at_bar(13),
                                          at_bar(21), at_bar(29), at_bar(37)};
    REQUIRE(starts.size() == expected.size());
    for (size_t i = 0; i < starts.size(); ++i) {
        CAPTURE(i);
        CHECK(starts[i] == doctest::Approx(expected[i]));
    }
    // With no sections and an offset of 3 bars: phrases at bars 3 and 11 of a 20 s track.
    const std::vector<double> offset = phrase_starts(grid, {}, 8, 3, 20.0);
    REQUIRE(offset.size() == 2);
    CHECK(offset[0] == doctest::Approx(at_bar(3)));
    CHECK(offset[1] == doctest::Approx(at_bar(11)));
}

TEST_CASE("sections: no grid gives no sections; a missing band is an error") {
    const std::vector<float> silence(kSampleRateHz * 10, 0.0f);
    const Result<FrontEndResult> features = analyse_front_end(silence, kSampleRateHz, {});
    const Result<std::vector<DetectedSection>> none =
        detect_sections(features.value(), BeatTrackResult{}, {});
    REQUIRE(none.ok());
    CHECK(none.value().empty());
    FrontEndResult no_hats = features.value();
    no_hats.bands.pop_back();
    CHECK_FALSE(detect_sections(no_hats, BeatTrackResult{}, {}).ok());
}

TEST_CASE("sections: parameters round trip through the registry") {
    ParamRegistry registry;
    REQUIRE(declare_section_params(registry).ok());
    REQUIRE(registry.set("section.drop_bars", 16.0).ok());
    CHECK(section_params_from(registry.values()).drop_bars == 16.0);
    CHECK(section_params_from(registry.values()).min_breakdown_bars == 2.0);
}
