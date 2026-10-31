#include "gui/player.h"

#include "core/paths.h"
#include "gui/file_dialog.h"
#include "gui/icons.h"
#include "gui/theme.h"
#include "gui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>
#include <random>
#include <set>

namespace psyight {

namespace {

constexpr const char *kIndexFile = "psyight.library.json"; // In the working directory.

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string length_text(double s) {
    if (s <= 0.0) {
        return "";
    }
    const auto whole = static_cast<long long>(std::lround(s));
    char text[32];
    std::snprintf(text, sizeof(text), "%lld:%02lld", whole / 60, whole % 60);
    return text;
}

// A folder tree from '/'-separated folder names.
struct FolderNode {
    std::map<std::string, FolderNode> children;
};

// A button's label: the icon, then the text (or only the icon, with an id).
std::string icon_text(const char *icon, const char *text) {
    return std::string(icon) + "  " + text;
}
std::string icon_only(const char *icon, const char *id) { return std::string(icon) + "###" + id; }

} // namespace

Player::Player(Session &session) : session_(session) {}

Player::~Player() {
    cancel_ = true;
    if (scan_.valid()) {
        scan_.wait();
    }
}

void Player::apply_settings(const UserSettings &settings) {
    folders_ = settings.library_folders;
    rekordbox_file_ = settings.rekordbox_xml;
    queue_.clear();
    for (const std::string &p : settings.queue) {
        queue_.push_back(utf8_to_path(p));
    }
    current_ = settings.queue_index >= 0 && settings.queue_index < static_cast<int>(queue_.size())
                   ? settings.queue_index
                   : -1;
    if (Result<MediaLibrary> index = load_library_index(kIndexFile); index) {
        library_ = std::move(index.value());
    }
    by_path_.clear();
    for (size_t i = 0; i < library_.tracks.size(); ++i) {
        by_path_[library_.tracks[i].path] = i;
    }
    load_rekordbox();
    start_scan(); // Refresh in the background: new, changed and removed files.
    rows_dirty_ = true;
}

void Player::store_settings(UserSettings &settings) const {
    settings.library_folders = folders_;
    settings.rekordbox_xml = rekordbox_file_;
    settings.queue.clear();
    for (const std::filesystem::path &p : queue_) {
        settings.queue.push_back(path_to_utf8(p));
    }
    settings.queue_index = current_;
}

void Player::start_scan() {
    if (scan_.valid() || folders_.empty()) {
        return;
    }
    std::vector<std::filesystem::path> roots;
    for (const std::string &f : folders_) {
        roots.push_back(utf8_to_path(f));
    }
    scanned_ = 0;
    cancel_ = false;
    scan_ = std::async(std::launch::async, [this, roots, previous = library_] {
        return scan_library(roots, previous, &scanned_, &cancel_);
    });
}

void Player::load_rekordbox() {
    rekordbox_ = {};
    rekordbox_by_path_.clear();
    rekordbox_status_.clear();
    if (rekordbox_file_.empty()) {
        return;
    }
    Result<RekordboxLibrary> read = read_rekordbox_xml(utf8_to_path(rekordbox_file_));
    if (!read) {
        rekordbox_status_ = read.error().message;
        return;
    }
    rekordbox_ = std::move(read.value());
    for (size_t i = 0; i < rekordbox_.tracks.size(); ++i) {
        rekordbox_by_path_[rekordbox_.tracks[i].path] = i;
    }
    rekordbox_status_ = std::to_string(rekordbox_.playlists.size()) + " playlists";
}

Player::Row Player::row_for(const std::filesystem::path &path) const {
    Row row;
    row.path = path;
    const TrackTags *tags = nullptr;
    if (const auto it = by_path_.find(path); it != by_path_.end()) {
        const LibraryTrack &t = library_.tracks[it->second];
        tags = &t.tags;
        row.folder = t.folder;
    } else if (const auto r = rekordbox_by_path_.find(path); r != rekordbox_by_path_.end()) {
        tags = &rekordbox_.tracks[r->second].tags;
    }
    if (row.folder.empty()) {
        row.folder = path_to_utf8(path.parent_path().filename());
    }
    if (tags != nullptr) {
        row.title = tags->title;
        row.artist = tags->artist;
        row.key = tags->key;
        row.bpm = tags->bpm;
        row.duration_s = tags->duration_s;
    }
    if (row.title.empty()) {
        row.title = path_to_utf8(path.stem());
    }
    return row;
}

void Player::rebuild_rows() {
    rows_.clear();
    selected_row_ = -1;
    std::vector<std::filesystem::path> paths;
    switch (selection_.kind) {
    case Selection::Kind::all:
        for (const LibraryTrack &t : library_.tracks) {
            paths.push_back(t.path);
        }
        break;
    case Selection::Kind::folder: {
        const std::string prefix = selection_.folder.empty() ? "" : selection_.folder + "/";
        for (const LibraryTrack &t : library_.tracks) {
            if (t.root == selection_.root &&
                (selection_.folder.empty() || t.folder == selection_.folder ||
                 t.folder.rfind(prefix, 0) == 0)) {
                paths.push_back(t.path);
            }
        }
        break;
    }
    case Selection::Kind::playlist:
        if (selection_.playlist < rekordbox_.playlists.size()) {
            paths = rekordbox_.playlists[selection_.playlist].tracks;
        }
        break;
    }
    const std::string needle = lower(search_);
    for (const std::filesystem::path &p : paths) {
        Row row = row_for(p);
        if (!needle.empty() && lower(row.title + " " + row.artist + " " + row.folder + " " +
                                     path_to_utf8(p.filename()))
                                       .find(needle) == std::string::npos) {
            continue;
        }
        rows_.push_back(std::move(row));
    }
    if (sort_column_ >= 0) {
        const auto key = [&](const Row &a, const Row &b) {
            switch (sort_column_) {
            case 0:
                return lower(a.title) < lower(b.title);
            case 1:
                return lower(a.artist) < lower(b.artist);
            case 2:
                return a.bpm < b.bpm;
            case 3:
                return a.key < b.key;
            case 4:
                return a.duration_s < b.duration_s;
            default:
                return lower(a.folder) < lower(b.folder);
            }
        };
        std::stable_sort(rows_.begin(), rows_.end(), [&](const Row &a, const Row &b) {
            return sort_descending_ ? key(b, a) : key(a, b);
        });
    }
    rows_dirty_ = false;
}

void Player::update() {
    if (scan_.valid() && scan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        library_ = scan_.get();
        by_path_.clear();
        for (size_t i = 0; i < library_.tracks.size(); ++i) {
            by_path_[library_.tracks[i].path] = i;
        }
        if (const Result<void> saved = save_library_index(kIndexFile, library_); !saved) {
            spdlog::warn("Library index not saved: {}", saved.error().message);
        }
        spdlog::info("Library: {} tracks", library_.tracks.size());
        rows_dirty_ = true;
    }

    // Follow the session: a gapless switch, or a track opened from elsewhere.
    const std::filesystem::path playing =
        session_.has_track() ? session_.track().path : std::filesystem::path();
    if (playing != seen_track_) {
        seen_track_ = playing;
        if (current_ + 1 < static_cast<int>(queue_.size()) && current_ >= 0 &&
            queue_[static_cast<size_t>(current_ + 1)] == playing) {
            ++current_; // Played on into the next one.
        } else if (current_ < 0 || current_ >= static_cast<int>(queue_.size()) ||
                   queue_[static_cast<size_t>(current_)] != playing) {
            const auto it = std::find(queue_.begin(), queue_.end(), playing);
            current_ = it == queue_.end() ? -1 : static_cast<int>(it - queue_.begin());
        }
    }

    // While a queued track plays, the next one gets ready.
    const bool on_queue = current_ >= 0 && session_.has_track() && !session_.loading() &&
                          queue_[static_cast<size_t>(current_)] == playing;
    if (on_queue && has_next()) {
        session_.preload(queue_[static_cast<size_t>(current_ + 1)]);
    } else if (!session_.preload_path().empty() && (!on_queue || !has_next())) {
        session_.preload({});
    }

    // The track ended without a gapless switch (the next was not ready): open the next one.
    Transport &transport = session_.transport();
    const bool now_playing = transport.playing();
    if (on_queue && was_playing_ && !now_playing && has_next() && transport.is_open() &&
        transport.position() >= transport.buffer().frames()) {
        go(current_ + 1);
    }
    was_playing_ = now_playing;
}

bool Player::has_previous() const { return current_ > 0; }
bool Player::has_next() const {
    return current_ >= 0 && current_ + 1 < static_cast<int>(queue_.size());
}

void Player::previous() {
    if (has_previous()) {
        go(current_ - 1);
    }
}

void Player::next() {
    if (has_next()) {
        go(current_ + 1);
    }
}

void Player::go(int index) {
    if (index < 0 || index >= static_cast<int>(queue_.size())) {
        return;
    }
    current_ = index;
    session_.preload({});
    session_.open_file(queue_[static_cast<size_t>(index)], true);
}

void Player::play_now(const std::filesystem::path &path) {
    const auto at = static_cast<std::ptrdiff_t>(current_ + 1);
    queue_.insert(queue_.begin() +
                      std::min<std::ptrdiff_t>(at, static_cast<std::ptrdiff_t>(queue_.size())),
                  path);
    go(static_cast<int>(
        std::min<std::ptrdiff_t>(at, static_cast<std::ptrdiff_t>(queue_.size()) - 1)));
}

void Player::play_next(const std::filesystem::path &path) {
    const auto at =
        std::min<std::ptrdiff_t>(current_ + 1, static_cast<std::ptrdiff_t>(queue_.size()));
    queue_.insert(queue_.begin() + at, path);
}

void Player::add(const std::filesystem::path &path) { queue_.push_back(path); }

void Player::draw_library() {
    if (!ImGui::Begin("Library")) {
        ImGui::End();
        return;
    }
    const float em = ImGui::GetFontSize();
    if (ImGui::Button(icon_text(kIconFolderAdd, "Add folder...").c_str())) {
        folder_requested_ = true;
    }
    ImGui::SetItemTooltip("A music folder; its subfolders are included.");
    ImGui::SameLine();
    ImGui::BeginDisabled(scan_.valid() || folders_.empty());
    if (ImGui::Button("Rescan")) {
        start_scan();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("rekordbox XML...")) {
        rekordbox_requested_ = true;
    }
    ImGui::SetItemTooltip("rekordbox: File > Export Collection in xml format; its playlists "
                          "appear in the tree. Choose it again after a new export.");
    ImGui::SameLine();
    if (scan_.valid()) {
        ImGui::TextDisabled("Scanning: %d files...", scanned_.load());
    } else {
        ImGui::TextDisabled(
            "%zu tracks%s%s", library_.tracks.size(),
            rekordbox_status_.empty() ? "" : ", rekordbox: ", rekordbox_status_.c_str());
    }

    if (ImGui::BeginTable("##library", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                          ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("tree", ImGuiTableColumnFlags_WidthFixed, 14.0f * em);
        ImGui::TableSetupColumn("tracks", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginChild("##tree");
        draw_tree();
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##search", "Search title, artist, folder", &search_)) {
            rows_dirty_ = true;
        }
        if (rows_dirty_) {
            rebuild_rows();
        }
        ImGui::BeginDisabled(rows_.empty());
        if (ImGui::SmallButton("Add all shown to the queue")) {
            for (const Row &r : rows_) {
                add(r.path);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu shown. Double-click plays now; right-click for more.",
                            rows_.size());
        draw_table();
        ImGui::EndTable();
    }
    ImGui::End();
}

void Player::after_frame(void *owner) {
    if (folder_requested_) {
        folder_requested_ = false;
        if (const auto chosen = pick_folder_dialog(owner); chosen) {
            const std::string f = path_to_utf8(*chosen);
            if (std::find(folders_.begin(), folders_.end(), f) == folders_.end()) {
                folders_.push_back(f);
                start_scan();
            }
        }
    }
    if (rekordbox_requested_) {
        rekordbox_requested_ = false;
        if (const auto chosen = open_rekordbox_file_dialog(owner); chosen) {
            rekordbox_file_ = path_to_utf8(*chosen);
            load_rekordbox();
            rows_dirty_ = true;
        }
    }
}

void Player::draw_tree() {
    const auto pick = [&](const Selection &s) {
        if (!(selection_ == s)) {
            selection_ = s;
            rows_dirty_ = true;
        }
    };
    const ImGuiTreeNodeFlags leaf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                    ImGuiTreeNodeFlags_SpanAvailWidth;
    ImGui::TreeNodeEx(
        "All tracks",
        leaf | (selection_.kind == Selection::Kind::all ? ImGuiTreeNodeFlags_Selected : 0));
    if (ImGui::IsItemClicked()) {
        pick({});
    }
    // The library folders, each a tree of its subfolders.
    std::optional<size_t> remove;
    for (size_t r = 0; r < folders_.size(); ++r) {
        const std::string &root = folders_[r];
        FolderNode tree;
        for (const LibraryTrack &t : library_.tracks) {
            if (t.root != root || t.folder.empty()) {
                continue;
            }
            FolderNode *node = &tree;
            size_t from = 0;
            while (from <= t.folder.size()) {
                const size_t slash = t.folder.find('/', from);
                const std::string part = t.folder.substr(
                    from, slash == std::string::npos ? std::string::npos : slash - from);
                node = &node->children[part];
                if (slash == std::string::npos) {
                    break;
                }
                from = slash + 1;
            }
        }
        ImGui::PushID(static_cast<int>(r));
        const bool root_selected = selection_.kind == Selection::Kind::folder &&
                                   selection_.root == root && selection_.folder.empty();
        const bool open =
            ImGui::TreeNodeEx(path_to_utf8(utf8_to_path(root).filename()).c_str(),
                              ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                  (root_selected ? ImGuiTreeNodeFlags_Selected : 0) |
                                  (tree.children.empty() ? ImGuiTreeNodeFlags_Leaf : 0));
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            pick({Selection::Kind::folder, root, "", 0});
        }
        if (ImGui::BeginPopupContextItem()) {
            ImGui::TextDisabled("%s", root.c_str());
            if (ImGui::MenuItem("Remove from the library")) {
                remove = r;
            }
            ImGui::EndPopup();
        }
        if (open) {
            const auto draw = [&](const auto &self, const FolderNode &node,
                                  const std::string &prefix) -> void {
                for (const auto &[name, child] : node.children) {
                    const std::string full = prefix.empty() ? name : prefix + "/" + name;
                    const bool selected = selection_.kind == Selection::Kind::folder &&
                                          selection_.root == root && selection_.folder == full;
                    const bool child_open = ImGui::TreeNodeEx(
                        full.c_str(),
                        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                            (selected ? ImGuiTreeNodeFlags_Selected : 0) |
                            (child.children.empty() ? ImGuiTreeNodeFlags_Leaf : 0),
                        "%s", name.c_str());
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        pick({Selection::Kind::folder, root, full, 0});
                    }
                    if (child_open) {
                        self(self, child, full);
                        ImGui::TreePop();
                    }
                }
            };
            draw(draw, tree, "");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove) {
        folders_.erase(folders_.begin() + static_cast<std::ptrdiff_t>(*remove));
        std::erase_if(library_.tracks, [&](const LibraryTrack &t) {
            return std::find(folders_.begin(), folders_.end(), t.root) == folders_.end();
        });
        by_path_.clear();
        for (size_t i = 0; i < library_.tracks.size(); ++i) {
            by_path_[library_.tracks[i].path] = i;
        }
        selection_ = {};
        rows_dirty_ = true;
    }
    // rekordbox: its folders, then playlists.
    if (!rekordbox_.playlists.empty() &&
        ImGui::TreeNodeEx("rekordbox", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        std::map<std::string, std::vector<size_t>> by_folder;
        for (size_t i = 0; i < rekordbox_.playlists.size(); ++i) {
            by_folder[rekordbox_.playlists[i].folder].push_back(i);
        }
        for (const auto &[folder, lists] : by_folder) {
            const bool nested = !folder.empty();
            if (nested && !ImGui::TreeNodeEx(folder.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                continue;
            }
            for (const size_t i : lists) {
                const bool selected =
                    selection_.kind == Selection::Kind::playlist && selection_.playlist == i;
                ImGui::PushID(static_cast<int>(i));
                ImGui::TreeNodeEx(rekordbox_.playlists[i].name.c_str(),
                                  leaf | (selected ? ImGuiTreeNodeFlags_Selected : 0));
                if (ImGui::IsItemClicked()) {
                    pick({Selection::Kind::playlist, "", "", i});
                }
                ImGui::PopID();
            }
            if (nested) {
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }
    if (folders_.empty()) {
        ImGui::TextDisabled("Add a music folder to start.");
    }
}

void Player::draw_table() {
    const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Reorderable |
                                  ImGuiTableFlags_Hideable | ImGuiTableFlags_SortTristate;
    if (!ImGui::BeginTable("##tracks", 6, flags, ImGui::GetContentRegionAvail())) {
        return;
    }
    const float em = ImGui::GetFontSize();
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 3.5f * em);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 3.0f * em);
    ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 3.5f * em);
    ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
        if (specs->SpecsCount > 0) {
            sort_column_ = specs->Specs[0].ColumnIndex;
            sort_descending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        } else {
            sort_column_ = -1;
        }
        specs->SpecsDirty = false;
        rebuild_rows();
    }
    const std::filesystem::path playing =
        session_.has_track() ? session_.track().path : std::filesystem::path();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const Row &row = rows_[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableNextColumn();
            const bool is_playing = row.path == playing;
            if (is_playing) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme().accent);
            }
            if (ImGui::Selectable(row.title.c_str(), selected_row_ == i,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_row_ = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    play_now(row.path);
                }
            }
            if (is_playing) {
                ImGui::PopStyleColor();
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Play now")) {
                    play_now(row.path);
                }
                if (ImGui::MenuItem("Play next")) {
                    play_next(row.path);
                }
                if (ImGui::MenuItem("Add to the queue")) {
                    add(row.path);
                }
                ImGui::Separator();
                ImGui::TextDisabled("%s", path_to_utf8(row.path).c_str());
                ImGui::EndPopup();
            }
            if (ImGui::BeginDragDropSource()) { // Onto the queue.
                const std::string p = path_to_utf8(row.path);
                ImGui::SetDragDropPayload("PSYIGHT_TRACK", p.c_str(), p.size() + 1);
                ImGui::TextUnformatted(row.title.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.artist.c_str());
            ImGui::TableNextColumn();
            if (row.bpm > 0.0) {
                mono_text("%.0f", row.bpm);
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.key.c_str());
            ImGui::TableNextColumn();
            mono_text("%s", length_text(row.duration_s).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", row.folder.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void Player::draw_queue() {
    if (ImGui::Begin("Queue")) {
        draw_queue_contents();
    }
    ImGui::End();
}

int Player::queue_size() const { return static_cast<int>(queue_.size()); }

void Player::draw_queue_contents() {
    ImGui::BeginDisabled(!has_previous());
    if (ImGui::Button(icon_only(kIconPrevious, "previous").c_str())) {
        previous();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_next());
    if (ImGui::Button(icon_only(kIconNext, "next").c_str())) {
        next();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(queue_.size() < 2);
    if (ImGui::Button(icon_only(kIconShuffle, "shuffle").c_str())) {
        // The tracks after the playing one, in a new order.
        std::mt19937 rng(std::random_device{}());
        std::shuffle(queue_.begin() + (current_ + 1), queue_.end(), rng);
        session_.preload({});
    }
    ImGui::SetItemTooltip("Shuffle the tracks after the playing one.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(queue_.empty());
    if (ImGui::Button("Clear")) {
        queue_.clear();
        current_ = -1;
        session_.preload({});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (has_next()) {
        const std::filesystem::path &next = queue_[static_cast<size_t>(current_ + 1)];
        ImGui::TextDisabled("Next: %s", session_.preload_ready() && session_.preload_path() == next
                                            ? "ready, no gap"
                                            : "analysing...");
    }

    std::optional<std::pair<size_t, size_t>> move;
    std::optional<size_t> remove;
    std::optional<std::pair<size_t, std::filesystem::path>> insert;
    ImGui::BeginChild("##queue");
    for (size_t i = 0; i < queue_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const Row row = row_for(queue_[i]);
        const bool current = static_cast<int>(i) == current_;
        if (current) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme().accent);
        }
        char label[512];
        std::snprintf(label, sizeof(label), "%zu  %s%s%s", i + 1, row.title.c_str(),
                      row.artist.empty() ? "" : "  -  ", row.artist.c_str());
        const float right = ImGui::GetContentRegionAvail().x - 5.0f * ImGui::GetFontSize();
        if (ImGui::Selectable(label, current, ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(right, 0.0f)) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            go(static_cast<int>(i));
        }
        if (current) {
            ImGui::PopStyleColor();
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("PSYIGHT_QUEUE", &i, sizeof(i));
            ImGui::TextUnformatted(row.title.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("PSYIGHT_QUEUE")) {
                move = std::pair{*static_cast<const size_t *>(p->Data), i};
            }
            if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("PSYIGHT_TRACK")) {
                insert = std::pair{i, utf8_to_path(static_cast<const char *>(p->Data))};
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        mono_text("%6s", length_text(row.duration_s).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(icon_only(kIconRemove, "remove").c_str())) {
            remove = i;
        }
        ImGui::PopID();
    }
    if (queue_.empty()) {
        ImGui::TextDisabled("Empty: double-click a track in the Library, or drag tracks here.");
    }
    ImGui::Dummy(ImGui::GetContentRegionAvail()); // A drop target below the list.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("PSYIGHT_TRACK")) {
            insert = std::pair{queue_.size(), utf8_to_path(static_cast<const char *>(p->Data))};
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndChild();

    // Edits after the list, keeping current_ on the same track.
    const std::filesystem::path playing_path =
        current_ >= 0 ? queue_[static_cast<size_t>(current_)] : std::filesystem::path();
    bool changed = false;
    if (move && move->first != move->second) {
        const std::filesystem::path moved = queue_[move->first];
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(move->first));
        queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(move->second), moved);
        changed = true;
    }
    if (remove) {
        if (static_cast<int>(*remove) == current_) {
            current_ = -1; // It plays on, outside the queue.
        }
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(*remove));
        changed = true;
    }
    if (insert) {
        queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(insert->first), insert->second);
        changed = true;
    }
    if (changed && current_ >= 0) {
        const auto it = std::find(queue_.begin(), queue_.end(), playing_path);
        current_ = it == queue_.end() ? -1 : static_cast<int>(it - queue_.begin());
    }
}

} // namespace psyight
