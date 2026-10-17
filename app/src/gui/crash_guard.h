// Crash recovery (SPEC M6): what the next start needs to know about the last one ending badly.
// - A marker file (psyight.running in the working directory) exists while the app runs; finding
//   it at start means the last session crashed or lost power. The app says so in the log, which
//   (like the labels and the project) autosaves.
// - Any crash writes a minidump to logs/ (dump files open in Visual Studio or WinDbg with the
//   build's .pdb files) before the process ends.
// - Logs also go to logs/psyight.log (5 MB, three old files kept).
// When the stream stops, the node runs its own loss policy after 2 s (by default a dim fallback
// show; firmware/node-f767), so the lights do not depend on the app surviving.
#pragma once

#include <filesystem>

namespace psyight {

class CrashGuard {
  public:
    explicit CrashGuard(std::filesystem::path working_directory = ".");
    ~CrashGuard(); // A clean exit: removes the marker.
    CrashGuard(const CrashGuard &) = delete;
    CrashGuard &operator=(const CrashGuard &) = delete;

    bool last_session_crashed() const { return last_crashed_; }

  private:
    std::filesystem::path marker_;
    bool last_crashed_ = false;
};

} // namespace psyight
