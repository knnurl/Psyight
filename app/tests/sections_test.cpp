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

namespace {

// 40 bars of rolling bass at 150 BPM with kicks where kick_at(bar) and the bass scaled by
// bass_gain(bar); the sections found, and the first downbeat and phrase offset.
template <typename KickAt, typename BassGain>
std::vector<DetectedSection> sections_of(KickAt kick_at, BassGain bass_gain, double &downbeat_s,
                                         int &phrase_offset) {
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
    downbeat_s = mix.labels.downbeats.front().t_s;
    const double bar_s = 4.0 * 60.0 / kBpm;
    for (size_t i = 0; i < mix.samples.size(); ++i) {
        const double bar = (static_cast<double>(i) / kSampleRateHz - downbeat_s) / bar_s;
        const int whole = static_cast<int>(std::floor(bar));
        mix.samples[i] = static_cast<float>(mix.samples[i] * bass_gain(whole));
        if (whole >= 0 && kick_at(whole)) {
            mix.samples[i] += kicks.samples[i];
        }
    }
    const Result<FrontEndResult> features = analyse_front_end(mix.samples, kSampleRateHz, {});
    REQUIRE(features.ok());
    const Result<BeatTrackResult> beats = track_beats(features.value(), {});
    REQUIRE(beats.ok());
    phrase_offset = beats.value().phrase_bar_offset;
    const Result<std::vector<DetectedSection>> found =
        detect_sections(features.value(), beats.value(), {});
    REQUIRE(found.ok());
    return found.value();
}

std::string summary_of(const std::vector<DetectedSection> &sections, double downbeat_s) {
    std::string out;
    for (const DetectedSection &s : sections) {
        out += s.label + "@" + std::to_string((s.start_s - downbeat_s) / 1.6) + "/" +
               std::to_string(s.energy) + " ";
    }
    return out;
}

} // namespace

TEST_CASE("sections: kick dropouts up to three bars stay in the groove; four make a breakdown") {
    double downbeat_s = 0.0;
    int offset = 0;
    const auto steady = [](int) { return 1.0; };
    // Dropouts of one bar (12) and three bars (18 to 20).
    std::vector<DetectedSection> found = sections_of(
        [](int bar) { return bar >= 4 && bar < 36 && bar != 12 && (bar < 18 || bar > 20); }, steady,
        downbeat_s, offset);
    CAPTURE(summary_of(found, downbeat_s));
    REQUIRE(found.size() == 3);
    CHECK(found[0].label == "intro");
    CHECK(found[1].label == "groove");
    CHECK(found[2].label == "outro");
    // Four bars (18 to 21): a breakdown, and the groove again (too short a break for a drop).
    found = sections_of([](int bar) { return bar >= 4 && bar < 36 && (bar < 18 || bar > 21); },
                        steady, downbeat_s, offset);
    CAPTURE(summary_of(found, downbeat_s));
    REQUIRE(found.size() == 5);
    CHECK(found[2].label == "breakdown");
    CHECK(found[2].start_s == doctest::Approx(downbeat_s + 18 * 1.6).epsilon(0.001));
    CHECK(found[3].label == "groove");
    CHECK(found[3].start_s == doctest::Approx(downbeat_s + 22 * 1.6).epsilon(0.001));
}

TEST_CASE("sections: a groove that steps up splits on the phrase grid, with higher energy") {
    double downbeat_s = 0.0;
    int offset = 0;
    const std::vector<DetectedSection> found =
        sections_of([](int bar) { return bar >= 4 && bar < 36; },
                    [](int bar) { return bar < 20 ? 0.15 : 1.0; }, downbeat_s, offset);
    CAPTURE(summary_of(found, downbeat_s));
    CAPTURE(offset);
    std::vector<const DetectedSection *> grooves;
    for (const DetectedSection &s : found) {
        if (s.label == "groove") {
            grooves.push_back(&s);
        }
    }
    REQUIRE(grooves.size() == 2);
    const double split_bar = (grooves[1]->start_s - downbeat_s) / 1.6;
    CHECK(std::fabs(split_bar - 20.0) <= 4.0);
    const int whole = static_cast<int>(std::lround(split_bar));
    CHECK(((whole - offset) % 8 + 8) % 8 == 0);
    CHECK(grooves[1]->energy > grooves[0]->energy);
}

TEST_CASE("sections: energy levels from the bars' level terciles") {
    std::vector<SectionBar> bars;
    for (int m = 0; m < 30; ++m) {
        SectionBar bar;
        bar.start_s = m;
        bar.level_db = m < 10 ? -40.0 : m < 20 ? -20.0 : -10.0;
        bars.push_back(bar);
    }
    std::vector<DetectedSection> sections = {{0.0, 10.0, "intro", 1.0},
                                             {10.0, 20.0, "groove", 1.0},
                                             {20.0, 30.0, "drop", 1.0},
                                             {30.0, 31.0, "outro", 1.0}}; // No bars: mid.
    for (DetectedSection &s : sections) {
        s.energy = -1;
    }
    assign_energy(sections, bars);
    CHECK(sections[0].energy == 0);
    CHECK(sections[1].energy == 1);
    CHECK(sections[2].energy == 2);
    CHECK(sections[3].energy == 1);
}

TEST_CASE("sections: the riser curve follows a build's flatness, holds, and is 0 elsewhere") {
    constexpr double kFrameS = 0.01;
    ResidualFeatures r;
    for (int i = 0; i < 1000; ++i) { // 10 s.
        const double t = i * kFrameS;
        // A riser from 2 s to 5 s, a gap to 6 s, then tonal again.
        r.flatness.push_back(t < 2.0   ? 0.02f
                             : t < 5.0 ? static_cast<float>(0.02 + 0.28 * (t - 2.0) / 3.0)
                             : t < 6.0 ? 0.05f
                                       : 0.02f);
    }
    const std::vector<DetectedSection> sections = {{0.0, 2.0, "breakdown", 1.0},
                                                   {2.0, 6.0, "build", 1.0},
                                                   {6.0, 8.0, "drop", 1.0},
                                                   {8.0, 9.0, "build", 1.0}, // Flat: a ramp.
                                                   {9.0, 10.0, "groove", 1.0}};
    const std::vector<float> curve = riser_curve(r, sections, kFrameS);
    REQUIRE(curve.size() == 1000);
    CHECK(curve[100] == 0.0f);
    CHECK(curve[300] > 0.1f);
    CHECK(curve[300] < 0.6f);
    for (int i = 201; i < 600; ++i) {
        CHECK(curve[i] >= curve[i - 1]); // Never falls in the build.
    }
    CHECK(curve[540] == doctest::Approx(1.0)); // Held through the gap.
    CHECK(curve[650] == 0.0f);
    CHECK(curve[850] == doctest::Approx(0.5).epsilon(0.05));
    CHECK(curve[899] == doctest::Approx(1.0));
    CHECK(curve[950] == 0.0f);
}
