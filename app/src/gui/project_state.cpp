#include "gui/project_state.h"

#include "core/paths.h"
#include "io/recovery.h"

#include <spdlog/spdlog.h>

namespace psyight {

namespace {

constexpr auto kRecoveryInterval = std::chrono::minutes(1);

double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::vector<StripSpec> specs_of(const Project &p) {
    std::vector<StripSpec> specs;
    for (const ProjectStrip &s : p.strips) {
        specs.push_back(s.spec);
    }
    return specs;
}

// Counts of the strips that fill their paths, fitted to their pieces (engine/rig.h: joints).
void fit(Project &p) {
    std::vector<StripSpec> specs = specs_of(p);
    fit_counts(specs);
    for (size_t i = 0; i < specs.size(); ++i) {
        p.strips[i].spec.count = specs[i].count;
    }
}

bool same_strips(const Project &a, const Project &b) {
    if (a.strips.size() != b.strips.size()) {
        return false;
    }
    for (size_t i = 0; i < a.strips.size(); ++i) {
        const StripSpec &x = a.strips[i].spec;
        const StripSpec &y = b.strips[i].spec;
        if (x.name != y.name || x.count != y.count || x.fills != y.fills ||
            x.pixels_per_m != y.pixels_per_m || x.polyline.size() != y.polyline.size()) {
            return false;
        }
        for (size_t k = 0; k < x.polyline.size(); ++k) {
            if (x.polyline[k].x != y.polyline[k].x || x.polyline[k].y != y.polyline[k].y ||
                x.polyline[k].z != y.polyline[k].z) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

ProjectState::ProjectState() {
    fit(project_);
    committed_ = project_;
    rebuild();
}

ProjectState::~ProjectState() {
    if (dirty_) {
        write_recovery();
    }
}

std::string ProjectState::name() const {
    return untitled() ? "Untitled" : path_to_utf8(path_.stem());
}

void ProjectState::changed() {
    if (!same_strips(project_, committed_)) {
        fit(project_);
    }
    history_.record(committed_, now_s());
    committed_ = project_;
    rebuild();
    ++version_;
    dirty_ = true;
}

void ProjectState::rebuild() {
    rig_ = rig_of(project_);
    patch_ = patch_of(project_);
}

void ProjectState::replace(Project project) {
    project_ = std::move(project);
    fit(project_); // Strips that fill their paths fill their pieces.
    committed_ = project_;
    history_.clear();
    rebuild();
    ++version_;
}

std::filesystem::path ProjectState::recovery_file() const {
    return untitled() ? std::filesystem::path(kUntitledFile) : path_;
}

void ProjectState::write_recovery() {
    if (const Result<void> saved = save_project(recovery_path(recovery_file()), project_); !saved) {
        spdlog::error("Recovery copy not written: {}", saved.error().message);
    }
}

Result<void> ProjectState::open(const std::filesystem::path &path) {
    Result<Project> loaded = load_project(path);
    if (!loaded) {
        status_ = loaded.error().message;
        return loaded.error();
    }
    path_ = path;
    replace(std::move(loaded.value()));
    dirty_ = false;
    status_ = "Opened " + path_to_utf8(path_.filename());
    spdlog::info("Opened project {}", path_to_utf8(path_));
    return {};
}

Result<void> ProjectState::save() {
    if (untitled()) {
        return Error{"the project has no file yet: use Save as"};
    }
    if (const Result<void> saved = save_project(path_, project_); !saved) {
        status_ = saved.error().message;
        return saved;
    }
    remove_recovery(path_);
    dirty_ = false;
    status_ = "Saved " + path_to_utf8(path_.filename());
    spdlog::info("Saved project {}", path_to_utf8(path_));
    return {};
}

Result<void> ProjectState::save_as(const std::filesystem::path &path) {
    const std::filesystem::path before = recovery_file();
    const std::filesystem::path previous = path_;
    path_ = path;
    if (Result<void> saved = save(); !saved) {
        path_ = previous;
        return saved;
    }
    remove_recovery(before); // The untitled copy, or the old file's.
    return {};
}

void ProjectState::new_project() {
    discard_changes();
    path_.clear();
    replace(default_project());
    status_ = "New project";
}

void ProjectState::discard_changes() {
    remove_recovery(recovery_file());
    dirty_ = false;
}

void ProjectState::autosave() {
    const auto now = std::chrono::steady_clock::now();
    if (!dirty_ || now - last_recovery_ < kRecoveryInterval) {
        return;
    }
    last_recovery_ = now;
    write_recovery();
}

bool ProjectState::recovery_available() const { return recovery_newer(recovery_file()); }

std::string ProjectState::recovery_age() const { return psyight::recovery_age(recovery_file()); }

Result<void> ProjectState::restore_recovery() {
    Result<Project> loaded = load_project(recovery_path(recovery_file()));
    if (!loaded) {
        status_ = loaded.error().message;
        return loaded.error();
    }
    replace(std::move(loaded.value()));
    dirty_ = true; // Restored, not saved: the copy stays until a save or a discard.
    status_ = "Restored unsaved changes";
    spdlog::info("Restored the unsaved changes of {}", name());
    return {};
}

void ProjectState::discard_recovery() { remove_recovery(recovery_file()); }

void ProjectState::request(ProjectRequest r, std::filesystem::path path) {
    request_ = r;
    request_path_ = std::move(path);
}

ProjectRequest ProjectState::take_request(std::filesystem::path &path) {
    const ProjectRequest r = request_;
    path = std::move(request_path_);
    request_ = ProjectRequest::none;
    request_path_.clear();
    return r;
}

bool ProjectState::undo() {
    if (!history_.undo(project_)) {
        return false;
    }
    committed_ = project_;
    rebuild();
    ++version_;
    dirty_ = true;
    return true;
}

bool ProjectState::redo() {
    if (!history_.redo(project_)) {
        return false;
    }
    committed_ = project_;
    rebuild();
    ++version_;
    dirty_ = true;
    return true;
}

void ProjectState::reset_to_default() {
    history_.record(committed_, now_s());
    project_ = default_project();
    fit(project_);
    committed_ = project_;
    rebuild();
    ++version_;
    dirty_ = true;
}

} // namespace psyight
