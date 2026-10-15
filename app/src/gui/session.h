// The loaded track and everything the panels share: transport, file-mode analysis and the manual
// grid. Loading (decode, overview, front-end analysis) runs on a worker thread; update() swaps the
// result in on the GUI thread.
#pragma once

#include "analysis/front_end.h"
#include "audio/transport.h"
#include "core/timebase.h"

#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace psyight {

struct LoadedTrack {
    std::string name;
    int sample_rate_hz = 0;
    double duration_s = 0.0;
    std::vector<double> overview_time_s; // Waveform overview: min and max per bucket.
    std::vector<double> overview_min;
    std::vector<double> overview_max;
    std::shared_ptr<const FrontEndResult> analysis;
};

class Session {
  public:
    void open_file(const std::filesystem::path &path);
    void update(); // Call once per GUI frame.

    Transport &transport() { return transport_; }
    bool has_track() const { return track_ != nullptr; }
    const LoadedTrack &track() const { return *track_; } // Precondition: has_track().
    bool loading() const { return loading_.valid(); }
    const std::string &status() const { return status_; }

    Grid grid{160.0, 0, kAnalysisSampleRateHz}; // Manual grid (M1): edited by the analysis view.

  private:
    struct LoadResult {
        Result<AudioBuffer> audio = Error{"not loaded"};
        std::shared_ptr<LoadedTrack> track;
    };
    static LoadResult load(std::filesystem::path path);

    Transport transport_;
    std::shared_ptr<const LoadedTrack> track_;
    std::future<LoadResult> loading_;
    std::string loading_name_;
    std::string status_;
};

} // namespace psyight
