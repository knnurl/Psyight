// The loaded track and everything the panels share: transport, file-mode analysis, ground-truth
// labels (which also hold the manual grid), the loop region and the visible time range. Loading
// (decode, overview, front-end analysis) runs on a worker thread; update() swaps the result in on
// the GUI thread.
#pragma once

#include "analysis/front_end.h"
#include "audio/transport.h"
#include "core/timebase.h"
#include "io/label_editor.h"

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
    std::shared_ptr<const FrontEndResult> analysis;
};

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
    ~Session(); // Saves unsaved labels.

    void open_file(const std::filesystem::path &path);
    void update(); // Call once per GUI frame.

    Transport &transport() { return transport_; }
    bool has_track() const { return track_ != nullptr; }
    const LoadedTrack &track() const { return *track_; } // Precondition: has_track().
    bool loading() const { return loading_.valid(); }
    const std::string &status() const { return status_; }
    double playhead_s() const;

    // Labels for the loaded track; the manual grid lives in labels().grid.
    LabelEditor &labels() { return labels_; }
    Grid grid() const; // The labels' grid on the sample clock.
    std::filesystem::path labels_path() const;
    // Also clears the dirty flag. Refused while an existing labels file could not be read, so a
    // damaged file is never overwritten.
    Result<void> save_labels();

    void set_loop(const LoopRegion &loop); // Also sends it to the transport.
    const LoopRegion &loop() const { return loop_; }

    TimeView view;

  private:
    struct LoadResult {
        Result<AudioBuffer> audio = Error{"not loaded"};
        std::shared_ptr<LoadedTrack> track;
    };
    static LoadResult load(std::filesystem::path path);
    void autosave(const char *reason);

    Transport transport_;
    std::shared_ptr<const LoadedTrack> track_;
    std::future<LoadResult> loading_;
    std::string loading_name_;
    std::string status_;
    LabelEditor labels_;
    bool labels_file_unreadable_ = false;
    LoopRegion loop_;
    std::chrono::steady_clock::time_point last_save_ = std::chrono::steady_clock::now();
};

} // namespace psyight
