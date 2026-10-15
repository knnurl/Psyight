// Source and transport panel (SPEC section 9.4, M1): load a file, waveform overview, play and
// pause (Space), seek by clicking the overview, loop region with draggable bounds, volume.
#pragma once

#include "audio/transport.h"

#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace psyight {

class TransportPanel {
  public:
    explicit TransportPanel(Transport &transport) : transport_(transport) {}

    // Decodes on a worker thread; the panel shows progress and swaps the file in when ready.
    void open_file(const std::filesystem::path &path);
    void draw();

  private:
    struct LoadedFile {
        Result<AudioBuffer> audio = Error{"not loaded"};
        std::vector<double> overview_time_s; // Bucket centres.
        std::vector<double> overview_min;
        std::vector<double> overview_max;
    };

    static LoadedFile load(std::filesystem::path path);
    void finish_loading();
    void draw_overview();

    Transport &transport_;
    std::future<LoadedFile> loading_;
    std::string loading_name_;
    std::string file_name_;
    std::string status_;
    std::vector<double> overview_time_s_;
    std::vector<double> overview_min_;
    std::vector<double> overview_max_;
    double duration_s_ = 0.0;
    bool loop_enabled_ = false;
    double loop_start_s_ = 0.0;
    double loop_end_s_ = 0.0;
    float volume_db_ = 0.0f;
};

} // namespace psyight
