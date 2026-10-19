// Engine inputs (SPEC sections 7.2 and 7.4 sources): what the analysis says about the music at a
// render time, on every timescale from 16th to section. File mode reads the whole-track analysis,
// so events at the render time are known exactly (zero perceived analysis latency, SPEC 4.3).
// A pure function of the analysis, the grid and the time.
#pragma once

#include "analysis/live_analysis.h"
#include "analysis/track_analysis.h"
#include "core/timebase.h"

#include <array>
#include <limits>
#include <string>
#include <vector>

namespace psyight {

inline constexpr double kNever = std::numeric_limits<double>::infinity();
inline constexpr int kMacroCount = 8; // Macro knobs (Show tab, MIDI): modulation sources.

struct EngineInputs {
    double t_s = 0.0;
    // Grid (constant tempo).
    double beat_phase = 0.0; // 0 to 1 through the current beat.
    long long beat_index = 0;
    int beat_in_bar = 0;     // 0 to 3.
    double bar_phase = 0.0;  // 0 to 1 through the bar.
    long long bar_index = 0; // From the first downbeat; negative before it.
    double slot_phase = 0.0; // 0 to 1 through the 16th.
    // Phrase (restarting at sections).
    double phrase_phase = 0.0; // 0 to 1 through the phrase, 0 before the first.
    int phrase_index = -1;
    // Kick.
    double since_kick_s = kNever;
    // Bass.
    bool bass_active = false; // A detected note is sounding.
    int bass_midi = 0;        // Of the sounding note, else of the last one.
    double bass_cents = 0.0;
    double bass_note_phase = 0.0; // 0 to 1 through the sounding note.
    double bass_gate_ratio = 0.0;
    double bass_decay_tau_ms = 0.0;
    double bass_velocity = 0.0;
    double bass_confidence = 0.0;
    double since_bass_onset_s = kNever;
    double since_bass_offset_s = 0.0; // After the last note ended; 0 while one sounds.
    long long bass_notes_so_far = 0;  // Onsets at or before t: steps a conveyor.
    bool bass_slide = false;          // The sounding note slides in.
    // Section.
    std::string section = "intro";
    double section_progress = 0.0; // 0 to 1 through the section.
    int section_index = -1;        // Into TrackAnalysis::sections; -1 outside them.
    double since_section_s = 0.0;  // Since the section started.
    double section_length_s = 0.0;
    int section_energy = 1; // 0 low, 1 mid, 2 high.
    double riser = 0.0;     // 0 to 1: how far the build's riser has risen; 0 outside builds.
    // Residual and hats.
    double residual_level = 0.0; // 0 to 1 (-90 to 0 dB).
    double residual_centroid_hz = 0.0;
    double residual_flatness = 0.0;
    double since_hat_s = kNever;
    long long hats_so_far = 0;
    // The operator's macro knobs, 0 to 1: set by the show renderer from its config, not by
    // inputs_at (they are not music).
    std::array<double, kMacroCount> macros{};
};

// Sections and phrase starts in place of the analysis's (sections edited by hand, from the
// labels); null: the analysis's own.
struct SectionTimeline {
    std::vector<DetectedSection> sections;
    std::vector<double> phrase_starts_s;
    std::vector<float> riser; // For these sections' builds (riser_curve).
};

EngineInputs inputs_at(const TrackAnalysis &analysis, const Grid &grid, double t_s,
                       const SectionTimeline *timeline = nullptr);

// The timeline for sections edited by hand: them (with their energy from the analysis's bars),
// the phrases restarting at each (phrases of the default length from the analysis's phrase
// offset, on `grid`), and the riser curve through their builds.
SectionTimeline section_timeline(const TrackAnalysis &analysis, const Grid &grid,
                                 std::vector<DetectedSection> sections);

// Live mode (SPEC 4.3): from the latest causal state, at render time t_s on the capture clock.
// The beat, bar, phrase and slot come from the loop's prediction, so they carry no analysis
// latency. Once locked with kicks on recent beats, the kick is predicted too (the last predicted
// beat at or before t_s); otherwise it is the last detected one. Bass, hats and sections arrive
// state.latency_s late. Unknown live: a sounding note's end (its phase uses the recent note
// length) and a section's length (taken as one phrase).
EngineInputs inputs_at(const LiveState &state, double t_s);

} // namespace psyight
