#include "gui/file_flow.h"

#include "core/paths.h"
#include "gui/file_dialog.h"
#include "gui/widgets.h"
#include "io/recovery.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

namespace psyight {

namespace {

constexpr const char *kWorkingDirectoryProject = "psyight.psyproj"; // The start-up file until now.

const char *popup_id(int prompt) {
    static const char *const ids[] = {"",
                                      "Unsaved changes###project",
                                      "Unsaved labels###track",
                                      "Unsaved changes###quit",
                                      "Restore###project",
                                      "Restore###labels"};
    return ids[prompt];
}

// A row of buttons under a question; returns the index of the one pressed, or -1. The first is
// the main action (accent); Escape presses the last one.
int buttons(std::initializer_list<const char *> labels) {
    ImGui::Spacing();
    int pressed = -1;
    int i = 0;
    for (const char *label : labels) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (i == 0 ? primary_button(label) : ImGui::Button(label)) {
            pressed = i;
        }
        ++i;
    }
    if (pressed < 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pressed = i - 1;
    }
    return pressed;
}

} // namespace

FileFlow::FileFlow(ProjectState &project, Session &session)
    : project_(project), session_(session) {}

bool FileFlow::start(bool capture) {
    capture_ = capture;
    if (capture) {
        if (std::filesystem::exists(kWorkingDirectoryProject)) {
            (void)project_.open(kWorkingDirectoryProject);
        }
        return false;
    }
    const std::vector<std::filesystem::path> recent = recent_projects_.items();
    for (const std::filesystem::path &path : recent) {
        if (open_project(path)) {
            return false;
        }
    }
    if (std::filesystem::exists(kWorkingDirectoryProject) &&
        open_project(kWorkingDirectoryProject)) {
        return false;
    }
    // Untitled: perhaps with unsaved changes from a session that did not end normally.
    restore_project_pending_ = project_.recovery_available();
    return !restore_project_pending_;
}

bool FileFlow::open_project(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
        spdlog::warn("Project {} is gone; removed from the recent list", path_to_utf8(path));
        recent_projects_.remove(path);
        return false;
    }
    if (const Result<void> opened = project_.open(path); !opened) {
        spdlog::error("{}", opened.error().message);
        return false;
    }
    recent_projects_.add(path);
    restore_project_pending_ = project_.recovery_available();
    return true;
}

void FileFlow::draw_menu_items() {
    if (ImGui::MenuItem("New project", "Ctrl+N")) {
        begin(Action::new_project);
    }
    if (ImGui::MenuItem("Open project...", "Ctrl+Shift+O")) {
        open_dialog_requested_ = true;
    }
    if (ImGui::BeginMenu("Open recent project", !recent_projects_.items().empty())) {
        std::filesystem::path chosen;
        for (const std::filesystem::path &path : recent_projects_.items()) {
            ImGui::PushID(path_to_utf8(path).c_str());
            const bool current = !project_.untitled() && path == project_.path();
            if (ImGui::MenuItem(path_to_utf8(path.stem()).c_str(), nullptr, current)) {
                chosen = path;
            }
            ImGui::SetItemTooltip("%s", path_to_utf8(path).c_str());
            ImGui::PopID();
        }
        ImGui::Separator();
        const bool clear = ImGui::MenuItem("Clear the list");
        ImGui::EndMenu();
        if (clear) {
            recent_projects_.clear();
        } else if (!chosen.empty()) {
            begin(Action::open_path, chosen);
        }
    }
    ImGui::Separator();
    const bool labels_dirty = session_.has_track() && session_.labels().dirty();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, project_.dirty() || labels_dirty)) {
        (void)save_all(Action::none);
    }
    ImGui::SetItemTooltip("Saves the project and the track's labels, whichever have changes.");
    if (ImGui::MenuItem("Save project as...", "Ctrl+Shift+S")) {
        save_as_requested_ = true;
        after_save_as_ = Action::none;
    }
}

void FileFlow::handle_shortcuts() {
    constexpr ImGuiInputFlags kGlobal = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, kGlobal)) {
        begin(Action::new_project);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O, kGlobal)) {
        open_dialog_requested_ = true;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, kGlobal)) {
        (void)save_all(Action::none);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, kGlobal)) {
        save_as_requested_ = true;
        after_save_as_ = Action::none;
    }
}

void FileFlow::begin(Action action, std::filesystem::path path) {
    action_ = action;
    action_path_ = std::move(path);
    if (project_.dirty()) {
        open_prompt(Prompt::save_project);
        return;
    }
    perform(action);
}

void FileFlow::perform(Action action) {
    switch (action) {
    case Action::new_project:
        project_.new_project();
        restore_project_pending_ = false;
        break;
    case Action::open_path:
        (void)open_project(action_path_);
        break;
    case Action::quit:
        quit_confirmed_ = true;
        break;
    case Action::none:
        break;
    }
    action_ = Action::none;
    action_path_.clear();
}

bool FileFlow::save_all(Action then) {
    if (session_.has_track() && session_.labels().dirty()) {
        if (const Result<void> saved = session_.save_labels(); !saved) {
            spdlog::error("{}", saved.error().message);
            return false;
        }
        spdlog::info("Saved {}", path_to_utf8(session_.labels_path().filename()));
    }
    if (project_.dirty()) {
        if (project_.untitled()) {
            save_as_requested_ = true; // The dialog runs between frames, then `then` follows.
            after_save_as_ = then;
            return false;
        }
        if (const Result<void> saved = project_.save(); !saved) {
            spdlog::error("{}", saved.error().message);
            return false;
        }
    }
    perform(then);
    return true;
}

bool FileFlow::ready_to_quit() {
    if (quit_confirmed_) {
        return true;
    }
    const bool labels_dirty = session_.has_track() && session_.labels().dirty();
    if (!project_.dirty() && !labels_dirty) {
        return true;
    }
    action_ = Action::quit;
    open_prompt(Prompt::quit);
    return false;
}

void FileFlow::open_prompt(Prompt prompt) {
    prompt_ = prompt;
    popup_open_requested_ = true; // Opened in draw(), at the top level of the ID stack.
}

void FileFlow::draw() {
    if (capture_) {
        return;
    }
    // What the panels asked for.
    std::filesystem::path requested;
    switch (project_.take_request(requested)) {
    case ProjectRequest::new_project:
        begin(Action::new_project);
        break;
    case ProjectRequest::open:
        open_dialog_requested_ = true;
        break;
    case ProjectRequest::open_path:
        begin(Action::open_path, requested);
        break;
    case ProjectRequest::save:
        (void)save_all(Action::none);
        break;
    case ProjectRequest::save_as:
        save_as_requested_ = true;
        after_save_as_ = Action::none;
        break;
    case ProjectRequest::none:
        break;
    }
    // Questions that come from the state: one at a time.
    if (prompt_ == Prompt::none) {
        if (!session_.pending_open().empty()) {
            open_prompt(Prompt::switch_track);
        } else if (restore_project_pending_) {
            open_prompt(Prompt::restore_project);
        } else if (session_.has_track() && session_.labels_recovery_available()) {
            open_prompt(Prompt::restore_labels);
        }
    }
    if (prompt_ == Prompt::none) {
        return;
    }
    const char *id = popup_id(static_cast<int>(prompt_));
    if (popup_open_requested_) {
        ImGui::OpenPopup(id);
        popup_open_requested_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const std::string project_name = "\"" + project_.name() + "\"";
    const std::string track = session_.has_track() ? session_.track().name : std::string();
    bool done = false;
    switch (prompt_) {
    case Prompt::save_project: {
        ImGui::Text("Save the changes to the project %s first?", project_name.c_str());
        ImGui::TextDisabled("If you don't, they are lost.");
        const int b = buttons({"Save", "Don't save", "Cancel"});
        if (b == 0) {
            const Action then = action_;
            done = true;
            if (project_.untitled()) {
                save_as_requested_ = true;
                after_save_as_ = then;
            } else if (const Result<void> saved = project_.save(); saved) {
                perform(then);
            } else {
                spdlog::error("{}", saved.error().message);
            }
        } else if (b == 1) {
            project_.discard_changes();
            perform(action_);
            done = true;
        } else if (b == 2) {
            action_ = Action::none;
            done = true;
        }
        break;
    }
    case Prompt::switch_track: {
        const std::string next = path_to_utf8(session_.pending_open().filename());
        ImGui::Text("Save the label changes for %s before opening %s?", track.c_str(),
                    next.c_str());
        ImGui::TextDisabled("Notes, the grid and edited sections.");
        const int b = buttons({"Save", "Don't save", "Cancel"});
        if (b >= 0) {
            session_.resolve_pending_open(b == 0 ? Session::PendingChoice::save
                                                 : b == 1 ? Session::PendingChoice::discard
                                                          : Session::PendingChoice::cancel);
            done = true;
        }
        break;
    }
    case Prompt::quit: {
        ImGui::TextUnformatted("Save before quitting?");
        if (project_.dirty()) {
            ImGui::BulletText("The project %s", project_name.c_str());
        }
        if (session_.has_track() && session_.labels().dirty()) {
            ImGui::BulletText("The labels of %s", track.c_str());
        }
        const int b = buttons({"Save", "Don't save", "Cancel"});
        if (b == 0) {
            done = true;
            (void)save_all(Action::quit); // Untitled: the Save as dialog comes first.
        } else if (b == 1) {
            project_.discard_changes();
            session_.discard_label_changes();
            perform(Action::quit);
            done = true;
        } else if (b == 2) {
            action_ = Action::none;
            done = true;
        }
        break;
    }
    case Prompt::restore_project: {
        ImGui::Text("The project %s has unsaved changes from %s,", project_name.c_str(),
                    project_.recovery_age().c_str());
        ImGui::TextUnformatted("kept when Psyight last closed without saving them.");
        const int b = buttons({"Restore them", "Discard them"});
        if (b == 0) {
            if (const Result<void> restored = project_.restore_recovery(); !restored) {
                spdlog::error("{}", restored.error().message);
            }
        } else if (b == 1) {
            project_.discard_recovery();
        }
        if (b >= 0) {
            restore_project_pending_ = false;
            done = true;
        }
        break;
    }
    case Prompt::restore_labels: {
        ImGui::Text("The labels of %s have unsaved changes from %s,", track.c_str(),
                    recovery_age(session_.labels_path()).c_str());
        ImGui::TextUnformatted("kept when they were last closed without saving.");
        const int b = buttons({"Restore them", "Discard them"});
        if (b == 0) {
            if (const Result<void> restored = session_.restore_labels_recovery(); !restored) {
                spdlog::error("{}", restored.error().message);
            }
        } else if (b == 1) {
            session_.discard_labels_recovery();
        }
        done = b >= 0;
        break;
    }
    case Prompt::none:
        break;
    }
    if (done) {
        prompt_ = Prompt::none;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FileFlow::after_frame(void *window) {
    if (open_dialog_requested_) {
        open_dialog_requested_ = false;
        if (const auto chosen = open_project_file_dialog(window); chosen) {
            begin(Action::open_path, *chosen);
        }
    }
    if (save_as_requested_) {
        save_as_requested_ = false;
        const Action then = after_save_as_;
        after_save_as_ = Action::none;
        const std::filesystem::path suggested =
            project_.untitled() ? std::filesystem::path(project_.name() + ".psyproj")
                                : project_.path();
        const auto chosen = save_project_file_dialog(window, suggested);
        if (!chosen) {
            action_ = Action::none; // Cancelled: whatever was to follow does not happen.
            return;
        }
        if (const Result<void> saved = project_.save_as(*chosen); !saved) {
            spdlog::error("{}", saved.error().message);
            action_ = Action::none;
            return;
        }
        recent_projects_.add(*chosen);
        perform(then);
    }
}

std::string FileFlow::window_title() const {
    return project_.name() + (project_.dirty() ? " (unsaved)" : "") + " - Psyight";
}

} // namespace psyight
