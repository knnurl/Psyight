// Opening and saving projects and labels, as the operator sees it: the project items of the File
// menu (New, Open, Open recent, Save, Save as), their shortcuts, the native file dialogs, and the
// questions the app asks before anything is lost or restored:
// - unsaved project changes before New, Open or quitting: Save, Don't save or Cancel (an untitled
//   project is saved through Save as);
// - unsaved labels before another track opens (Session::pending_open) and when quitting;
// - a recovery copy newer than its file (io/recovery.h), for the project when it opens and for
//   the labels when their track loads: Restore or Discard.
// At start the last project opened is opened again (else psyight.psyproj in the working
// directory, as before; else a new untitled project). Ctrl+S saves whatever is unsaved: the
// labels and the project. Panels ask for these actions through ProjectState::request().
#pragma once

#include "gui/project_state.h"
#include "gui/session.h"
#include "io/recent_files.h"

#include <filesystem>
#include <string>

namespace psyight {

class FileFlow {
  public:
    FileFlow(ProjectState &project, Session &session);

    // Opens the project to start with. capture: the working directory's psyight.psyproj only, no
    // questions. Returns true on a first run (nothing to open), for the setup assistant.
    bool start(bool capture);

    void draw_menu_items(); // Inside the File menu.
    void handle_shortcuts();
    void draw();                    // The questions (modal popups); once a frame.
    void after_frame(void *window); // Native dialogs, between frames (owner: the HWND).

    // The window is closing: true when nothing is unsaved (or the operator has decided), else
    // the question opens and the close should be cancelled.
    bool ready_to_quit();
    bool quit_confirmed() const { return quit_confirmed_; }

    std::string window_title() const;

  private:
    enum class Action { none, new_project, open_path, quit };
    enum class Prompt { none, save_project, switch_track, quit, restore_project, restore_labels };

    void begin(Action action, std::filesystem::path path = {}); // Asks first when unsaved.
    void perform(Action action);
    bool open_project(const std::filesystem::path &path);
    bool save_all(Action then); // False while a Save as dialog is still to come or on failure.
    void open_prompt(Prompt prompt);

    ProjectState &project_;
    Session &session_;
    RecentFiles recent_projects_{RecentFiles::kProjectsFile, "projects"};
    Action action_ = Action::none; // Waiting for the question or the Save as dialog.
    std::filesystem::path action_path_;
    Prompt prompt_ = Prompt::none;
    bool popup_open_requested_ = false;
    bool restore_project_pending_ = false;
    bool open_dialog_requested_ = false;
    bool save_as_requested_ = false;
    Action after_save_as_ = Action::none;
    bool quit_confirmed_ = false;
    bool capture_ = false; // Capture mode: no questions, no dialogs.
};

} // namespace psyight
