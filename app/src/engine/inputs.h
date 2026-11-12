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
inline constexpr int kMacroCount = 8;   // Macro knobs (Show tab, MIDI): modulation sources.
inline constexpr int kEventHistory = 4; // Recent hats and bass onsets kept (EngineInputs).
using EventAges = std::array<double, kEventHistory>;
inline constexpr EventAges kNoEvents = {kNever, kNever, kNever, kNever};

// A bass note in the history (EngineInputs::notes): the latest few, newest first.
inline constexpr int kNoteHistory = 8;
struct NoteEvent {
    double age_s = kNever; // Since its onset.
    double length_s = 0.0; // Sounding: the time so far. Ended: its duration. 0: unknown.
    bool sounding = false;
    int midi = 0;
    double cents = 0.0;
    double gate_ratio = 0.0;
    double velocity = 0.0;
    double confidence = 0.0;
    double decay_tau_ms = 0.0;
    bool slide = false;
    bool long_note = false;
};

struct EngineInputs {
    double t_s = 0.0;
    // Grid (constant tempo).
    double beat_phase = 0.0; // 0 to 1 through the current beat.
    long long beat_index = 0;
    int beat_in_bar = 0;     // 0 to 3.
    double bar_phase = 0.0;  // 0 to 1 through the bar.
    long long bar_index = 0; // From the first downbeat; negative before it.
    double slot_phase = 0.0; // 0 to 1 through the 16th.
    double beat_s = 0.0;     // Beat length; 0 without a tempo.
    // Phrase (restarting at sections).
    double phrase_phase = 0.0; // 0 to 1 through the phrase, 0 before the first.
    int phrase_index = -1;
    // Kick.
    double since_kick_s = kNever;
    long long kicks_so_far = 0; // Kicks at or before t: steps a pattern.
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
    // The section's end is known (file mode). Live mode's length is a guess (one phrase), and a
    // section runs on past it, so effects timed to the end (the build's last beat) must not fire.
    bool section_end_known = false;
    int section_energy = 1; // 0 low, 1 mid, 2 high.
    double riser = 0.0;     // 0 to 1: how far the build's riser has risen; 0 outside builds.
    double intensity = 0.5; // 0 to 1: how full the music is (analysis/intensity.h; live: the
                            // residual's activity against the grooves').
    // A run of consecutive build sections taken as one (file mode; a build labelled as two
    // 4-bar sections climbs as one 8-bar build): the section's own values when its neighbours
    // are not builds, 0 outside builds. Live mode copies the section's own.
    double build_progress = 0.0; // 0 to 1 through the run.
    double since_build_s = 0.0;
    double build_length_s = 0.0;
    bool build_end_known = false; // As section_end_known.
    // 0 to 1 through a build, never falling within it; 0 outside builds. File mode: the larger of
    // the riser and the build progress (a quiet last bar holds what the build reached). Live mode,
    // which cannot know the build's length: ramp = 1 - 2^(-since_build_s / phrase), and the
    // larger of the ramp and riser * (0.5 + 0.5 ramp): the riser lifts it to a half fast, the
    // ramp climbs half the remaining way each phrase.
    double tension = 0.0;
    // The last kNoteHistory bass notes, newest first; entry 0 agrees with the bass fields above.
    // Live mode knows the latest note and only the onset times of three before it (length 0).
    std::array<NoteEvent, kNoteHistory> notes{};
    // Residual and hats.
    double residual_level = 0.0; // 0 to 1 (-90 to 0 dB).
    double residual_centroid_hz = 0.0;
    double residual_flatness = 0.0;
    double since_hat_s = kNever;
    long long hats_so_far = 0;
    // The ages of the last kEventHistory hats and bass onsets, newest first (kNever when fewer),
    // so an effect can draw overlapping repeats (an echo's hats) rather than only the latest.
    EventAges hat_ages_s = kNoEvents;
    EventAges bass_onset_ages_s = kNoEvents;
    // The operator's macro knobs, 0 to 1: set by the show renderer from its config, not by
    // inputs_at (they are not music).
    std::array<double, kMacroCount> macros{};
};

// Sections and phrase starts in place of the analysis's (sections edited by hand, from the
// labels); null: the analysis's own.
struct SectionTimeline {
    std::vector<DetectedSection> sections;
    std::vector<double> phrase_starts_s;
    std::vector<float> riser;          // For these sections' builds (riser_curve).
    std::vector<DetectedEvent> events; // The labels' breaks and fills, in time order.
};

// The breaks and fills the engine reads: the timeline's when one is given (labelled), else the
// analysis's detected ones only when detected_events (AutomationParams::detected_events), else
// none.
const std::vector<DetectedEvent> &
engine_events(const TrackAnalysis &analysis, const SectionTimeline *timeline, bool detected_events);

EngineInputs inputs_at(const TrackAnalysis &analysis, const Grid &grid, double t_s,
                       const SectionTimeline *timeline = nullptr, bool detected_events = false);

// The timeline for sections edited by hand: them (with their energy from the analysis's bars),
// the phrases restarting at each (phrases of the default length from the analysis's phrase
// offset, on `grid`), the riser curve through their builds, and the labelled events.
SectionTimeline section_timeline(const TrackAnalysis &analysis, const Grid &grid,
                                 std::vector<DetectedSection> sections,
                                 std::vector<DetectedEvent> events = {});

// Live mode (SPEC 4.3): from the latest causal state, at render time t_s on the capture clock.
// The beat, bar, phrase and slot come from the loop's prediction, so they carry no analysis
// latency. Once locked with kicks on recent beats, the kick is predicted too (the last predicted
// beat at or before t_s); otherwise it is the last detected one. Bass, hats and sections arrive
// state.latency_s late. Unknown live: a sounding note's end (its phase uses the recent note
// length) and a section's length (taken as one phrase).
EngineInputs inputs_at(const LiveState &state, double t_s);

} // namespace psyight
