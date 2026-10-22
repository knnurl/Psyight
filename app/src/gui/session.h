// The loaded track and everything the panels share: transport, file-mode analysis, ground-truth
// labels (which also hold the manual grid), the loop region and the visible time range. Loading
// (decode, overview, the whole track analysis from the mix or its stems) runs on a worker thread;
// update() swaps the result in on the GUI thread.
// The M4 student (ml/student.h) also runs on load when its weights exist at
// data/m4/student.weights.{json,bin} in the working directory (never in the repository): its
// notes can replace the DSP tracker's in the bass roll and the scoring panel, for A/B on the same
// audio (SPEC 6.9). The show always uses the DSP analysis.
// Labels files change only on save_labels(): while the labels have unsaved changes, a recovery
// copy goes beside the file (io/recovery.h) at most once a minute and on exit. Opening another
// track then waits for a decision (pending_open(), resolve_pending_open()), and a track whose
// recovery copy is newer than its labels offers it back (labels_recovery_available()).
#pragma once

#include "engine/inputs.h"

#include "analysis/front_end.h"
#include "analysis/track_analysis.h"
#include "audio/live_input.h"
#include "audio/transport.h"
#include "core/timebase.h"
#include "io/label_editor.h"
#include "ml/student.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace psyight {

struct LoadedTrack {
    std::string name;
    std::filesystem::path path;
    int sample_rate_hz = 0;
    double duration_s = 0.0;
    std::vector<double> overview_time_s; // Waveform overview: min and max per bucket.
    std::vector<double> overview_min;
    std::vector<double> overview_max;
    std::shared_ptr<const FrontEndResult> analysis; // The mix's features (in track_analysis).
    // Beats, kicks, bass notes, sections, hats, residual and aggregates; null when the analysis
    // found no tempo (then only `analysis` exists).
    std::shared_ptr<const TrackAnalysis> track_analysis;
    // The student's outputs and notes, on track_analysis's grid; null without weights or a tempo.
    std::shared_ptr<const StudentResult> student;
};

// Whose bass notes the bass roll and the scoring panel show.
enum class DetectionSource { dsp, student };

struct LoopRegion {
    bool enabled = false;
    double start_s = 0.0;
    double end_s = 0.0;
};

// Time range shown by every time-based plot, linked through ImPlot axis links.
struct TimeView {
    bool follow_playhead = true;
    double window_s = 8.0;
    double min_s = 0.0;
    double max_s = 8.0;
};

class Session {
  public:
    ~Session(); // Writes a recovery copy of unsaved labels; never the labels file itself.

    // Loads a track. With unsaved labels it only becomes pending, until resolve_pending_open().
    void open_file(const std::filesystem::path &path);
    const std::filesystem::path &pending_open() const { return pending_open_; } // Empty: none.
    enum class PendingChoice { save, discard, cancel };
    void resolve_pending_open(PendingChoice choice);
    // Re-runs the track analysis in the background on the labels' current grid (for example
    // after the grid was set by ear), from the mix or the stems.
    void reanalyse(AnalysisSource source);
    bool reanalysing() const { return reanalysis_.valid(); }
    bool stems_available() const; // For the loaded track.
    void update();                // Call once per GUI frame, before the panels.
    // Call after the panels: a pan by the user stops following the playhead; a zoom while following
    // sets the window length.
    void end_frame();

    Transport &transport() { return transport_; }
    bool has_track() const { return track_ != nullptr; }
    const LoadedTrack &track() const { return *track_; } // Precondition: has_track().
    bool loading() const { return loading_.valid(); }
    const std::string &status() const { return status_; }
    double playhead_s() const;

    // Labels for the loaded track; the manual grid lives in labels().grid.
    LabelEditor &labels() { return labels_; }
    // The sections edited by hand (the labels, when sections_edited), as the engine's timeline on
    // the labels' grid; null: the show follows the detection. Rebuilt when they change.
    std::shared_ptr<const SectionTimeline> edited_sections();
    Grid grid() const; // The labels' grid on the sample clock.
    std::filesystem::path labels_path() const;
    // Also clears the dirty flag and removes the recovery copy. Refused while an existing labels
    // file could not be read, so a damaged file is never overwritten.
    Result<void> save_labels();
    void discard_label_changes(); // Unsaved label changes are dropped on purpose.
    // A recovery copy newer than the loaded track's labels file, found when the track loaded.
    bool labels_recovery_available() const { return labels_recovery_available_; }
    Result<void> restore_labels_recovery(); // Its contents become the unsaved labels.
    void discard_labels_recovery();

    // The shown detections: the DSP tracker's, or the student's when chosen and available. Null
    // before an analysis exists.
    const std::vector<BassNote> *detected_notes() const;
    const char *detection_name() const; // "stems", "mix" or "M4 student".
    DetectionSource detection_source = DetectionSource::dsp;
    bool student_available() const { return has_track() && track_->student != nullptr; }

    // Live mode (M5): capture and causal analysis, with the student. Starting pauses the file.
    // live_input() is null while live mode is off; any thread may call it.
    Result<void> start_live(const CaptureOptions &options);
    void stop_live();
    std::shared_ptr<const LiveInput> live_input() const { return live_.load(); }
    bool live_available() const { return student_model_ != nullptr; }

    void set_loop(const LoopRegion &loop); // Also sends it to the transport.
    const LoopRegion &loop() const { return loop_; }

    TimeView view;

  private:
    struct LoadResult {
        Result<AudioBuffer> audio = Error{"not loaded"};
        std::shared_ptr<LoadedTrack> track;
    };
    static LoadResult load(std::filesystem::path path, std::shared_ptr<const StudentModel> student);
    void start_loading(const std::filesystem::path &path);
    void write_labels_recovery(const char *reason);

    Transport transport_;
    std::shared_ptr<const StudentModel> student_model_ = load_student_model();
    static std::shared_ptr<const StudentModel> load_student_model();
    std::shared_ptr<LiveInput> live_owner_; // GUI thread only.
    std::atomic<std::shared_ptr<const LiveInput>> live_;
    std::shared_ptr<const LoadedTrack> track_;
    std::future<LoadResult> loading_;
    std::future<Result<TrackAnalysis>> reanalysis_;
    const LoadedTrack *reanalysis_track_ = nullptr; // The track a re-analysis was started for.
    std::string loading_name_;
    std::string status_;
    LabelEditor labels_;
    std::shared_ptr<const SectionTimeline> timeline_; // edited_sections(), and what it was from:
    std::vector<SectionLabel> timeline_from_;
    double timeline_bpm_ = 0.0;
    SampleIndex timeline_downbeat_ = 0;
    const TrackAnalysis *timeline_analysis_ = nullptr;
    bool labels_file_unreadable_ = false;
    bool labels_recovery_available_ = false;
    std::filesystem::path pending_open_;
    LoopRegion loop_;
    double expected_view_min_s_ = 0.0; // The view as update() set it, to detect user changes.
    double expected_view_max_s_ = 0.0;
    std::chrono::steady_clock::time_point last_save_ = std::chrono::steady_clock::now();
};

} // namespace psyight
