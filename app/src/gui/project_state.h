// The open project (io/project.h), shared by the preview and rig panels, with the rig and patch
// built from it. Panels change it through edit() and then call changed(), which rebuilds the rig
// and patch and bumps the version; the preview republishes its show config when the version
// changes.
// Files: the project is untitled (never saved) or has a file, and that file changes only on
// save() or save_as(). While there are unsaved changes, autosave() writes a recovery copy beside
// it (io/recovery.h; untitled projects: kUntitledFile in the working directory) at most once a
// minute, and the destructor writes one if the changes were neither saved nor discarded. Opening,
// new_project() and the panels' buttons go through requests (request_open and so on) that the
// file flow (gui/file_flow.h) carries out, asking first when there are unsaved changes.
// Undo: every changed() records the project as it was (core/undo_history.h; changes within half a
// second are one step, so a knob drag undoes at once); resetting to the default is undoable, and
// opening a file or a new project clears the history.
#pragma once

#include "core/result.h"
#include "core/undo_history.h"
#include "engine/rig.h"
#include "io/project.h"
#include "output/patch.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace psyight {

// What the panels ask the file flow to do.
enum class ProjectRequest { none, new_project, open, open_path, save, save_as };

class ProjectState {
  public:
    // Where an untitled project's recovery copy goes (as "<this>.autosave").
    static constexpr const char *kUntitledFile = "untitled.psyproj";

    ProjectState();
    ~ProjectState(); // Writes a recovery copy of unsaved changes; never the file itself.
    ProjectState(const ProjectState &) = delete;
    ProjectState &operator=(const ProjectState &) = delete;

    const Project &project() const { return project_; }
    Project &edit() { return project_; } // Call changed() after changing it.
    void changed();
    const Rig &rig() const { return rig_; }
    const Patch &patch() const { return patch_; }
    std::uint64_t version() const { return version_; }
    bool dirty() const { return dirty_; }

    // The project's file; empty while untitled.
    const std::filesystem::path &path() const { return path_; }
    bool untitled() const { return path_.empty(); }
    std::string name() const; // The file name without .psyproj, or "Untitled".

    // Opens a file: on failure the current project and file stay. Clears the undo history.
    Result<void> open(const std::filesystem::path &path);
    Result<void> save(); // Precondition: !untitled().
    Result<void> save_as(const std::filesystem::path &path);
    void new_project();     // The default project, untitled.
    void discard_changes(); // Unsaved changes are dropped on purpose: no recovery copy.
    void autosave();        // Once a frame: the recovery copy, a minute after a change.

    // The recovery copy for the current file (or the untitled one), when newer than the file.
    bool recovery_available() const;
    std::string recovery_age() const;
    Result<void> restore_recovery(); // Its contents become the unsaved project.
    void discard_recovery();

    void request(ProjectRequest r, std::filesystem::path path = {});
    ProjectRequest take_request(std::filesystem::path &path); // For the file flow.

    void reset_to_default();
    bool can_undo() const { return history_.can_undo(); }
    bool can_redo() const { return history_.can_redo(); }
    bool undo();
    bool redo();
    const std::string &status() const { return status_; }

  private:
    void rebuild();
    void replace(Project project); // Opened, new or restored: a fresh history.
    std::filesystem::path recovery_file() const;
    void write_recovery();

    Project project_ = default_project();
    Project committed_ = project_; // As of the last change: the state an edit starts from.
    UndoHistory<Project> history_;
    Rig rig_;
    Patch patch_;
    std::uint64_t version_ = 1;
    bool dirty_ = false;
    std::filesystem::path path_;
    std::string status_;
    std::chrono::steady_clock::time_point last_recovery_ = std::chrono::steady_clock::now();
    ProjectRequest request_ = ProjectRequest::none;
    std::filesystem::path request_path_;
};

} // namespace psyight
