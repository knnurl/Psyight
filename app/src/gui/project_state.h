// The open project (io/project.h), shared by the preview and rig panels, with the rig and patch
// built from it. Panels change it through edit() and then call changed(), which rebuilds the rig
// and patch and bumps the version; the preview republishes its show config when the version
// changes. Loaded from psyight.psyproj in the working directory
// at start when that exists, and saved there on exit when changed and at most a minute after a
// change (autosave), unless the file could not be read (a damaged file is never overwritten).
#pragma once

#include "core/result.h"
#include "engine/rig.h"
#include "io/project.h"
#include "output/patch.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace psyight {

class ProjectState {
  public:
    ProjectState();
    ~ProjectState(); // Saves unsaved changes.
    ProjectState(const ProjectState &) = delete;
    ProjectState &operator=(const ProjectState &) = delete;

    const Project &project() const { return project_; }
    Project &edit() { return project_; } // Call changed() after changing it.
    void changed();
    const Rig &rig() const { return rig_; }
    const Patch &patch() const { return patch_; }
    std::uint64_t version() const { return version_; }
    bool dirty() const { return dirty_; }

    std::filesystem::path &path() { return path_; }
    Result<void> load();
    Result<void> save();
    void autosave(); // Once a frame: saves a change after a minute (crash recovery, M6).
    void reset_to_default();
    const std::string &status() const { return status_; }

  private:
    void rebuild();

    Project project_ = default_project();
    Rig rig_;
    Patch patch_;
    std::uint64_t version_ = 1;
    bool dirty_ = false;
    bool file_unreadable_ = false;
    std::filesystem::path path_ = "psyight.psyproj";
    std::string status_;
    std::chrono::steady_clock::time_point last_save_ = std::chrono::steady_clock::now();
};

} // namespace psyight
