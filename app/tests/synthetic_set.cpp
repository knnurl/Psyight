#include "synthetic_set.h"

namespace psyight::synth {

namespace {

constexpr int kBars = 8;
constexpr int kF1 = 29;
constexpr int kE1 = 28;

SyntheticCase pattern_case(std::string name, Pattern pattern, double gate_ratio,
                           SynthParams params = {}, int root_midi = kF1) {
    params.bars = kBars;
    return {std::move(name), params, make_pattern(pattern, kBars, root_midi, gate_ratio)};
}

// Rolling positions with the pitch changing every beat.
std::vector<BassEvent> melodic(double gate_ratio) {
    constexpr int kPitches[] = {29, 32, 36, 27}; // F1, G#1, C2, D#1.
    std::vector<BassEvent> events;
    for (int beat = 0; beat < kBars * 4; ++beat) {
        for (int slot = 1; slot < 4; ++slot) {
            events.push_back({beat * 4.0 + slot, gate_ratio, kPitches[beat % 4], 1.0, false});
        }
    }
    return events;
}

// Per bar: a slide up, a long note, a short octave jump and a slide down.
std::vector<BassEvent> slides_and_long_notes() {
    std::vector<BassEvent> events;
    for (int bar = 0; bar < kBars; ++bar) {
        const double b = bar * 16.0;
        events.push_back({b + 1, 1.0, 29, 1.0, false});
        events.push_back({b + 2, 1.6, 33, 1.0, true});
        events.push_back({b + 5, 2.7, 29, 1.0, false});
        events.push_back({b + 9, 0.6, 29, 1.0, false});
        events.push_back({b + 10, 0.6, 29, 1.0, false});
        events.push_back({b + 11, 0.6, 41, 1.0, false});
        events.push_back({b + 13, 1.0, 36, 1.0, false});
        events.push_back({b + 14, 1.8, 31, 1.0, true});
    }
    return events;
}

} // namespace

std::vector<SyntheticCase> synthetic_set() {
    std::vector<SyntheticCase> set;
    set.push_back(pattern_case("rolling, gate 0.6", Pattern::rolling, 0.6));
    set.push_back(pattern_case("rolling, gate 1.0", Pattern::rolling, 1.0));
    {
        SynthParams p; // Like the owner's reference minute: 150 BPM, E1, gate 1.0, ducked.
        p.bpm = 150.0;
        p.sidechain_depth = 0.5;
        set.push_back(pattern_case("rolling E1 150 BPM, gate 1.0, sidechain 0.5", Pattern::rolling,
                                   1.0, p, kE1));
    }
    set.push_back(pattern_case("ghost note, gate 0.8", Pattern::rolling_ghost, 0.8));
    set.push_back(pattern_case("octave jump, gate 0.6", Pattern::rolling_octave_jump, 0.6));
    set.push_back(pattern_case("triplet gallop, gate 0.7", Pattern::triplet_gallop, 0.7));
    set.push_back(pattern_case("offbeat double, gate 0.5", Pattern::offbeat_double, 0.5));
    set.push_back(pattern_case("offbeat, gate 0.3", Pattern::offbeat, 0.3));
    {
        SynthParams p;
        p.sidechain_depth = 0.8;
        set.push_back(pattern_case("rolling, gate 0.9, sidechain 0.8", Pattern::rolling, 0.9, p));
    }
    {
        SynthParams p;
        p.hats = true;
        p.noise_level = 0.01;
        set.push_back(pattern_case("rolling, gate 0.6, hats and noise", Pattern::rolling, 0.6, p));
    }
    {
        SynthParams p;
        p.bass_decay_tau_ms = 60.0;
        set.push_back(pattern_case("rolling, gate 0.8, decay 60 ms", Pattern::rolling, 0.8, p));
    }
    {
        SynthParams p;
        p.bars = kBars;
        set.push_back({"melodic, gate 0.7", p, melodic(0.7)});
    }
    {
        SynthParams p;
        p.bars = kBars;
        set.push_back({"slides and long notes", p, slides_and_long_notes()});
    }
    return set;
}

} // namespace psyight::synth
