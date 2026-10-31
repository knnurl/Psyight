// The media player (SPEC 9.5): Library and Queue buttons on the menu bar drop down the library
// (its folders and rekordbox playlists beside the queue) and the queue; previous, next and the
// queue on the transport too.
// - Library: folders (the operator's, scanned on a worker thread into io/media_library.h's index,
//   psyight.library.json) and rekordbox playlists (io/rekordbox.h) in a tree; a searchable,
//   sortable table of the tracks shown; double-click plays a track now, the context menu plays
//   it next or adds it (or every track shown) to the queue.
// - Queue (beside the library, and dropped down from the menu bar and the transport): the tracks
//   in order, the playing one marked; drag to reorder, double-click to jump, remove, clear,
//   shuffle the rest. While a queued track plays, the next one is loaded and
//   analysed in the background (Session::preload) and the transport continues into it without
//   a gap; when that is not ready in time, the next track opens and plays as soon as it loads.
// The folders, the rekordbox file and the queue persist in the user settings.
#pragma once

#include "gui/session.h"
#include "io/media_library.h"
#include "io/playlist_file.h"
#include "io/rekordbox.h"
#include "io/user_settings.h"

#include <atomic>
#include <filesystem>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace psyight {

class Player {
  public:
    explicit Player(Session &session);
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    void apply_settings(const UserSettings &settings); // Also loads the index and rescans.
    void store_settings(UserSettings &settings) const;

    void update(); // Once a frame, before the panels: scans, the queue's advance, preloading.
    // The menu bar's Library and Queue buttons, each dropping down its window: the library
    // (folders and playlists, tracks, and the queue) sized to most of the app, or the queue.
    void draw_menu_buttons();
    // The queue's buttons and list, for the library and the queue dropdowns.
    void draw_queue_contents();
    int queue_size() const;
    void after_frame(void *owner); // Its file dialogs, outside the frame (owner: the window).

    // The transport's previous and next.
    bool has_previous() const;
    bool has_next() const;
    void previous();
    void next();

  private:
    struct Row {
        std::filesystem::path path;
        std::string title;
        std::string artist;
        std::string key;
        std::string folder;
        double bpm = 0.0;
        double duration_s = 0.0;
    };
    // What the tree has selected: everything, a library folder, or a rekordbox playlist.
    struct Selection {
        enum class Kind { all, folder, playlist, file } kind = Kind::all; // file: imported.
        std::string root;   // Folder: its library root.
        std::string folder; // Folder: under the root ('/' separated; empty: the root itself).
        size_t playlist = 0;
        bool operator==(const Selection &) const = default;
    };

    void start_scan();
    void load_rekordbox();
    void load_playlist_files(); // Reads them and matches their tracks to the library.
    void rebuild_rows();
    Row row_for(const std::filesystem::path &path) const;
    void draw_library_contents();
    void draw_tree();
    void draw_table();
    void play_now(const std::filesystem::path &path);
    void play_next(const std::filesystem::path &path);
    void add(const std::filesystem::path &path);
    void go(int index); // Plays queue_[index].

    Session &session_;
    std::vector<std::string> folders_; // UTF-8.
    MediaLibrary library_;
    std::map<std::filesystem::path, size_t> by_path_; // Into library_.tracks.
    std::future<MediaLibrary> scan_;
    std::atomic<int> scanned_{0};
    std::atomic<bool> cancel_{false};
    std::string rekordbox_file_;
    RekordboxLibrary rekordbox_;
    std::map<std::filesystem::path, size_t> rekordbox_by_path_; // Into rekordbox_.tracks.
    std::string rekordbox_status_;
    // Imported playlist files, their tracks matched to files (by path, else by title and artist
    // in the library); `missing` counts the rows found nowhere.
    struct Imported {
        std::string file; // UTF-8.
        std::string name;
        std::vector<std::filesystem::path> tracks;
        int missing = 0;
        std::string error;
    };
    std::vector<std::string> playlist_files_;
    std::vector<Imported> imported_;
    bool playlist_requested_ = false;

    Selection selection_;
    std::string search_;
    std::vector<Row> rows_;
    bool rows_dirty_ = true;
    int sort_column_ = -1; // None: the source's order.
    bool sort_descending_ = false;
    int selected_row_ = -1;
    bool folder_requested_ = false;
    bool rekordbox_requested_ = false;

    std::vector<std::filesystem::path> queue_;
    int current_ = -1;                 // In queue_; -1: not playing from the queue.
    std::filesystem::path seen_track_; // The session's track last frame.
    bool was_playing_ = false;
};

} // namespace psyight
