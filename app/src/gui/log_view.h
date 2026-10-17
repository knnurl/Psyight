// Log panel (SPEC section 9.4, M1): the latest messages of the default spdlog logger, kept by a
// ring-buffer sink added at start-up, warnings in amber and errors in red. Threads that log are
// never blocked by the panel for longer than the sink's copy; the audio thread never logs.
#pragma once

#include <spdlog/sinks/ringbuffer_sink.h>

#include <memory>

namespace psyight {

class LogView {
  public:
    // Adds the ring buffer to the default logger. Call before other threads start logging.
    LogView();

    void draw();

  private:
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink_;
    bool problems_only_ = false;
    bool follow_ = true;
};

} // namespace psyight
