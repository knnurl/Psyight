#include "gui/project_state.h"

#include "core/paths.h"

#include <spdlog/spdlog.h>

namespace psyight {

ProjectState::ProjectState() {
    rebuild();
    if (std::filesystem::exists(path_)) {
        if (const Result<void> loaded = load(); !loaded) {
            file_unreadable_ = true;
            spdlog::error("{}", loaded.error().message);
        }
    }
}

ProjectState::~ProjectState() {
    if (dirty_ && !file_unreadable_) {
        if (const Result<void> saved = save(); !saved) {
            spdlog::error("{}", saved.error().message);
        }
    }
}

void ProjectState::changed() {
    rebuild();
    ++version_;
    dirty_ = true;
}

void ProjectState::rebuild() {
    rig_ = rig_of(project_);
    patch_ = patch_of(project_);
}

Result<void> ProjectState::load() {
    Result<Project> loaded = load_project(path_);
    if (!loaded) {
        status_ = loaded.error().message;
        return loaded.error();
    }
    project_ = std::move(loaded.value());
    rebuild();
    ++version_;
    dirty_ = false;
    file_unreadable_ = false;
    status_ = "Loaded " + path_to_utf8(path_);
    spdlog::info("{}", status_);
    return {};
}

Result<void> ProjectState::save() {
    if (const Result<void> saved = save_project(path_, project_); !saved) {
        status_ = saved.error().message;
        return saved;
    }
    dirty_ = false;
    file_unreadable_ = false;
    status_ = "Saved " + path_to_utf8(path_);
    spdlog::info("{}", status_);
    return {};
}

void ProjectState::autosave() {
    const auto now = std::chrono::steady_clock::now();
    if (!dirty_ || file_unreadable_ || now - last_save_ < std::chrono::minutes(1)) {
        return;
    }
    last_save_ = now;
    if (const Result<void> saved = save(); !saved) {
        spdlog::error("{}", saved.error().message);
    }
}

void ProjectState::reset_to_default() {
    project_ = default_project();
    rebuild();
    ++version_;
    dirty_ = true;
}

} // namespace psyight
