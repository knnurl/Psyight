// Bass note tracker v0, DSP (SPEC section 6.3), file mode. Runs on the front-end features of a
// whole track and returns one record per bass note.
//
// Steps:
// 1. Kicks: peaks of the click-band flux that come with a rise of the low-band (sub plus bass)
//    energy and a click that is broadband (strong against the low-mid flux), near a beat of the
//    grid, with another candidate one beat away (kicks come in runs). Each kick opens a mask in
//    which bass onsets are down-weighted and energy drops do not end notes (sidechain ducking).
// 2. Onsets: adaptive-threshold peak picking on spectral flux of the bass region (its own STFT,
//    40 Hz to 2 kHz by default, so the per-note filter opening shows), normalised to the track.
//    A peak must also have bass activity above a floor relative to the track's bass level.
// 3. Offsets: the first of the bass envelope falling below k x the note's peak (outside kick
//    masks), the next onset, or the next kick. Ending at a kick is a v0 simplification: a long note
//    held under a kick is split there.
// Low bass needs windows of at least one period (24 ms at E1): shorter ones see the waveform, not
// the envelope. The envelope is the bass band's power averaged over envelope_window_ms, and the
// onset STFT is 2048 samples (43 ms).
// 4. Pitch: the front end's harmonic-sum salience (the bass roll's pitch background), averaged
//    over the central part of the note; cents by parabolic interpolation across semitone rows.
// 5. Slides: a note whose halves differ in salience pitch (a pitch change without a re-attack)
//    is split where the second pitch overtakes the first; the parts are marked slide_out and
//    slide_in.
// 6. Derived: gate ratio and slot index from the grid, long-note flag, velocity, decay time
//    constant, confidence.
//
// Assumptions: mono 48 kHz analysis; the front end's default bands (sub, bass, low mid, click);
// kicks have a click transient; the bass is monophonic. Every output is a pure function of the
// features, the grid and the parameters (deterministic).
// Latency: non-causal (file mode); uses the whole track.
#pragma once

#include "analysis/front_end.h"
#include "core/params.h"
#include "core/result.h"
#include "core/timebase.h"
#include "io/labels.h"

#include <span>
#include <vector>

namespace psyight {

struct BassTrackerParams {
    // Kicks.
    double kick_threshold = 0.4; // Click flux peak, as a fraction of the track's p99.
    // Sub plus bass level rise across the kick. Sidechained mixes keep the low band nearly level
    // (Phase Change: 2.3 dB median at beats), so this only rejects flat-or-falling candidates; hats
    // fail the grid test and bass attacks the click ratio.
    double kick_min_low_rise_db = 1.0;
    // Click-band flux over low-mid flux at the peak: a kick's click is broadband, a bass attack's
    // brightness falls off above its filter (synthetic: 0.03 to 0.07). Real kicks vary: median
    // 0.63 at the beats of OculusDrift, 0.19 at those of Hall of Purity (distorted kicks).
    double kick_min_click_ratio = 0.1;
    double kick_min_gap_ms = 200.0;
    // M1 uses the manual grid as a beat prior: a synth's attack click can pass the click and rise
    // tests, but not off the beat. 0 disables. M2 replaces the grid with beat tracking.
    double kick_grid_tolerance_ms = 35.0;
    double kick_min_run_beats = 2.0; // 2: another kick candidate must lie one beat away.
    double kick_bias_ms = 0.0;       // Subtracted from detected kick times.
    // The mask also starts this long before the kick, on top of the reach of the onset function
    // (half its STFT window plus half the envelope window): its flux sees the kick early.
    double kick_mask_pre_ms = 15.0;
    double kick_mask_ms = 60.0;
    // Onsets.
    double onset_window_samples = 2048; // Power of two.
    double onset_low_hz = 40.0;
    double onset_high_hz = 2000.0;
    double envelope_window_ms = 25.0; // Bass power is averaged over this, centred.
    double onset_delta = 0.04;        // Above the local mean, as a fraction of the track's p99.
    double onset_floor = 0.05;        // Absolute minimum, as a fraction of the track's p99.
    double onset_mean_window_ms = 100.0;
    double onset_min_level_change_db = -12.0; // Level after minus before; rejects offset clicks.
    double onset_min_gap_ms = 45.0;
    double kick_mask_onset_weight = 0.0; // Onset function scale inside kick masks.
    double activity_floor_db = -30.0;    // Bass level relative to the track's p95 bass level.
    double onset_bias_ms = 0.0;          // Subtracted from detected onset times.
    // Offsets.
    double offset_ratio = 0.25; // k: the note ends when its amplitude falls below k x its peak.
    double peak_search_ms = 30.0;
    double offset_bias_ms = 0.0; // Subtracted from offsets, after the envelope window's lag.
    // Pitch and slides.
    double pitch_centre_fraction = 0.5; // Central part of the note used for its pitch.
    double slide_min_part_ms = 60.0;    // Each pitch of a slide must hold this long.
    double slide_bias_ms = 20.0;        // Split lands mid-glide; about half a glide time.
    // Derived.
    double long_note_slots = 1.5;
    double velocity_window_s = 2.0;    // Velocity is relative to the loudest note within this.
    double confidence_margin_db = 6.0; // Pitch salience margin giving full pitch confidence.
};

// SPEC 6.3 note record.
struct BassNote {
    double onset_s = 0.0;
    double offset_s = 0.0;
    double duration_ms = 0.0;
    double slot_len_ms = 0.0;
    double gate_ratio = 0.0;
    int pitch_midi = 0;
    double cents = 0.0;
    int octave = 0;          // Scientific pitch notation: MIDI 24 is C1.
    double decay_tau_ms = 0; // 0 when the note is too short to fit or does not decay.
    bool slide_in = false;
    bool slide_out = false;
    bool is_long = false;
    double velocity = 0.0; // Peak amplitude relative to the loudest note nearby, 0 to 1.
    double confidence = 0.0;
    int slot_index = 0; // Slot in bar, 0 to 15, of the nearest grid slot.
};

struct BassTrackResult {
    std::vector<double> kick_times_s;
    std::vector<BassNote> notes;
    std::vector<float> onset_function; // Per frame, normalised; for plots and tests.
};

// mono is the audio the features were computed from.
Result<BassTrackResult> track_bass(std::span<const float> mono, const FrontEndResult &features,
                                   const Grid &grid, const BassTrackerParams &params);

// For scoring against labels.
std::vector<BassNoteLabel> to_labels(const std::vector<BassNote> &notes);

// Registry declarations ("bass.*") with the defaults above, and the reverse mapping.
Result<void> declare_bass_tracker_params(ParamRegistry &registry);
BassTrackerParams bass_tracker_params_from(const ParamValues &values);

} // namespace psyight
