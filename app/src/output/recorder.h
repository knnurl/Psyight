// Session recorder (SPEC sections 4.2 and 11): writes a session folder (io/session_record.h) on
// its own thread. The show thread pushes frames and the GUI thread pushes configurations, each
// through its own single-producer queue; a full queue drops the item and counts it rather than
// blocking either thread.
#pragma once

#include "core/result.h"
#include "core/spsc_queue.h"
#include "io/session_record.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace psyight {

class Recorder {
  public:
    // Creates the folder (it must not exist yet) and writes meta.json and events.jsonl.
    static Result<std::unique_ptr<Recorder>>
    start(const std::filesystem::path &folder, const SessionMeta &meta, const std::string &events);
    ~Recorder(); // Writes what is queued, then closes the files.
    Recorder(const Recorder &) = delete;
    Recorder &operator=(const Recorder &) = delete;

    bool push_frame(std::shared_ptr<const RecordedFrame> frame); // Show thread.
    bool push_params(std::shared_ptr<const std::string> line);   // GUI thread.

    const std::filesystem::path &folder() const { return folder_; }
    std::int64_t frames_written() const { return frames_written_.load(); }
    std::int64_t dropped() const { return dropped_.load(); }
    std::string error() const; // The first write error, if any.

  private:
    Recorder(std::filesystem::path folder, int pixels);
    void run();
    bool drain();

    std::filesystem::path folder_;
    int pixels_;
    std::ofstream frames_;
    std::ofstream params_;
    SpscQueue<std::shared_ptr<const RecordedFrame>> frame_queue_{1024}; // About 17 s at 60 Hz.
    SpscQueue<std::shared_ptr<const std::string>> params_queue_{256};
    std::atomic<std::int64_t> frames_written_{0};
    std::atomic<std::int64_t> dropped_{0};
    std::atomic<bool> failed_{false};
    std::string error_; // Written by the writer thread before failed_ is set.
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace psyight
