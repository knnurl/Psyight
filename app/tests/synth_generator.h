// Synthetic psytrance test signal with exact ground truth (SPEC section 6.11).
//
// Renders a kick and a monophonic low-passed saw bass (plus optional hats, noise and sidechain
// ducking) and emits labels in the schema of SPEC 6.10. Every event is placed on an integer sample
// index and its label time is that index divided by the sample rate, so the labels are exact.
//
// Ground-truth definitions:
// - Bass onset: the sample where the note's 1 ms attack starts.
// - Bass offset: the sample where its gate closes; a release_ms linear fade follows. Overlapping
//   notes are monophonic: a note's offset is clipped to the next onset.
// - Slide: a note with slide_in glides from the previous pitch without re-attacking. The previous
//   note's gate stays open until the slide note's onset and is labelled slide_out.
// - Sidechain ducking changes the bass level only; it never ends a note. It is keyed from the beat
//   grid, not the rendered kick, so it also works with the kick off (isolated bass tests).
// - Velocity is the attack's scale before ducking. A slide keeps the previous attack, so its own
//   velocity field is ignored and its label carries the previous note's velocity.
// - An event that a coincident event leaves with no length is dropped.
// Rendering is a pure function of the parameters and events: same input, bit-identical output.
#pragma once

#include "io/labels.h"

#include <cstdint>
#include <vector>

namespace psyight::synth {

// One bass note in musical time. Positions are in 16th-note slots from the first downbeat.
struct BassEvent {
    double start_slots = 0.0;  // May be fractional, for example for triplets.
    double length_slots = 0.6; // Gate length. Above 1.5 slots it is a long note.
    int midi = 29;
    double velocity = 1.0; // Peak amplitude scale, 0 to 1.
    bool slide_in = false;
};

struct SynthParams {
    int sample_rate_hz = 48000;
    double bpm = 160.0;
    int bars = 4;
    double lead_in_s = 0.5; // Silence before the first downbeat.
    double tail_s = 0.5;    // Silence after the last bar.
    std::uint64_t seed = 1; // Drives the noise sources (click, hats, background noise).

    bool kick = true;
    double kick_level = 0.8;
    bool kick_click = true;

    double bass_level = 0.5;
    double bass_decay_tau_ms = 200.0; // Amplitude decay while the gate is open.
    double bass_release_ms = 2.0;
    double filter_start_hz = 2000.0; // Low-pass cutoff envelope, restarted by each attack.
    double filter_end_hz = 300.0;
    double filter_tau_ms = 40.0;
    double slide_time_ms = 40.0;

    double sidechain_depth = 0.0; // Bass gain reduction at each kick, 0 to 1.
    double sidechain_release_ms = 60.0;

    bool hats = false; // Offbeat 8ths, noise high-passed at 8 kHz.
    double hat_level = 0.1;
    double noise_level = 0.0; // Continuous white noise.
};

// The six bass patterns from the browser prototype. K is the kick, B a bass note, - a rest.
enum class Pattern {
    rolling,             // K B B B
    rolling_ghost,       // K B b B, the second B at 0.4 velocity (as in the prototype)
    rolling_octave_jump, // K B B B, last B an octave up
    triplet_gallop,      // K B B on a triplet grid
    offbeat_double,      // K - B B
    offbeat,             // K - B -
};

// gate_ratio is relative to the pattern's step: a 16th, or a triplet 8th (4/3 slots) for the
// gallop.
std::vector<BassEvent> make_pattern(Pattern pattern, int bars, int root_midi, double gate_ratio);

struct SynthOutput {
    std::vector<float> samples; // Mono.
    Labels labels;
};

SynthOutput render(const SynthParams &params, const std::vector<BassEvent> &events);

} // namespace psyight::synth
