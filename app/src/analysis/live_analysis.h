// Live-mode analysis (SPEC sections 4.3, 6.5 and 6.8, M5): strictly causal, a hop at a time,
// from mono audio as it arrives (loopback or an interface). Deterministic: the same samples give
// the same states, whatever the block sizes they arrive in.
//
// Per student frame (5.33 ms): causal features (StudentFeatureStream), the M4 student
// (StudentStream), then causal decoding of its heads:
// - Kicks: peaks of the kick head, confirmed one frame after the peak.
// - Bass: onset peaks confirmed three frames after the peak, so the decoder's +-2-frame kick mask
//   can see kicks after it; a note ends when the gate falls below its threshold (after the minimum
//   length) or at the next onset; pitch is the most common class so far. The offline decoder's
//   minimum gap keeps the strongest of two close peaks; here the later one is dropped.
// - Hats: peaks of the hat head. Residual activity: the residual head, smoothed. Residual
//   centroid and flatness: from the feature frame's bands above 400 Hz (band powers, so flatness
//   is on a coarser scale than file mode's per-bin value).
// Beat: a second-order phase-locked loop on kick onsets. The tempo is acquired from the folded
// inter-onset intervals of the first kicks; each kick near a predicted beat then corrects the phase
// by pll_phase_gain and the period by pll_period_gain of the error; lock is declared after
// lock_kicks kicks within lock_beats. Kicks far from every predicted beat several times in a row
// (a new track in a mix) restart the acquisition. Between kicks (breakdowns) the loop runs free.
// Kicks count for downbeats and sections only in runs (on two consecutive beats or more, as file
// mode's kicks), so a stray hit in a breakdown does not end it. Downbeat: the first kick of a run
// that returns after a bar or more. Sections: breakdown after
// breakdown_bars bars without a kick, build when the residual activity rises in a breakdown, drop
// when the kick returns after drop_after_bars bars, groove otherwise; phrases restart at section
// starts.
//
// Latency, measured from the audio a decision describes: the student's lookahead plus one frame
// for kicks and hats, plus three for bass onsets (latency_s), plus up to one hop of buffering. The
// beat phase is predicted, so it carries no latency once locked. Allocates only in prepare(),
// except when recording (tools and tests).
#pragma once

#include "analysis/bass_tracker.h"
#include "analysis/student_data.h"
#include "core/result.h"
#include "ml/student.h"

#include <array>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace psyight {

enum class LiveSection { intro, groove, breakdown, build, drop };
const char *live_section_name(LiveSection section); // As file mode's labels.

struct LiveParams {
    double min_bpm = 120.0;
    double max_bpm = 190.0;
    int acquire_kicks = 8;         // Kicks used to measure the tempo before the loop starts.
    double acquire_spread = 0.04;  // Folded intervals must agree within this fraction.
    double pll_phase_gain = 0.25;  // Of each kick's phase error, applied to the phase.
    double pll_period_gain = 0.05; // Of each kick's phase error, applied to the period.
    double capture_beats = 0.25;   // A kick this close to a predicted beat steers the loop.
    double lock_beats = 0.06;      // Within this, a kick counts towards lock (24 ms at 150 BPM).
    int lock_kicks = 4;
    int reacquire_kicks = 4; // Consecutive kicks outside the capture range: start again.
    int breakdown_bars = 2;
    int drop_after_bars = 8;
    int drop_bars = 8;
    int phrase_bars = 8;
    double build_rise =
        6.0 / 90.0; // Residual activity rise (6 dB) that turns a breakdown into a build.
    double residual_smoothing_s = 0.05;
    double kick_threshold = 0.5;
    double hat_threshold = 0.5;
    int event_min_gap_frames = 4; // Kicks and hats, as the offline decoder's kick peaks.
};

inline constexpr double kLiveNever = -std::numeric_limits<double>::infinity();

// Everything the engine needs, in source seconds (the capture clock). Trivially copyable, so it
// can be published through a seqlock.
struct LiveState {
    long long frames = 0;    // Student frames decoded.
    double analysed_s = 0.0; // The newest frame decoded describes this time.
    double latency_s = 0.0;  // Bass onset decision delay (kicks: two frames less).
    // Beat.
    bool tempo_known = false;
    bool locked = false;
    double period_s = 0.4;
    double anchor_s = 0.0;       // A predicted beat...
    long long anchor_beat = 0;   // ...and its index.
    long long downbeat_beat = 0; // A beat index on a downbeat.
    long long phrase_beat = 0;   // A beat where a phrase starts (phrases are phrase_bars long).
    int phrase_bars = 8;
    double last_error_ms = 0.0; // The last steering kick's phase error.
    long long last_kick_beat = std::numeric_limits<long long>::min();
    int kick_streak = 0; // Consecutive beats with a kick, up to the last kick.
    long long last_run_beat = std::numeric_limits<long long>::min(); // Last kick in a run.
    // Events.
    double last_kick_s = kLiveNever;
    long long kicks = 0;
    double last_hat_s = kLiveNever;
    long long hats = 0;
    double residual_level = 0.0; // 0 to 1, as file mode's residual level.
    double residual_centroid_hz = 0.0;
    double residual_flatness = 0.0;
    // Bass.
    bool bass_active = false;
    int bass_midi = 0;
    double bass_onset_s = kLiveNever;
    double bass_end_s = kLiveNever; // Of the last finished note.
    double bass_confidence = 0.0;
    double bass_typical_s = 0.06; // Recent note length, for the note phase while one sounds.
    long long bass_notes = 0;
    // Section.
    LiveSection section = LiveSection::intro;
    double section_start_s = 0.0;
    int section_count = 0;
    // For the scene rotation (engine/automation.h): starts of each kind so far, the current one
    // included, and the section before the current one.
    std::array<int, 5> section_occurrences{};
    bool has_previous = false;
    LiveSection previous_section = LiveSection::intro;
    int previous_occurrence = 0;
};

// The beat, downbeat, phrase and section half of the live analysis, driven by kick times: the
// loop and the rules above. Writes the beat and section fields of a LiveState and reads its
// residual_level. Separate so that tests can drive it with exact kick times.
class LiveBeat {
  public:
    void prepare(const LiveParams &params); // The only allocation.
    void reset();
    void on_kick(LiveState &state, double t_s);
    void update(LiveState &state, double t_s); // Once a frame: breakdowns, builds, drop ends.
    static double position(const LiveState &state, double t_s); // In beats.
    static double time_of(const LiveState &state, long long beat);

  private:
    void set_section(LiveState &state, LiveSection section, double start_s);

    LiveParams params_;
    std::vector<double> acquire_kicks_s_; // Recent kick times while acquiring.
    bool acquiring_ = true;
    int off_grid_kicks_ = 0;
    int in_lock_kicks_ = 0;
    double breakdown_floor_ = 1.0; // Lowest residual activity since the breakdown began.
    long long drop_end_beat_ = 0;
};

class LiveAnalyser {
  public:
    // The model must outlive the analyser.
    Result<void> prepare(int sample_rate_hz, const StudentModel &model,
                         const LiveParams &params = {});
    void reset();
    // Mono samples in arrival order, any block size.
    void process(std::span<const float> samples);
    const LiveState &state() const { return state_; }
    double frame_s() const { return frame_s_; }

    // Tools and tests: keep every kick and finished note (allocates).
    void set_recording(bool on) { recording_ = on; }
    const std::vector<double> &recorded_kicks() const { return recorded_kicks_; }
    const std::vector<BassNote> &recorded_notes() const { return recorded_notes_; }
    // The state after each frame's decisions, newest last (recording only).
    const std::vector<LiveState> &recorded_states() const { return recorded_states_; }

  private:
    static constexpr int kRing = 16; // Frames of head outputs kept (a power of two).
    struct Frame {
        float kick = 0, onset = 0, gate = 0, hat = 0, residual = 0;
        int pitch_class = 0;
        float centroid_hz = 0, flatness = 0; // From the features of this input frame.
    };
    Frame &at(long long frame) { return ring_[static_cast<size_t>(frame & (kRing - 1))]; }
    void run_frame();               // Features, student, decoding for the next input frame.
    void decode_frame(long long j); // j: the input frame the newest outputs describe.
    void scan_note(long long j);    // Gate and pitch of the sounding note up to frame j.

    void on_kick(double t_s);
    void finish_note(double end_s);
    void begin_note(long long frame, double onset_s);

    const StudentModel *model_ = nullptr;
    LiveParams params_;
    int sample_rate_hz_ = 0;
    double frame_s_ = 0.0;
    StudentFeatureStream features_;
    std::unique_ptr<StudentStream> student_;
    std::vector<float> pending_; // Samples of the hop being filled.
    size_t pending_count_ = 0;
    std::vector<float> feature_frame_, binary_, pitch_;
    long long input_frames_ = 0; // Feature frames computed.
    std::array<Frame, kRing> ring_{};
    long long last_kick_frame_ = -1000;
    long long last_hat_frame_ = -1000;
    long long last_onset_frame_ = -1000;
    long long note_frame_ = -1;             // Onset frame of the sounding note.
    long long scan_ = 0;                    // Next frame of the sounding note to scan.
    std::array<long long, 8> candidates_{}; // Bass onset peaks waiting for the kick mask.
    int candidate_count_ = 0;
    std::array<long long, 4> recent_kick_frames_{};
    std::vector<float> short_centres_hz_; // Centre of each short band.
    std::vector<int> pitch_counts_;
    LiveBeat beat_;
    LiveState state_;
    bool recording_ = false;
    std::vector<double> recorded_kicks_;
    std::vector<BassNote> recorded_notes_;
    std::vector<LiveState> recorded_states_;
};

} // namespace psyight
