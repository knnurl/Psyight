#include "gui/labelling_view.h"

#include "core/paths.h"
#include "gui/file_dialog.h"
#include "gui/icons.h"
#include "gui/theme.h"
#include "gui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>

namespace psyight {

namespace {

constexpr float kLibraryWidthEm = 22.0f;
constexpr float kMinZoomBars = 8.0f;
constexpr float kMaxZoomBars = 128.0f;
constexpr float kHandleGrabPixels = 5.0f;
constexpr float kPanStartPixels = 3.0f; // A press moving less is a click.

ImU32 section_colour(const std::string &label, float alpha = 1.0f) {
    const ThemeTokens &t = theme();
    const ImVec4 c = label == "groove"      ? t.section_groove
                     : label == "breakdown" ? t.section_breakdown
                     : label == "build"     ? t.section_build
                     : label == "drop"      ? t.section_drop
                                            : t.section_other;
    return to_u32(with_alpha(c, c.w * alpha));
}

std::string format_time(double seconds) {
    const int total_ms = static_cast<int>(std::lround(std::max(0.0, seconds) * 1000.0));
    char text[32];
    std::snprintf(text, sizeof(text), "%d:%02d.%03d", total_ms / 60000, total_ms / 1000 % 60,
                  total_ms % 1000);
    return text;
}

ImU32 status_colour(LabelStatus status) {
    switch (status) {
    case LabelStatus::none:
        return to_u32(theme().text_dim);
    case LabelStatus::started:
        return to_u32(theme().warning);
    case LabelStatus::done:
        return to_u32(theme().success);
    }
    return to_u32(theme().text_dim);
}

const char *filter_id(int index) {
    static const char *const kIds[] = {"all", "none", "started", "done"};
    return kIds[std::clamp(index, 0, 3)];
}

bool passes(const TrackEntry &t, const std::string &filter, const std::string &search) {
    if (filter == "none" && t.status != LabelStatus::none) {
        return false;
    }
    if (filter == "started" && t.status != LabelStatus::started) {
        return false;
    }
    if (filter == "done" && t.status != LabelStatus::done) {
        return false;
    }
    if (search.empty()) {
        return true;
    }
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string needle = lower(search);
    return lower(t.name).find(needle) != std::string::npos ||
           lower(t.folder).find(needle) != std::string::npos;
}

} // namespace

void LabellingView::apply_settings(const UserSettings &settings) {
    folders_.clear();
    for (const std::string &f : settings.label_folders) {
        folders_.push_back(utf8_to_path(f));
    }
    filter_ = settings.label_filter;
    scanned_ = false;
}

void LabellingView::store_settings(UserSettings &settings) const {
    settings.label_folders.clear();
    for (const std::filesystem::path &f : folders_) {
        settings.label_folders.push_back(path_to_utf8(f));
    }
    settings.label_filter = filter_;
}

void LabellingView::after_frame(void *owner) {
    if (!folder_requested_) {
        return;
    }
    folder_requested_ = false;
    if (const auto chosen = pick_folder_dialog(owner); chosen) {
        if (std::find(folders_.begin(), folders_.end(), *chosen) == folders_.end()) {
            folders_.push_back(*chosen);
        }
        scanned_ = false;
    }
}

void LabellingView::rescan() {
    tracks_ = scan_track_folders(folders_);
    scanned_ = true;
    int done = 0;
    for (const TrackEntry &t : tracks_) {
        done += t.status == LabelStatus::done;
    }
    char text[96];
    std::snprintf(text, sizeof(text), "%zu track%s, %d done.", tracks_.size(),
                  tracks_.size() == 1 ? "" : "s", done);
    message_ = text;
}

void LabellingView::open_track(const std::filesystem::path &path) {
    if (session_.has_track() && session_.track().path == path) {
        return;
    }
    session_.open_file(path); // Asks first (Save, Don't save, Cancel) when labels are unsaved.
}

double LabellingView::playhead_s() const { return session_.playhead_s(); }

void LabellingView::seek(double t_s) {
    if (!session_.has_track()) {
        return;
    }
    const LoadedTrack &track = session_.track();
    t_s = std::clamp(t_s, 0.0, track.duration_s);
    session_.transport().seek(seconds_to_samples(t_s, track.sample_rate_hz));
}

bool LabellingView::sections_labelled() const {
    const Labels &l = session_.labels().labels();
    return l.sections_edited && !l.sections.empty();
}

std::vector<SectionLabel> LabellingView::working_sections() const {
    if (sections_labelled() || !session_.has_track()) {
        return session_.labels().labels().sections;
    }
    const TrackAnalysis *a = session_.track().track_analysis.get();
    return a != nullptr ? sections_from_detection(a->sections, session_.labels().labels().grid)
                        : std::vector<SectionLabel>{};
}

void LabellingView::commit(std::vector<SectionLabel> sections) {
    session_.labels().set_sections(std::move(sections), true);
}

LabelPhrases LabellingView::effective_phrases() const {
    const Labels &l = session_.labels().labels();
    if (l.phrases.set || !session_.has_track()) {
        return l.phrases;
    }
    LabelPhrases detected;
    const TrackAnalysis *a = session_.track().track_analysis.get();
    if (a == nullptr || a->beats.bpm <= 0.0) {
        return detected;
    }
    const double bar_s = 4.0 * 60.0 / a->beats.bpm;
    detected.set = true;
    detected.bars = 8;
    detected.start_s =
        samples_to_seconds(a->beats.grid.first_downbeat, a->beats.grid.sample_rate_hz) +
        a->beats.phrase_bar_offset * bar_s;
    detected.start_s = snap_to_bar(l.grid, detected.start_s); // On the labelled grid's bars.
    return detected;
}

double LabellingView::snapped(double t_s) const {
    const ImGuiIO &io = ImGui::GetIO();
    const Labels &l = session_.labels().labels();
    if (io.KeyAlt) {
        return t_s;
    }
    return io.KeyShift ? snap_to_phrase(l.grid, effective_phrases(), t_s)
                       : snap_to_bar(l.grid, t_s);
}

void LabellingView::mark(int label) {
    const double bar_s = bar_length_s(session_.labels().labels().grid);
    const double tolerance = bar_s > 0.0 ? 0.5 * bar_s : 0.5;
    commit(
        mark_section(working_sections(), snapped(playhead_s()), kSectionNames[label], tolerance));
}

Result<void> LabellingView::save() {
    Result<void> saved = session_.save_labels();
    if (!saved) {
        message_ = "Not saved: " + saved.error().message;
        return saved;
    }
    message_ = "Saved.";
    if (session_.has_track()) {
        for (TrackEntry &t : tracks_) {
            if (t.path == session_.track().path) {
                t.status = label_status(t.path, &t.sections);
            }
        }
    }
    return saved;
}

void LabellingView::draw(bool *open) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    if (ImGui::GetFrameCount() > last_frame_ + 1) {
        ImGui::SetNextWindowFocus(); // Just opened: take the keys.
    }
    last_frame_ = ImGui::GetFrameCount();
    ImGui::Begin("Label sections", nullptr, flags);
    if (!scanned_) {
        rescan();
    }
    if (session_.has_track() && seen_track_ != &session_.track()) {
        seen_track_ = &session_.track();
        dragging_ = -1;
        for (TrackEntry &t : tracks_) { // The track left behind may have been saved.
            t.status = label_status(t.path, &t.sections);
        }
    }
    const float em = ImGui::GetFontSize();
    const float library = std::min(kLibraryWidthEm * em, 0.32f * ImGui::GetContentRegionAvail().x);
    ImGui::BeginChild("##library", ImVec2(library, 0.0f), ImGuiChildFlags_Borders);
    draw_library(library);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##track", ImVec2(0.0f, 0.0f));
    draw_track();
    ImGui::EndChild();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            *open = false;
        }
        handle_keys();
    }
    ImGui::End();
}

void LabellingView::draw_library(float width) {
    const float em = ImGui::GetFontSize();
    ImGui::TextUnformatted("Tracks");
    ImGui::SameLine(width - 12.0f * em);
    if (ImGui::SmallButton("Add folder...")) {
        folder_requested_ = true;
    }
    ImGui::SetItemTooltip("A folder of music (subfolders included). Remembered.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) {
        rescan();
    }
    int remove = -1;
    for (size_t i = 0; i < folders_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(kIconRemove)) {
            remove = static_cast<int>(i);
        }
        ImGui::SetItemTooltip("Remove this folder from the list (no files are touched).");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", path_to_utf8(folders_[i]).c_str());
        ImGui::PopID();
    }
    if (remove >= 0) {
        folders_.erase(folders_.begin() + remove);
        rescan();
    }
    if (folders_.empty()) {
        ImGui::TextWrapped("Add a folder of music to list its tracks here.");
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", "Search names and folders", &search_);
    static const char *const kFilters[] = {"All tracks", "Not started", "In progress", "Done"};
    int filter = 0;
    for (int k = 0; k < 4; ++k) {
        filter = filter_ == filter_id(k) ? k : filter;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##filter", &filter, kFilters, 4)) {
        filter_ = filter_id(filter);
    }
    if (!message_.empty()) {
        ImGui::TextDisabled("%s", message_.c_str());
    }
    const std::filesystem::path current =
        session_.has_track() ? session_.track().path : std::filesystem::path();
    const float list_height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginTable("##tracks", 2,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0f, std::max(4.0f * em, list_height)))) {
        ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthFixed, 0.9f * em);
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
        for (size_t i = 0; i < tracks_.size(); ++i) {
            const TrackEntry &t = tracks_[i];
            if (!passes(t, filter_, search_)) {
                continue;
            }
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(at.x + 0.35f * em, at.y + 0.5f * ImGui::GetTextLineHeight()), 0.28f * em,
                status_colour(t.status));
            ImGui::TableNextColumn();
            if (ImGui::Selectable(t.name.c_str(), t.path == current,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                open_track(t.path);
            }
            ImGui::SetItemTooltip(
                "%s\n%s%s", t.folder.c_str(), label_status_name(t.status),
                t.sections > 0 ? (", " + std::to_string(t.sections) + " sections").c_str() : "");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Next unfinished (N)", ImVec2(-FLT_MIN, 0.0f))) {
        size_t from = tracks_.size();
        for (size_t i = 0; i < tracks_.size(); ++i) {
            from = tracks_[i].path == current ? i : from;
        }
        if (const auto next = next_unfinished(tracks_, from); next) {
            open_track(tracks_[*next].path);
        } else {
            message_ = "Every listed track is done.";
        }
    }
}

void LabellingView::draw_track() {
    const float em = ImGui::GetFontSize();
    if (!session_.has_track()) {
        ImGui::TextDisabled(session_.loading()
                                ? "Loading and analysing..."
                                : "Pick a track on the left (or File > Open track).");
        return;
    }
    draw_header();
    const LoadedTrack &track = session_.track();
    ImGui::Spacing();
    draw_timeline("##overview", {0.0, track.duration_s}, 5.0f * em, false);
    ImGui::Spacing();
    // The zoomed view: zoom_bars_ bars, the playhead a third of the way in.
    const double bar_s = bar_length_s(session_.labels().labels().grid);
    const double window_s = bar_s > 0.0 ? zoom_bars_ * bar_s : 60.0;
    double from = playhead_s() - window_s / 3.0;
    from = std::clamp(from, -0.02 * window_s, std::max(0.0, track.duration_s - 0.98 * window_s));
    draw_timeline("##zoom", {from, from + window_s}, 11.0f * em, true);
    // The section keys as buttons in the sections' colours: a click marks at the playhead too.
    for (int i = 0; i < 6; ++i) {
        std::string name = kSectionNames[i];
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
        const std::string text = std::to_string(i + 1) + "  " + name + "##mark";
        ImGui::PushStyleColor(ImGuiCol_Button, section_colour(kSectionNames[i], 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, section_colour(kSectionNames[i], 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, section_colour(kSectionNames[i], 1.0f));
        ImGui::PushID(i);
        if (ImGui::Button(text.c_str(), ImVec2(7.5f * em, 0.0f))) {
            mark(i);
        }
        ImGui::PopID();
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip("Key %d: %s from the nearest bar to the playhead (Shift: phrase, "
                              "Alt: exact).",
                              i + 1, kSectionNames[i]);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::SetNextItemWidth(14.0f * em);
    ImGui::SliderFloat("##zoom bars", &zoom_bars_, kMinZoomBars, kMaxZoomBars, "%.0f bars shown",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("The zoomed view's width (or the mouse wheel over it).");
    ImGui::SameLine();
    ImGui::TextDisabled("Keys: Space play; 1 to 6 sections (Shift: phrase, Alt: exact); Del "
                        "remove; P phrase; D downbeat; , . grid shift; [ ] jump; "
                        "Enter done; N next.");
    ImGui::Spacing();
    draw_grid_controls();
    ImGui::Spacing();
    draw_section_table();
}

void LabellingView::draw_header() {
    const float em = ImGui::GetFontSize();
    const LoadedTrack &track = session_.track();
    LabelEditor &labels = session_.labels();
    ImGui::PushFont(theme_font(ThemeFont::ui_medium));
    ImGui::TextUnformatted(track.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled(labels.dirty() ? "(unsaved)" : "");
    if (session_.reanalysing() || session_.loading()) {
        ImGui::SameLine();
        ImGui::TextDisabled("analysing...");
    }
    // Transport and position.
    Transport &transport = session_.transport();
    const bool playing = transport.playing();
    if (primary_button((std::string(playing ? kIconPause : kIconPlay) + "###play").c_str())) {
        playing ? transport.pause() : transport.play();
    }
    ImGui::SetItemTooltip(playing ? "Pause (Space)" : "Play (Space)");
    ImGui::SameLine();
    const double t = playhead_s();
    const Labels &l = labels.labels();
    ImGui::PushFont(theme_font(ThemeFont::timecode));
    ImGui::TextUnformatted(format_time(t).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    const LabelPhrases phrases = effective_phrases();
    const long long bar = bar_index_at(l.grid, t);
    const int in_phrase = bar_in_phrase(l.grid, phrases, t);
    const std::vector<SectionLabel> sections = working_sections();
    const int at = section_at(sections, t);
    mono_text("bar %lld%s%s  %s", bar + 1,
              in_phrase >= 0 ? (", " + std::to_string(in_phrase + 1) + " of " +
                                std::to_string(phrases.bars) + " in the phrase")
                                   .c_str()
                             : "",
              l.phrases.set ? "" : (in_phrase >= 0 ? " (detected)" : ""),
              at >= 0 ? sections[static_cast<size_t>(at)].label.c_str() : "");
    // Saving and done, at the right.
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 23.0f * em);
    bool done = l.sections_done;
    if (ImGui::Checkbox("Done (Enter)", &done)) {
        labels.set_sections_done(done);
    }
    ImGui::SetItemTooltip("Every section and the phrase grid of this track are checked: it counts "
                          "as ground truth for the section metrics.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!labels.dirty());
    if (ImGui::Button("Save")) {
        (void)save();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Save the labels (Ctrl+S): <track>.labels.json beside the audio.");
    ImGui::SameLine();
    if (primary_button("Save and next")) {
        if (save()) {
            size_t from = tracks_.size();
            for (size_t i = 0; i < tracks_.size(); ++i) {
                from = tracks_[i].path == track.path ? i : from;
            }
            if (const auto next = next_unfinished(tracks_, from); next) {
                open_track(tracks_[*next].path);
            }
        }
    }
    ImGui::SetItemTooltip("Save, then open the next track not marked done (N).");
}

void LabellingView::shift_grid(double by_s) {
    LabelEditor &labels = session_.labels();
    const Labels &l = labels.labels();
    LabelGrid grid = l.grid;
    const double bar_s = bar_length_s(grid);
    grid.first_downbeat_s += by_s;
    if (bar_s > 0.0) { // Kept within the first bar; the bar lines are the same.
        grid.first_downbeat_s -= std::floor(grid.first_downbeat_s / bar_s) * bar_s;
    }
    labels.set_grid(grid);
    if (l.phrases.set) {
        LabelPhrases p = l.phrases;
        p.start_s = std::max(0.0, p.start_s + by_s);
        labels.set_phrases(p);
    }
    if (grid_moves_sections_ && sections_labelled()) {
        std::vector<SectionLabel> sections = l.sections;
        for (size_t i = 1; i < sections.size(); ++i) { // The first stays at 0 s.
            sections[i].start_s = std::max(0.0, sections[i].start_s + by_s);
        }
        labels.set_sections(std::move(sections), true);
    }
}

void LabellingView::draw_grid_controls() {
    const float em = ImGui::GetFontSize();
    LabelEditor &labels = session_.labels();
    const Labels &l = labels.labels();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Grid");
    ImGui::SameLine();
    LabelGrid grid = l.grid;
    ImGui::SetNextItemWidth(6.0f * em);
    if (drag_double("BPM", &grid.bpm, 0.01f, 60.0, 250.0, "%.3f")) {
        labels.set_grid(grid);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(6.0f * em);
    if (drag_double("first downbeat (s)", &grid.first_downbeat_s, 0.001f, 0.0, 600.0, "%.3f")) {
        labels.set_grid(grid);
    }
    ImGui::SameLine();
    if (ImGui::Button("Downbeat here (D)")) {
        LabelGrid g = l.grid;
        const double beat_s = g.bpm > 0.0 ? 60.0 / g.bpm : 0.0;
        if (beat_s > 0.0) {
            const double beat = g.first_downbeat_s +
                                std::round((playhead_s() - g.first_downbeat_s) / beat_s) * beat_s;
            const double bar_s = 4.0 * beat_s;
            g.first_downbeat_s = beat - std::floor(beat / bar_s) * bar_s;
            labels.set_grid(g);
        }
    }
    ImGui::SetItemTooltip("The beat nearest the playhead becomes a downbeat (the grid shifts by "
                          "whole beats; the tempo stays).");
    ImGui::SameLine(0.0f, 2.0f * em);
    ImGui::TextUnformatted("Shift");
    const double beat_s = l.grid.bpm > 0.0 ? 60.0 / l.grid.bpm : 0.0;
    const struct {
        const char *text;
        double by_s;
    } shifts[] = {{"-beat", -beat_s}, {"-10 ms", -0.010}, {"-1 ms", -0.001},
                  {"+1 ms", 0.001},   {"+10 ms", 0.010},  {"+beat", beat_s}};
    for (const auto &shift : shifts) {
        ImGui::SameLine(0.0f, 0.3f * em);
        if (ImGui::SmallButton(shift.text) && shift.by_s != 0.0) {
            session_.labels().begin_gesture();
            shift_grid(shift.by_s);
            session_.labels().end_gesture();
        }
    }
    ImGui::SetItemTooltip("Moves the grid earlier or later. Keys: , and . (10 ms; Shift: 1 ms). "
                          "Ctrl and a drag on the waveform moves it with the mouse.");
    ImGui::SameLine();
    ImGui::Checkbox("Sections follow", &grid_moves_sections_);
    ImGui::SetItemTooltip("Your sections and phrase start move with the grid (they were placed on "
                          "its bars).");
    ImGui::SameLine(0.0f, 2.0f * em);
    ImGui::TextUnformatted("Phrases of");
    ImGui::SameLine();
    LabelPhrases phrases = effective_phrases();
    int sixteen = phrases.bars == 16 ? 1 : 0;
    ImGui::SetNextItemWidth(4.0f * em);
    if (ImGui::Combo("bars##phrase", &sixteen,
                     "8\0"
                     "16\0")) {
        LabelPhrases p = phrases;
        p.set = true;
        p.bars = sixteen == 1 ? 16 : 8;
        labels.set_phrases(p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Phrase starts here (P)")) {
        LabelPhrases p = phrases;
        p.set = true;
        p.start_s = snap_to_bar(l.grid, playhead_s());
        labels.set_phrases(p);
    }
    ImGui::SetItemTooltip("The bar nearest the playhead starts a phrase.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!l.phrases.set);
    if (ImGui::Button("Use detected phrases")) {
        labels.set_phrases({});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(l.phrases.set ? "labelled" : "detected");
}

void LabellingView::draw_section_table() {
    const float em = ImGui::GetFontSize();
    const Labels &l = session_.labels().labels();
    std::vector<SectionLabel> sections = working_sections();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(sections_labelled() ? "Sections"
                                               : "Sections (the detection: an edit "
                                                 "starts from it)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!sections_labelled());
    if (ImGui::SmallButton("Back to the detection")) {
        session_.labels().set_sections({}, false);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Drop the labelled sections (undoable).");
    if (!ImGui::BeginTable("##sections", 5,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingFixedFit,
                           ImVec2(0.0f, std::max(6.0f * em, ImGui::GetContentRegionAvail().y)))) {
        return;
    }
    ImGui::TableSetupColumn("Start");
    ImGui::TableSetupColumn("Bar");
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 9.0f * em);
    ImGui::TableSetupColumn("Length");
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    const double duration = session_.track().duration_s;
    const double bar_s = bar_length_s(l.grid);
    const double t = playhead_s();
    int changed_index = -1;
    std::string changed_label;
    int removed = -1;
    for (size_t i = 0; i < sections.size(); ++i) {
        const SectionLabel &s = sections[i];
        const double end = i + 1 < sections.size() ? sections[i + 1].start_s : duration;
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        if (t >= s.start_s && t < end) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                   to_u32(with_alpha(theme().accent, 0.18f)));
        }
        ImGui::TableNextColumn();
        if (ImGui::Selectable(format_time(s.start_s).c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowOverlap)) {
            seek(s.start_s);
        }
        ImGui::TableNextColumn();
        mono_text("%lld", bar_index_at(l.grid, s.start_s + 1e-6) + 1);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##label", s.label.c_str())) {
            for (const char *name : kSectionNames) {
                if (ImGui::Selectable(name, s.label == name)) {
                    changed_index = static_cast<int>(i);
                    changed_label = name;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TableNextColumn();
        if (bar_s > 0.0) {
            mono_text("%.1f bars", (end - s.start_s) / bar_s);
        } else {
            mono_text("%.1f s", end - s.start_s);
        }
        ImGui::TableNextColumn();
        if (i > 0 && ImGui::SmallButton(kIconRemove)) {
            removed = static_cast<int>(i);
        }
        if (i > 0) {
            ImGui::SetItemTooltip("Remove this boundary: the section before runs on.");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (changed_index >= 0) {
        sections[static_cast<size_t>(changed_index)].label = changed_label;
        commit(std::move(sections));
    } else if (removed > 0) {
        sections.erase(sections.begin() + removed);
        commit(std::move(sections));
    }
}

// The detailed waveform between y and y + h over [x0, x0 + width]: per pixel column, the lowest
// and highest sample, coloured by its bands (low red, mid green, high blue).
void LabellingView::draw_waveform(ImDrawList &draw, const LoadedTrack &track, float y, float h,
                                  float x0, float width,
                                  const std::function<double(float)> &t_of) const {
    const double bucket_s =
        static_cast<double>(track.detail_bucket_samples) / std::max(1, track.sample_rate_hz);
    const auto buckets = static_cast<long long>(track.detail_min.size());
    const float mid = y + 0.5f * h;
    constexpr ImVec4 kLow(1.0f, 0.32f, 0.22f, 1.0f);
    constexpr ImVec4 kMid(0.35f, 0.9f, 0.45f, 1.0f);
    constexpr ImVec4 kHigh(0.35f, 0.65f, 1.0f, 1.0f);
    for (float x = x0; x < x0 + width; x += 1.0f) {
        const auto first = static_cast<long long>(std::floor(t_of(x) / bucket_s));
        const auto last =
            std::max(first, static_cast<long long>(std::floor(t_of(x + 1.0f) / bucket_s)));
        if (last < 0 || first >= buckets) {
            continue;
        }
        float lowest = 0.0f;
        float highest = 0.0f;
        std::array<float, 3> bands{};
        for (long long b = std::max(0LL, first); b <= std::min(buckets - 1, last); ++b) {
            const auto i = static_cast<size_t>(b);
            lowest = std::min(lowest, track.detail_min[i]);
            highest = std::max(highest, track.detail_max[i]);
            for (size_t k = 0; k < 3; ++k) {
                bands[k] = std::max(bands[k], track.detail_bands[i][k]);
            }
        }
        const float sum = bands[0] + bands[1] + bands[2];
        ImVec4 c = theme().text_dim;
        if (sum > 0.0f) {
            c = ImVec4((bands[0] * kLow.x + bands[1] * kMid.x + bands[2] * kHigh.x) / sum,
                       (bands[0] * kLow.y + bands[1] * kMid.y + bands[2] * kHigh.y) / sum,
                       (bands[0] * kLow.z + bands[1] * kMid.z + bands[2] * kHigh.z) / sum, 0.85f);
        }
        draw.AddLine(ImVec2(x + 0.5f, mid - std::min(1.0f, highest) * 0.5f * h),
                     ImVec2(x + 0.5f, mid - std::max(-1.0f, lowest) * 0.5f * h + 1.0f), to_u32(c));
    }
}

void LabellingView::draw_timeline(const char *id, Span span, float height, bool lanes) {
    const LoadedTrack &track = session_.track();
    const Labels &l = session_.labels().labels();
    const float em = ImGui::GetFontSize();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float width = std::max(10.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton(id, ImVec2(width, height),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const double length = std::max(1e-6, span.to_s - span.from_s);
    const auto x_of = [&](double t) {
        return p0.x + static_cast<float>((t - span.from_s) / length) * width;
    };
    const auto t_of = [&](float x) { return span.from_s + (x - p0.x) / width * length; };
    draw->PushClipRect(p0, ImVec2(p0.x + width, p0.y + height), true);
    draw->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), to_u32(theme().frame));

    // Rows.
    const float ruler_h = ImGui::GetTextLineHeight();
    const float band_h = 1.6f * em;
    const float detected_h = 0.45f * em;
    const float ruler_y = p0.y;
    const float band_y = ruler_y + ruler_h;
    const float detected_y = band_y + band_h + 2.0f;
    const float rest_y = detected_y + detected_h + 3.0f;
    const float rest_h = p0.y + height - rest_y;

    // The bar and phrase grid.
    const LabelPhrases phrases = effective_phrases();
    const double bar_s = bar_length_s(l.grid);
    if (bar_s > 0.0) {
        const float bar_px = static_cast<float>(bar_s / length) * width;
        const long long first = bar_index_at(l.grid, span.from_s);
        const long long last = bar_index_at(l.grid, span.to_s) + 1;
        for (long long b = first; b <= last; ++b) {
            const double tb = l.grid.first_downbeat_s + static_cast<double>(b) * bar_s;
            const float x = x_of(tb);
            const int in_phrase = bar_in_phrase(l.grid, phrases, tb + 1e-6);
            const bool phrase_start = in_phrase == 0;
            // Halfway through the phrase (bar 4 of 8, 8 of 16): a line between the two.
            const bool phrase_middle = phrases.bars >= 2 && in_phrase == phrases.bars / 2;
            if (!phrase_start && !phrase_middle && bar_px < 4.0f) {
                continue;
            }
            const bool strong = phrase_start || phrase_middle;
            const float alpha = phrase_start ? 0.9f : phrase_middle ? 0.5f : 0.18f;
            draw->AddLine(
                ImVec2(x, ruler_y + (strong ? 0.0f : 0.55f * ruler_h)), ImVec2(x, p0.y + height),
                to_u32(with_alpha(strong ? theme().phrase_tick : theme().text_dim, alpha)),
                phrase_start ? 1.5f : 1.0f);
            if (strong && 4.0f * bar_px > 2.2f * em) {
                char text[16];
                std::snprintf(text, sizeof(text), "%lld", b + 1);
                draw->AddText(ImVec2(x + 2.0f, ruler_y),
                              to_u32(with_alpha(theme().text_dim, phrase_start ? 1.0f : 0.6f)),
                              text);
            }
        }
    }

    // Lanes: the waveform (overview) or the per-bar features (zoom).
    if (!lanes && !track.overview_time_s.empty()) {
        const float mid = rest_y + 0.5f * rest_h;
        for (size_t i = 0; i < track.overview_time_s.size(); ++i) {
            const double tt = track.overview_time_s[i];
            if (tt < span.from_s || tt > span.to_s) {
                continue;
            }
            const float x = x_of(tt);
            draw->AddLine(
                ImVec2(x, mid - static_cast<float>(track.overview_max[i]) * 0.5f * rest_h),
                ImVec2(x, mid - static_cast<float>(track.overview_min[i]) * 0.5f * rest_h),
                to_u32(with_alpha(theme().text_dim, 0.5f)));
        }
    }
    const TrackAnalysis *a = track.track_analysis.get();
    // Zoomed: the waveform over the upper part, the per-bar features under it.
    const float wave_h = lanes ? 0.55f * rest_h : 0.0f;
    if (lanes && track.detail_bucket_samples > 0 && !track.detail_min.empty()) {
        draw_waveform(*draw, track, rest_y, wave_h, p0.x, width, t_of);
    }
    if (lanes && a != nullptr && !a->section_bars.empty()) {
        // Kick (a row), level (bars), riser (a line).
        const float kick_h = 0.6f * em;
        const float level_y = rest_y + wave_h + kick_h + 3.0f;
        const float level_h = std::max(1.0f, rest_h - wave_h - kick_h - 3.0f);
        double lo = a->section_bars.front().level_db;
        double hi = lo;
        for (const SectionBar &b : a->section_bars) {
            lo = std::min(lo, b.level_db);
            hi = std::max(hi, b.level_db);
        }
        const double analysis_bar_s = a->beats.bpm > 0.0 ? 4.0 * 60.0 / a->beats.bpm : 1.0;
        for (const SectionBar &b : a->section_bars) {
            if (b.start_s + analysis_bar_s < span.from_s || b.start_s > span.to_s) {
                continue;
            }
            const float x0 = x_of(b.start_s) + 1.0f;
            const float x1 = x_of(b.start_s + analysis_bar_s) - 1.0f;
            const float kick_share = b.kick_beats / 4.0f;
            draw->AddRectFilled(
                ImVec2(x0, rest_y + wave_h), ImVec2(x1, rest_y + wave_h + kick_h),
                to_u32(with_alpha(theme().text, b.kick ? 0.15f + 0.6f * kick_share : 0.06f)));
            const float level = hi > lo ? static_cast<float>((b.level_db - lo) / (hi - lo)) : 0.5f;
            draw->AddRectFilled(ImVec2(x0, level_y + level_h * (1.0f - level)),
                                ImVec2(x1, level_y + level_h),
                                to_u32(with_alpha(theme().info, 0.35f)));
        }
        if (!a->riser.empty() && a->mix_features.sample_rate_hz > 0) {
            const double frame_s =
                static_cast<double>(a->mix_features.hop_samples) / a->mix_features.sample_rate_hz;
            ImVec2 previous(-1.0f, 0.0f);
            for (float x = p0.x; x <= p0.x + width; x += 2.0f) {
                const double tt = t_of(x);
                const auto frame = static_cast<long>(std::lround(tt / frame_s));
                if (frame < 0 || frame >= static_cast<long>(a->riser.size())) {
                    previous.x = -1.0f;
                    continue;
                }
                const ImVec2 point(x, level_y +
                                          level_h * (1.0f - a->riser[static_cast<size_t>(frame)]));
                if (previous.x >= 0.0f) {
                    draw->AddLine(previous, point, to_u32(theme().warning), 1.5f);
                }
                previous = point;
            }
        }
    }

    // The detection, thin.
    if (a != nullptr) {
        for (size_t i = 0; i < a->sections.size(); ++i) {
            const DetectedSection &s = a->sections[i];
            if (s.end_s < span.from_s || s.start_s > span.to_s) {
                continue;
            }
            draw->AddRectFilled(ImVec2(x_of(s.start_s), detected_y),
                                ImVec2(x_of(s.end_s), detected_y + detected_h),
                                section_colour(s.label, 0.8f));
        }
    }

    // The labelled sections (or the detection they would start from, faded).
    const bool labelled = sections_labelled();
    std::vector<SectionLabel> sections = working_sections();
    for (size_t i = 0; i < sections.size(); ++i) {
        const double start = sections[i].start_s;
        const double end = i + 1 < sections.size() ? sections[i + 1].start_s : track.duration_s;
        if (end < span.from_s || start > span.to_s) {
            continue;
        }
        const float x0 = x_of(start);
        const float x1 = x_of(end);
        draw->AddRectFilled(ImVec2(x0, band_y), ImVec2(x1, band_y + band_h),
                            section_colour(sections[i].label, labelled ? 1.0f : 0.35f));
        if (i > 0) {
            draw->AddLine(ImVec2(x0, band_y), ImVec2(x0, band_y + band_h),
                          to_u32(theme().section_boundary), labelled ? 2.0f : 1.0f);
        }
        const float text_x = std::max(x0, p0.x) + 3.0f;
        const char *text = sections[i].label.c_str();
        if (x1 - text_x > ImGui::CalcTextSize(text).x + 2.0f) {
            draw->AddText(ImVec2(text_x, band_y + 0.5f * (band_h - ImGui::GetTextLineHeight())),
                          to_u32(theme().section_text), text);
        }
    }

    // The playhead.
    const float play_x = x_of(playhead_s());
    draw->AddLine(ImVec2(play_x, p0.y), ImVec2(play_x, p0.y + height), to_u32(theme().playhead),
                  2.0f);
    draw->PopClipRect();

    // Mouse: drag a boundary on the band, else seek (and scrub); in the zoomed view, dragging the
    // waveform and lanes pans; right-click a section's menu.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int near_boundary = -1;
    if (hovered && mouse.y >= band_y && mouse.y <= band_y + band_h) {
        for (size_t i = 1; i < sections.size(); ++i) {
            if (std::fabs(x_of(sections[i].start_s) - mouse.x) <= kHandleGrabPixels) {
                near_boundary = static_cast<int>(i);
            }
        }
    }
    if (near_boundary >= 0 || dragging_ >= 0) {
        if (hovered || active) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
    }
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        near_boundary >= 0) {
        if (!labelled) {
            commit(sections); // The edit starts from the detection.
        }
        session_.labels().begin_gesture();
        dragging_ = near_boundary;
    }
    if (active && dragging_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const double t = ImGui::GetIO().KeyAlt ? t_of(mouse.x) : snap_to_bar(l.grid, t_of(mouse.x));
        const double gap = bar_s > 0.0 ? bar_s : 0.5;
        commit(move_boundary(working_sections(), static_cast<size_t>(dragging_), t, gap));
    } else if (lanes && ImGui::IsItemActivated() && dragging_ < 0 &&
               ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.y > band_y + band_h &&
               ImGui::GetIO().KeyCtrl) {
        grid_dragging_ = true;
        pan_last_x_ = mouse.x;
        session_.labels().begin_gesture();
    } else if (active && grid_dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        shift_grid((mouse.x - pan_last_x_) / width * length); // The bar lines follow the mouse.
        pan_last_x_ = mouse.x;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (lanes && ImGui::IsItemActivated() && dragging_ < 0 &&
               ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.y > band_y + band_h) {
        panning_ = true;
        pan_start_x_ = pan_last_x_ = mouse.x;
    } else if (active && panning_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // The content follows the mouse: dragging right goes back in time.
        if (std::fabs(mouse.x - pan_start_x_) > kPanStartPixels) {
            seek(playhead_s() - (mouse.x - pan_last_x_) / width * length);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        pan_last_x_ = mouse.x;
    } else if (active && dragging_ < 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        seek(t_of(mouse.x)); // Click or scrub.
    }
    if (grid_dragging_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        session_.labels().end_gesture();
        grid_dragging_ = false;
    }
    if (panning_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (std::fabs(mouse.x - pan_start_x_) <= kPanStartPixels) {
            seek(t_of(pan_start_x_)); // A click, not a pan.
        }
        panning_ = false;
    }
    if (dragging_ >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        session_.labels().end_gesture();
        dragging_ = -1;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouse.y >= band_y &&
        mouse.y <= band_y + band_h) {
        menu_time_s_ = t_of(mouse.x);
        menu_section_ = section_at(sections, menu_time_s_);
        ImGui::OpenPopup("##section menu");
    }
    if (hovered && !lanes) {
        ImGui::SetItemTooltip("Click or drag to move the playhead; drag a boundary to move it "
                              "(Alt: off the bars); right-click a section for its menu.");
    }
    const ImGuiIO &io = ImGui::GetIO();
    if (hovered && lanes && (io.MouseWheelH != 0.0f || (io.KeyShift && io.MouseWheel != 0.0f))) {
        // Shift and the wheel (or a sideways wheel or touchpad) pan by an eighth of the view.
        const float steps = io.MouseWheelH != 0.0f ? -io.MouseWheelH : -io.MouseWheel;
        seek(playhead_s() + steps * 0.125 * length);
    } else if (hovered && lanes && io.MouseWheel != 0.0f) {
        zoom_bars_ =
            std::clamp(zoom_bars_ * std::pow(0.85f, io.MouseWheel), kMinZoomBars, kMaxZoomBars);
    }
    if (hovered && lanes) {
        ImGui::SetItemTooltip("Drag the waveform to pan (click: move the playhead); Ctrl and a "
                              "drag moves the grid; the wheel zooms, Shift and the wheel pan; "
                              "drag a boundary to move it.");
    }
    if (ImGui::BeginPopup("##section menu")) {
        if (menu_section_ >= 0 && menu_section_ < static_cast<int>(sections.size())) {
            const auto index = static_cast<size_t>(menu_section_);
            ImGui::TextDisabled("%s from %s", sections[index].label.c_str(),
                                format_time(sections[index].start_s).c_str());
            ImGui::Separator();
            for (const char *name : kSectionNames) {
                if (ImGui::MenuItem(name, nullptr, sections[index].label == name)) {
                    std::vector<SectionLabel> changed = sections;
                    changed[index].label = name;
                    commit(std::move(changed));
                }
            }
            ImGui::Separator();
            const double split = snap_to_bar(l.grid, menu_time_s_);
            char split_text[48];
            std::snprintf(split_text, sizeof(split_text), "Split at bar %lld",
                          bar_index_at(l.grid, split + 1e-6) + 1);
            if (ImGui::MenuItem(split_text)) {
                commit(mark_section(sections, split, sections[index].label,
                                    bar_s > 0.0 ? 0.25 * bar_s : 0.1));
            }
            if (ImGui::MenuItem("Remove this section's start", nullptr, false, index > 0)) {
                std::vector<SectionLabel> changed = sections;
                changed.erase(changed.begin() + static_cast<std::ptrdiff_t>(index));
                commit(std::move(changed));
            }
        }
        ImGui::EndPopup();
    }
}

void LabellingView::handle_keys() {
    if (ImGui::GetIO().WantTextInput || !session_.has_track()) {
        return;
    }
    const ImGuiIO &io = ImGui::GetIO();
    const Labels &l = session_.labels().labels();
    const double bar_s = bar_length_s(l.grid);
    Transport &transport = session_.transport();
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        transport.playing() ? transport.pause() : transport.play();
    }
    const double t = playhead_s();
    const double step = bar_s > 0.0 ? bar_s * (io.KeyShift ? effective_phrases().bars : 1) : 2.0;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        seek(snap_to_bar(l.grid, t) - step);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        seek(snap_to_bar(l.grid, t) + step);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
        seek(0.0);
    }
    const std::vector<SectionLabel> sections = working_sections();
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
        // While playing, a little slack: pressing just after a boundary goes to the one before.
        if (const auto b = previous_boundary(sections, t - (transport.playing() ? 0.5 : 0.0)); b) {
            seek(*b);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
        if (const auto b = next_boundary(sections, t); b) {
            seek(*b);
        }
    }
    if (!io.KeyCtrl) {
        for (int k = 0; k < 6; ++k) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + k), false) ||
                ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad1 + k), false)) {
                mark(k);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            const std::vector<SectionLabel> removed =
                remove_boundary(sections, t, bar_s > 0.0 ? bar_s : 1.0);
            if (removed.size() != sections.size()) {
                commit(removed);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Comma) || ImGui::IsKeyPressed(ImGuiKey_Period)) {
            const double nudge_s = ImGui::GetIO().KeyShift ? 0.001 : 0.010;
            session_.labels().begin_gesture();
            shift_grid(ImGui::IsKeyPressed(ImGuiKey_Comma) ? -nudge_s : nudge_s);
            session_.labels().end_gesture();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
            LabelPhrases p = effective_phrases();
            p.set = true;
            p.start_s = snap_to_bar(l.grid, t);
            session_.labels().set_phrases(p);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_D, false) && l.grid.bpm > 0.0) {
            LabelGrid g = l.grid;
            const double beat_s = 60.0 / g.bpm;
            const double beat =
                g.first_downbeat_s + std::round((t - g.first_downbeat_s) / beat_s) * beat_s;
            g.first_downbeat_s = beat - std::floor(beat / (4.0 * beat_s)) * 4.0 * beat_s;
            session_.labels().set_grid(g);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            session_.labels().set_sections_done(!l.sections_done);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_N, false) && save()) {
            size_t from = tracks_.size();
            for (size_t i = 0; i < tracks_.size(); ++i) {
                from = tracks_[i].path == session_.track().path ? i : from;
            }
            if (const auto next = next_unfinished(tracks_, from); next) {
                open_track(tracks_[*next].path);
            }
        }
    }
}

} // namespace psyight
