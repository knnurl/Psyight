// Live analysis (analysis/live_analysis.h): streaming features against the batch ones, block-size
// invariance of the whole analyser (fixture weights), and the beat loop and section rules driven
// by exact kick times.

#include "analysis/live_analysis.h"
#include "audio/live_input.h"
#include "engine/inputs.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <thread>
#include <vector>

using namespace psyight;

namespace {

constexpr int kRate = 48000;

std::vector<float> test_signal(int samples) {
    std::vector<float> out(static_cast<size_t>(samples));
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> noise(-0.05f, 0.05f);
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double kick = std::fmod(t, 0.4) < 0.08 ? std::sin(2 * 3.14159265 * 55 * t) : 0.0;
        out[static_cast<size_t>(i)] = static_cast<float>(
            0.4 * std::sin(2 * 3.14159265 * 43.65 * t) + 0.5 * kick + noise(rng));
    }
    return out;
}

// Drives a LiveBeat with kicks, updating sections every 5 ms as the analyser would.
struct BeatRig {
    LiveParams params;
    LiveBeat beat;
    LiveState state;
    double now_s = 0.0;

    BeatRig() {
        beat.prepare(params);
        state.phrase_bars = params.phrase_bars;
    }
    void advance_to(double t_s) {
        for (; now_s + 0.005 <= t_s; now_s += 0.005) {
            beat.update(state, now_s);
        }
    }
    void kick(double t_s) {
        advance_to(t_s);
        ++state.kicks;
        state.last_kick_s = t_s;
        beat.on_kick(state, t_s);
    }
    // Predicted beat phase error at t_s against beats at first_s + k * period_s, in seconds.
    double phase_error_s(double t_s, double first_s, double period_s) const {
        const EngineInputs in = inputs_at(state, t_s);
        const double live = static_cast<double>(in.beat_index) + in.beat_phase;
        const double truth = (t_s - first_s) / period_s;
        return std::remainder(live - truth, 1.0) * period_s;
    }
};

} // namespace

TEST_CASE("live: streaming student features equal the batch features") {
    const std::vector<float> signal = test_signal(kRate * 2 + 100);
    const Result<StudentFeatures> batch = student_features(signal, kRate);
    REQUIRE(batch.ok());
    StudentFeatureStream stream;
    REQUIRE(stream.prepare(kRate).ok());
    const int hop = stream.params().hop_samples;
    std::vector<float> frame(static_cast<size_t>(stream.dims()));
    int different = 0;
    for (int k = 0; k < batch.value().frames; ++k) {
        if (k > 0) {
            stream.push(std::span(signal).subspan(static_cast<size_t>((k - 1) * hop), hop));
        }
        stream.frame(frame);
        different +=
            std::memcmp(frame.data(),
                        batch.value().values.data() + static_cast<size_t>(k) * stream.dims(),
                        frame.size() * sizeof(float)) != 0;
    }
    CHECK(different == 0);
}

TEST_CASE("live: the analysis does not depend on the block size") {
    const Result<StudentModel> model =
        load_student(std::filesystem::path(PSYIGHT_TESTS_DIR) / "data" / "student_fixture");
    REQUIRE(model.ok());
    const std::vector<float> signal = test_signal(kRate * 3);
    std::vector<std::vector<BassNote>> notes;
    std::vector<std::vector<double>> kicks;
    std::vector<LiveState> ends;
    for (const size_t block : {size_t{1}, size_t{480}, size_t{4096}}) {
        LiveAnalyser live;
        REQUIRE(live.prepare(kRate, model.value()).ok());
        live.set_recording(true);
        for (size_t at = 0; at < signal.size(); at += block) {
            live.process(std::span(signal).subspan(at, std::min(block, signal.size() - at)));
        }
        notes.push_back(live.recorded_notes());
        kicks.push_back(live.recorded_kicks());
        ends.push_back(live.state());
    }
    // Input frames 0 to 562 (one per full hop, plus frame 0), less the 2-frame lookahead.
    CHECK(ends[0].frames == kRate * 3 / 256 + 1 - 2);
    for (size_t i = 1; i < ends.size(); ++i) {
        CHECK(kicks[i] == kicks[0]);
        REQUIRE(notes[i].size() == notes[0].size());
        for (size_t n = 0; n < notes[0].size(); ++n) {
            CHECK(notes[i][n].onset_s == notes[0][n].onset_s);
            CHECK(notes[i][n].offset_s == notes[0][n].offset_s);
            CHECK(notes[i][n].pitch_midi == notes[0][n].pitch_midi);
        }
        CHECK(ends[i].frames == ends[0].frames);
        CHECK(ends[i].residual_level == ends[0].residual_level);
        CHECK(ends[i].residual_centroid_hz == ends[0].residual_centroid_hz);
        CHECK(ends[i].bass_notes == ends[0].bass_notes);
    }
}

TEST_CASE("live beat: acquires, locks and predicts a steady kick with jitter") {
    BeatRig rig;
    const double period = 0.4; // 150 BPM.
    std::mt19937 rng(9);
    std::uniform_real_distribution<double> jitter(-0.003, 0.003);
    int locked_after = -1;
    for (int k = 0; k < 64; ++k) {
        rig.kick(1.0 + k * period + jitter(rng));
        if (rig.state.locked && locked_after < 0) {
            locked_after = k + 1;
        }
    }
    CHECK(rig.state.tempo_known);
    REQUIRE(rig.state.locked);
    CHECK(locked_after <= 16);
    CHECK(60.0 / rig.state.period_s == doctest::Approx(150.0).epsilon(0.002));
    for (const double t : {26.5, 26.62, 26.9}) {
        CHECK(std::abs(rig.phase_error_s(t, 1.0, period)) < 0.004);
    }
    // Locked with kicks on every beat: the kick is predicted, not waited for.
    const EngineInputs in = inputs_at(rig.state, 1.0 + 64 * period + 0.01);
    CHECK(in.since_kick_s == doctest::Approx(0.01).epsilon(0.5));
    CHECK(rig.state.section == LiveSection::groove);
}

TEST_CASE("live beat: follows a tempo ramp and ignores off-beat hits") {
    BeatRig rig;
    double t = 1.0;
    double period = 0.4;
    for (int k = 0; k < 160; ++k) {
        if (k >= 32 && k < 96) {
            period = 60.0 / (150.0 + 2.0 * (k - 32) / 64.0); // 150 to 152 BPM over 64 beats.
        }
        rig.kick(t);
        if (k % 8 == 3) {
            rig.kick(t + 0.5 * period); // An off-beat percussion hit.
        }
        t += period;
    }
    CHECK(rig.state.locked);
    CHECK(60.0 / rig.state.period_s == doctest::Approx(152.0).epsilon(0.002));
    CHECK(std::abs(rig.phase_error_s(t - 0.1, t, period)) < 0.005);
}

TEST_CASE("live beat: a new track at another phase is reacquired") {
    BeatRig rig;
    double t = 1.0;
    for (int k = 0; k < 40; ++k, t += 0.4) {
        rig.kick(t);
    }
    REQUIRE(rig.state.locked);
    const double new_first = t + 0.2; // Half a beat later, and 152 BPM.
    const double new_period = 60.0 / 152.0;
    int relocked_after = -1;
    for (int k = 0; k < 40; ++k) {
        rig.kick(new_first + k * new_period);
        if (k > 4 && rig.state.locked && relocked_after < 0) {
            relocked_after = k + 1;
        }
    }
    CHECK(relocked_after > 0);
    CHECK(relocked_after <= 24);
    CHECK(std::abs(rig.phase_error_s(new_first + 40 * new_period, new_first, new_period)) < 0.005);
}

TEST_CASE("live beat: breakdowns, drops, downbeats and the run rule") {
    BeatRig rig;
    const double period = 0.4;
    const auto beat_s = [&](int b) { return 1.0 + b * period; };
    for (int b = 0; b < 32; ++b) {
        rig.kick(beat_s(b));
    }
    REQUIRE(rig.state.locked);
    // A one-bar fill: the kick misses beats 32 to 35 and returns on 36, a downbeat.
    for (int b = 36; b < 64; ++b) {
        rig.kick(beat_s(b));
    }
    CHECK(rig.state.section == LiveSection::groove);
    EngineInputs in = inputs_at(rig.state, beat_s(40) + 0.01);
    CHECK(in.beat_in_bar == 0);
    CHECK(in.bar_phase < 0.01);
    // Nine bars without a kick run: a breakdown from beat 64; a stray kick on beat 80 does not end
    // it; the kick returning on beat 100 is a drop, for 8 bars, then a groove.
    rig.advance_to(beat_s(73));
    CHECK(rig.state.section == LiveSection::breakdown);
    CHECK(rig.state.section_start_s == doctest::Approx(beat_s(64)).epsilon(1e-3));
    rig.kick(beat_s(80));
    rig.advance_to(beat_s(99));
    CHECK(rig.state.section == LiveSection::breakdown);
    for (int b = 100; b < 140; ++b) {
        rig.kick(beat_s(b));
        if (b == 103) { // The run of 4 that returns on beat 100 is complete.
            CHECK(rig.state.section == LiveSection::drop);
            CHECK(rig.state.section_start_s == doctest::Approx(beat_s(100)).epsilon(1e-3));
            in = inputs_at(rig.state, beat_s(103) + 0.01);
            CHECK(in.bar_index == 0);
            CHECK(in.beat_in_bar == 3);
            CHECK(in.phrase_index == 0);
            CHECK(in.section == "drop");
        }
    }
    CHECK(rig.state.section == LiveSection::groove);
    CHECK(rig.state.section_start_s == doctest::Approx(beat_s(132)).epsilon(1e-3));
}

TEST_CASE("live beat: a flatness riser in a breakdown is a build; flat texture is not") {
    const double period = 0.4;
    const auto beat_s = [&](int b) { return 1.0 + b * period; };
    const auto breakdown_with = [&](auto flatness_at) {
        BeatRig rig;
        for (int b = 0; b < 32; ++b) {
            rig.kick(beat_s(b));
        }
        REQUIRE(rig.state.locked);
        for (int b = 32; b < 72; ++b) { // Ten bars without a kick.
            rig.state.residual_flatness = flatness_at(b);
            rig.advance_to(beat_s(b + 1));
        }
        return rig.state.section;
    };
    // Tonal for six bars, then a riser climbing to 0.15 over the last four.
    CHECK(breakdown_with([](int b) { return b < 56 ? 0.01 : 0.01 + 0.14 * (b - 56) / 16.0; }) ==
          LiveSection::build);
    // Noisy from the start and fading: texture, not a riser.
    CHECK(breakdown_with([](int b) { return 0.12 - 0.1 * (b - 32) / 40.0; }) ==
          LiveSection::breakdown);
}

TEST_CASE("live inputs: bass, hats and residual from the state") {
    LiveState s;
    s.bass_notes = 3;
    s.bass_active = true;
    s.bass_midi = 28;
    s.bass_onset_s = 10.0;
    s.bass_typical_s = 0.08;
    s.last_hat_s = 10.01;
    s.hats = 7;
    s.residual_level = 0.4;
    EngineInputs in = inputs_at(s, 10.04);
    CHECK(in.bass_active);
    CHECK(in.bass_midi == 28);
    CHECK(in.bass_note_phase == doctest::Approx(0.5));
    CHECK(in.since_hat_s == doctest::Approx(0.03));
    CHECK(in.hats_so_far == 7);
    CHECK(in.residual_level == doctest::Approx(0.4));
    s.bass_active = false;
    s.bass_end_s = 10.06;
    in = inputs_at(s, 10.1);
    CHECK_FALSE(in.bass_active);
    CHECK(in.since_bass_offset_s == doctest::Approx(0.04));
    CHECK(in.since_kick_s == kNever); // No kick yet.
}

TEST_CASE("live input: capture and analysis threads run at real-time pace (null backend)") {
    const Result<StudentModel> loaded =
        load_student(std::filesystem::path(PSYIGHT_TESTS_DIR) / "data" / "student_fixture");
    REQUIRE(loaded.ok());
    LiveInput live(std::make_shared<const StudentModel>(loaded.value()));
    CaptureOptions options;
    options.null_backend = true;
    REQUIRE(live.start(options).ok());
    const auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const double elapsed_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double clock_s = static_cast<double>(live.now()) / live.sample_rate_hz();
    const LiveState state = live.state();
    MESSAGE("live clock " << clock_s << " s after " << elapsed_s << " s; analysed to "
                          << state.analysed_s << " s; delay " << live.analysis_delay_ms()
                          << " ms; load " << 100.0 * live.load() << "%");
    CHECK(clock_s == doctest::Approx(elapsed_s).epsilon(0.25));
    CHECK(state.frames > 50);
    CHECK(clock_s - state.analysed_s < 0.1); // The analysis keeps up.
    CHECK(live.dropped() == 0);
    CHECK_FALSE(state.tempo_known); // Silence: no kicks.
    live.stop();
    CHECK_FALSE(live.running());
}

TEST_CASE("live beat: the operator sets the downbeat and nudges the beat") {
    BeatRig rig;
    for (int b = 0; b < 32; ++b) {
        rig.kick(1.0 + b * 0.4);
    }
    REQUIRE(rig.state.locked);
    // Pressed 60 ms late on beat 41 (a human on the "one"): beat 41 becomes a downbeat.
    LiveBeat::set_downbeat(rig.state, 1.0 + 41 * 0.4 + 0.06);
    EngineInputs in = inputs_at(rig.state, 1.0 + 41 * 0.4 + 0.01);
    CHECK(in.beat_in_bar == 0);
    // Live beat numbers count from the kick where the tempo was acquired (the 9th).
    CHECK(rig.state.downbeat_beat == std::llround(LiveBeat::position(rig.state, 1.0 + 41 * 0.4)));
    CHECK_FALSE(rig.state.downbeat_from_novelty);
    const double before = inputs_at(rig.state, 20.0).beat_phase;
    LiveBeat::nudge(rig.state, 0.010);
    const double after = inputs_at(rig.state, 20.0).beat_phase;
    CHECK(before - after == doctest::Approx(0.010 / 0.4).epsilon(0.01)); // Beats come later.
    rig.beat.relock(rig.state);
    CHECK_FALSE(rig.state.locked);
    CHECK(rig.state.tempo_known); // The beat keeps running while it measures again.
}

TEST_CASE("live beat: a double trigger on one beat does not break a run") {
    BeatRig rig;
    for (int b = 0; b < 40; ++b) {
        rig.kick(1.0 + b * 0.4);
        if (b > 12) {
            rig.kick(1.0 + b * 0.4 + 0.03); // A second detection 30 ms later, same beat.
        }
    }
    CHECK(rig.state.locked);
    CHECK(rig.state.kick_streak >= 20);
    CHECK(rig.state.section == LiveSection::groove);
    CHECK(60.0 / rig.state.period_s == doctest::Approx(150.0).epsilon(0.002));
}

TEST_CASE("live beat: a run returning soon after a declared breakdown ends it") {
    BeatRig rig;
    const auto beat_s = [](int b) { return 1.0 + b * 0.4; };
    for (int b = 0; b < 24; ++b) {
        rig.kick(beat_s(b));
    }
    // No kick on beats 24 to 31, so a breakdown is declared 2 bars after beat 23; the run that
    // completes on beat 35 started on beat 32, 8 beats after the last run (not more).
    for (int b = 32; b < 40; ++b) {
        rig.kick(beat_s(b));
        if (b == 31 + 1) {
            CHECK(rig.state.section == LiveSection::breakdown);
        }
    }
    CHECK(rig.state.section == LiveSection::groove);
    CHECK(rig.state.section_start_s == doctest::Approx(beat_s(32)).epsilon(1e-3));
}

TEST_CASE("live beat: the riser rises through a build, never falls, and ends with it; energy") {
    const double period = 0.4;
    const auto beat_s = [&](int b) { return 1.0 + b * period; };
    BeatRig rig;
    for (int b = 0; b < 32; ++b) {
        rig.kick(beat_s(b));
    }
    REQUIRE(rig.state.locked);
    CHECK(rig.state.section_energy == 1);
    double last = 0.0;
    bool rose = false;
    for (int b = 32; b < 72; ++b) { // Ten bars without a kick; a riser over the last four.
        rig.state.residual_flatness = b < 56 ? 0.01 : 0.01 + 0.3 * (b - 56) / 16.0;
        rig.advance_to(beat_s(b + 1));
        if (rig.state.section == LiveSection::groove) {
            CHECK(b < 40); // The breakdown is declared after two bars.
        } else if (rig.state.section == LiveSection::breakdown) {
            CHECK(rig.state.riser == 0.0);
            CHECK(rig.state.section_energy == 0);
        } else {
            REQUIRE(rig.state.section == LiveSection::build);
            CHECK(rig.state.riser >= last);
            CHECK(rig.state.section_energy == 1);
            rose = rose || rig.state.riser > 0.5;
            last = rig.state.riser;
        }
    }
    CHECK(rose);
    CHECK(inputs_at(rig.state, beat_s(72)).riser == doctest::Approx(rig.state.riser));
    rig.state.residual_flatness = 0.01;
    for (int b = 72; b < 80; ++b) {
        rig.kick(beat_s(b));
    }
    CHECK(rig.state.section == LiveSection::drop);
    CHECK(rig.state.riser == 0.0);
    CHECK(rig.state.section_energy == 2);
}
