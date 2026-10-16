#include "gui/bass_roll_view.h"

#include "core/paths.h"
#include "core/pitch.h"
#include "gui/spectrogram_image.h"
#include "gui/time_plot.h"

#include <imgui.h>
#include <implot.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <tuple>
#include <vector>

namespace psyight {

namespace {

constexpr int kLowestMidi = 21;  // A0.
constexpr int kHighestMidi = 64; // E4: bass with an octave or two of headroom.
constexpr float kEdgeGrabPixels = 5.0f;
constexpr float kMinNoteWidthForEdgesPixels = 14.0f;
constexpr ImU32 kNoteFill = IM_COL32(64, 190, 170, 200);
constexpr ImU32 kNoteBorder = IM_COL32(150, 240, 225, 255);
constexpr ImU32 kSelectedBorder = IM_COL32(255, 230, 120, 255);
constexpr ImU32 kSlideLine = IM_COL32(150, 240, 225, 200);
constexpr ImU32 kBlackKeyRow = IM_COL32(255, 255, 255, 10);
constexpr ImU32 kBoxFill = IM_COL32(255, 230, 120, 30);
constexpr ImU32 kBoxBorder = IM_COL32(255, 230, 120, 180);
constexpr ImU32 kCompleteRegion = IM_COL32(80, 200, 120, 28);
constexpr ImU32 kIncompleteRegion = IM_COL32(200, 200, 200, 18);
constexpr double kPitchRangeLowQuantile = 0.6; // Most frames are quiet: keep them dark.
constexpr double kPitchRangeHighQuantile = 0.997;
static_assert(kLowestMidi == kSalienceFirstMidi && kHighestMidi == kSalienceLastMidi,
              "the bass roll rows and the salience rows must match");
constexpr int kMaxRepeatCount = 256;

struct RepeatPeriod {
    const char *label;
    int slots; // 0: the pattern's default period.
};
constexpr std::array<RepeatPeriod, 7> kRepeatPeriods{{{"Auto", 0},
                                                      {"1/16", 1},
                                                      {"1 beat", 4},
                                                      {"2 beats", 8},
                                                      {"1 bar", 16},
                                                      {"2 bars", 32},
                                                      {"4 bars", 64}}};

std::string describe_slots(int slots) {
    char text[32];
    if (slots % kSlotsPerBar == 0) {
        std::snprintf(text, sizeof(text), "%d bar%s", slots / kSlotsPerBar,
                      slots == kSlotsPerBar ? "" : "s");
    } else if (slots % kSlotsPerBeat == 0) {
        std::snprintf(text, sizeof(text), "%d beat%s", slots / kSlotsPerBeat,
                      slots == kSlotsPerBeat ? "" : "s");
    } else {
        std::snprintf(text, sizeof(text), "%d/16", slots);
    }
    return text;
}

bool is_black_key(int midi) {
    const int pitch_class = midi % 12;
    return pitch_class == 1 || pitch_class == 3 || pitch_class == 6 || pitch_class == 8 ||
           pitch_class == 10;
}

} // namespace

double BassRollView::snapped(double t_s) const {
    if (!snap_ || ImGui::GetIO().KeyAlt) {
        return t_s;
    }
    const Grid grid = session_.grid();
    return samples_to_seconds(grid.snap_to_slot(seconds_to_samples(t_s, grid.sample_rate_hz)),
                              grid.sample_rate_hz);
}

int BassRollView::hovered_note(bool &near_start, bool &near_end) const {
    near_start = near_end = false;
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    const float mouse_x = ImGui::GetMousePos().x;
    const std::vector<BassNoteLabel> &notes = session_.labels().labels().bass_notes;
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i) { // Later notes are on top.
        const BassNoteLabel &note = notes[i];
        if (mouse.y < note.midi || mouse.y >= note.midi + 1) {
            continue;
        }
        const float left = ImPlot::PlotToPixels(note.onset_s, 0.0).x;
        const float right = ImPlot::PlotToPixels(note.offset_s, 0.0).x;
        if (mouse_x < left - kEdgeGrabPixels || mouse_x > right + kEdgeGrabPixels) {
            continue;
        }
        if (right - left >= kMinNoteWidthForEdgesPixels) {
            near_start = mouse_x - left <= kEdgeGrabPixels;
            near_end = right - mouse_x <= kEdgeGrabPixels;
        } else {
            near_end = mouse_x > right; // Narrow notes: only the trailing margin resizes.
        }
        return i;
    }
    return -1;
}

void BassRollView::draw_roll() {
    ImGui::Begin("Bass roll");
    if (!session_.has_track()) {
        ImGui::TextUnformatted("No track loaded.");
        ImGui::End();
        return;
    }
    const LoadedTrack &track = session_.track();
    LabelEditor &labels = session_.labels();
    sync_selection();
    mouse_time_s_.reset();
    ImGui::Checkbox("Snap to 1/16 (hold Alt for free)", &snap_);
    ImGui::SameLine();
    ImGui::TextDisabled("Double-click: new note. Drag: move, resize or box select (Shift adds). "
                        "Right-click: delete. Ctrl+C, V, D: copy, paste, duplicate. "
                        "Middle-drag: pan. Middle double-click: fit. Ctrl+click: seek.");
    ImGui::Checkbox("Pitch background", &show_pitch_);
    if (show_pitch_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(12.0f * ImGui::GetFontSize());
        if (ImGui::DragFloatRange2("##pitch range", &pitch_min_db_, &pitch_max_db_, 0.25f, -120.0f,
                                   0.0f, "%.0f dB", "%.0f dB", ImGuiSliderFlags_AlwaysClamp)) {
            pitch_max_db_ = std::max(pitch_max_db_, pitch_min_db_ + 1.0f);
            pitch_range_changed_ = true;
        }
    }
    const FrontEndResult *analysis = track.analysis.get();
    if (analysis != nullptr && !salience_textures_.built_for(&analysis->pitch_salience)) {
        // A new track: start from its own value range (quiet frames dark, peaks bright).
        std::tie(pitch_min_db_, pitch_max_db_) = display_range(
            analysis->pitch_salience, kPitchRangeLowQuantile, kPitchRangeHighQuantile);
        pitch_range_changed_ = true;
    }
    if (analysis != nullptr && pitch_range_changed_) {
        salience_textures_.build(analysis->pitch_salience, analysis->hop_samples,
                                 analysis->sample_rate_hz, pitch_min_db_, pitch_max_db_,
                                 implot_colour_table(ImPlotColormap_Plasma));
        pitch_range_changed_ = false;
    }
    ImGui::SameLine();
    ImGui::Text("| %zu notes, %zu selected%s", labels.labels().bass_notes.size(), selection_.size(),
                labels.dirty() ? ", unsaved" : "");

    // In the bass roll the left button edits, so panning and fit-to-data (ImPlot's double-click,
    // which would reset the view whenever a note is created) move to the middle button here only.
    ImPlotInputMap &input = ImPlot::GetInputMap();
    const ImPlotInputMap saved_input = input;
    input.Pan = ImGuiMouseButton_Middle;
    input.Fit = ImGuiMouseButton_Middle;
    if (ImPlot::BeginPlot("##bass roll", ImVec2(-1, -1),
                          ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
        setup_time_axis(session_.view);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_Y1, kLowestMidi, kHighestMidi + 1, ImPlotCond_Always);
        std::vector<double> tick_rows;
        std::vector<std::string> tick_names;
        for (int midi = kLowestMidi; midi <= kHighestMidi; ++midi) {
            if (midi % 12 == 0 || midi % 12 == 5) { // C and F rows.
                tick_rows.push_back(midi + 0.5);
                tick_names.push_back(midi_to_note_name(midi));
            }
        }
        std::vector<const char *> tick_labels;
        for (const std::string &name : tick_names) {
            tick_labels.push_back(name.c_str());
        }
        ImPlot::SetupAxisTicks(ImAxis_Y1, tick_rows.data(), static_cast<int>(tick_rows.size()),
                               tick_labels.data());

        const ImPlotRect limits = ImPlot::GetPlotLimits();
        ImPlot::PushPlotClipRect();
        ImDrawList *draw = ImPlot::GetPlotDrawList();
        if (show_pitch_ && analysis != nullptr) {
            ImPlot::PopPlotClipRect();
            salience_textures_.draw(limits.X.Min, limits.X.Max, kSalienceFirstMidi);
            ImPlot::PushPlotClipRect();
        }
        for (int midi = kLowestMidi; midi <= kHighestMidi && !show_pitch_; ++midi) {
            if (is_black_key(midi)) {
                draw->AddRectFilled(ImPlot::PlotToPixels(limits.X.Min, midi + 1),
                                    ImPlot::PlotToPixels(limits.X.Max, midi), kBlackKeyRow);
            }
        }
        draw_regions();
        ImPlot::PopPlotClipRect();
        draw_grid_lines(session_.grid(), limits.X.Min, limits.X.Max);
        ImPlot::PushPlotClipRect();
        draw_notes();
        ImPlot::PopPlotClipRect();
        draw_playhead(session_.playhead_s());
        draw_loop(session_, false, 10); // Shaded only: dragging here edits notes.
        handle_mouse(track);
        ImPlot::EndPlot();
    }
    input = saved_input;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        handle_keys();
    }
    ImGui::End();
}

void BassRollView::draw_regions() const {
    ImDrawList *draw = ImPlot::GetPlotDrawList();
    for (const LabelRegion &region : session_.labels().labels().regions) {
        draw->AddRectFilled(ImPlot::PlotToPixels(region.start_s, kHighestMidi + 1),
                            ImPlot::PlotToPixels(region.end_s, kLowestMidi),
                            region.complete ? kCompleteRegion : kIncompleteRegion);
    }
}

void BassRollView::draw_notes() const {
    ImDrawList *draw = ImPlot::GetPlotDrawList();
    const std::vector<BassNoteLabel> &notes = session_.labels().labels().bass_notes;
    for (size_t i = 0; i < notes.size(); ++i) {
        const BassNoteLabel &note = notes[i];
        const ImVec2 top_left = ImPlot::PlotToPixels(note.onset_s, note.midi + 1);
        const ImVec2 bottom_right = ImPlot::PlotToPixels(note.offset_s, note.midi);
        draw->AddRectFilled(top_left, bottom_right, kNoteFill, 2.0f);
        const bool selected = is_selected(static_cast<int>(i));
        draw->AddRect(top_left, bottom_right, selected ? kSelectedBorder : kNoteBorder, 2.0f, 0,
                      selected ? 2.0f : 1.0f);
        if (note.slide_in && i > 0) {
            const BassNoteLabel &previous = notes[i - 1];
            draw->AddLine(ImPlot::PlotToPixels(previous.offset_s, previous.midi + 0.5),
                          ImPlot::PlotToPixels(note.onset_s, note.midi + 0.5), kSlideLine, 2.0f);
        }
    }
}

void BassRollView::sync_selection() {
    const LoadedTrack *track = session_.has_track() ? &session_.track() : nullptr;
    LabelEditor &labels = session_.labels();
    if (track != selection_track_ || labels.undo_redo_count() != selection_undo_redo_count_) {
        selection_track_ = track;
        selection_undo_redo_count_ = labels.undo_redo_count();
        selection_.clear();
        if (drag_ != Drag::none) {
            labels.end_gesture();
            drag_ = Drag::none;
        }
        boxing_ = false;
    }
    const auto count = static_cast<int>(labels.labels().bass_notes.size());
    std::erase_if(selection_, [count](int i) { return i >= count; });
}

bool BassRollView::is_selected(int index) const {
    return std::binary_search(selection_.begin(), selection_.end(), index);
}

void BassRollView::set_selection(std::vector<int> indices) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    selection_ = std::move(indices);
}

void BassRollView::select_in_view() {
    std::vector<int> visible;
    const std::vector<BassNoteLabel> &notes = session_.labels().labels().bass_notes;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        if (notes[i].offset_s > session_.view.min_s && notes[i].onset_s < session_.view.max_s) {
            visible.push_back(i);
        }
    }
    set_selection(std::move(visible));
}

void BassRollView::move_selection(double delta_s, int delta_midi) {
    const auto grabbed_at = std::find(selection_.begin(), selection_.end(), grabbed_);
    const std::vector<int> moved = session_.labels().move_notes(selection_, delta_s, delta_midi);
    if (grabbed_at != selection_.end()) {
        grabbed_ = moved[grabbed_at - selection_.begin()];
    }
    set_selection(moved);
}

NotePattern BassRollView::selection_pattern() const {
    return copy_pattern(session_.labels().labels().bass_notes, selection_, session_.grid(),
                        kRepeatPeriods[repeat_period_choice_].slots);
}

void BassRollView::paste(double anchor_s) {
    if (clipboard_.empty()) {
        return;
    }
    const PatternPlacement placed = place_pattern(clipboard_, session_.grid(), anchor_s, 1);
    set_selection(
        session_.labels().insert_notes(placed.notes, placed.clear_from_s, placed.clear_to_s));
}

void BassRollView::duplicate(int count, double limit_s) {
    const NotePattern pattern = selection_pattern();
    const Grid grid = session_.grid();
    const PatternPlacement placed = place_pattern(
        pattern, grid, pattern.anchor_s + pattern_period_s(pattern, grid), count, limit_s);
    if (placed.notes.empty()) {
        return; // Never clear notes without placing any.
    }
    const std::vector<int> added =
        session_.labels().insert_notes(placed.notes, placed.clear_from_s, placed.clear_to_s);
    // Select the last copy, so the next duplicate continues the line.
    set_selection(std::vector<int>(
        added.begin() + static_cast<std::ptrdiff_t>(placed.last_copy_first), added.end()));
}

void BassRollView::handle_mouse(const LoadedTrack &track) {
    LabelEditor &labels = session_.labels();
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    const ImGuiIO &io = ImGui::GetIO();
    bool near_start = false;
    bool near_end = false;
    const bool hovered = ImPlot::IsPlotHovered();
    const int under_mouse = hovered ? hovered_note(near_start, near_end) : -1;
    if (hovered) {
        mouse_time_s_ = mouse.x;
    }

    if (hovered && drag_ == Drag::none && !boxing_) {
        if (near_start || near_end) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (under_mouse >= 0) {
            const BassNoteLabel &note = labels.labels().bass_notes[under_mouse];
            const double slot_s = 60.0 / labels.labels().grid.bpm / 4.0;
            ImGui::SetTooltip(
                "%s  onset %.3f s  %.1f ms  gate %.2f%s", midi_to_note_name(note.midi).c_str(),
                note.onset_s, 1000.0 * (note.offset_s - note.onset_s),
                (note.offset_s - note.onset_s) / slot_s, note.slide_in ? "  slide in" : "");
        }
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && under_mouse < 0) {
        boxing_ = false; // The first click of the double-click started a box.
        const double onset_s = snapped(mouse.x);
        const int midi = std::clamp(static_cast<int>(std::floor(mouse.y)), 0, 127);
        set_selection({labels.add_note(
            {onset_s, onset_s + default_length_s_, midi, 0.0, false, false, 1.0})});
        return;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (under_mouse >= 0 && io.KeyShift) {
            std::vector<int> toggled = selection_;
            if (is_selected(under_mouse)) {
                std::erase(toggled, under_mouse);
            } else {
                toggled.push_back(under_mouse);
            }
            set_selection(std::move(toggled));
        } else if (under_mouse >= 0) {
            // Dragging a selected note moves the whole selection; an edge resizes one note.
            if (!is_selected(under_mouse) || near_start || near_end) {
                set_selection({under_mouse});
            }
            grabbed_ = under_mouse;
            drag_ = near_start ? Drag::resize_start : near_end ? Drag::resize_end : Drag::move;
            drag_moved_ = false;
            grab_offset_s_ = mouse.x - labels.labels().bass_notes[under_mouse].onset_s;
            labels.begin_gesture();
        } else if (io.KeyCtrl) {
            session_.transport().seek(seconds_to_samples(std::clamp(mouse.x, 0.0, track.duration_s),
                                                         track.sample_rate_hz));
        } else {
            // A plain click deselects and a drag selects a box. Seeking here would scroll a
            // following view between the two clicks of a double-click and put the new note
            // somewhere else.
            boxing_ = true;
            box_start_s_ = mouse.x;
            box_start_row_ = mouse.y;
            box_base_ = io.KeyShift ? selection_ : std::vector<int>{};
            set_selection(box_base_);
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && under_mouse >= 0 &&
        drag_ == Drag::none && !boxing_) {
        labels.delete_note(under_mouse);
        selection_.clear();
    }

    if (boxing_) {
        handle_box_select(mouse.x, mouse.y);
    }
    if (drag_ != Drag::none) {
        const BassNoteLabel note = labels.labels().bass_notes[grabbed_];
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            drag_moved_ = true;
            switch (drag_) {
            case Drag::move:
                move_selection(snapped(mouse.x - grab_offset_s_) - note.onset_s,
                               std::clamp(static_cast<int>(std::floor(mouse.y)), 0, 127) -
                                   note.midi);
                break;
            case Drag::resize_start:
                grabbed_ = labels.resize_note(
                    grabbed_,
                    std::min(snapped(mouse.x), note.offset_s - LabelEditor::kMinNoteLengthS),
                    note.offset_s);
                set_selection({grabbed_});
                break;
            case Drag::resize_end:
                grabbed_ = labels.resize_note(
                    grabbed_, note.onset_s,
                    std::max(snapped(mouse.x), note.onset_s + LabelEditor::kMinNoteLengthS));
                set_selection({grabbed_});
                break;
            case Drag::none:
                break;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!drag_moved_) {
                set_selection({grabbed_}); // A click on a selected note selects only it.
            }
            const BassNoteLabel &dropped = labels.labels().bass_notes[grabbed_];
            default_length_s_ = dropped.offset_s - dropped.onset_s;
            labels.end_gesture();
            drag_ = Drag::none;
        }
    }
}

void BassRollView::handle_box_select(double mouse_s, double mouse_row) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        boxing_ = false;
        return;
    }
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        return;
    }
    const double from_s = std::min(box_start_s_, mouse_s);
    const double to_s = std::max(box_start_s_, mouse_s);
    const double low_row = std::min(box_start_row_, mouse_row);
    const double high_row = std::max(box_start_row_, mouse_row);
    std::vector<int> selected = box_base_;
    const std::vector<BassNoteLabel> &notes = session_.labels().labels().bass_notes;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        const BassNoteLabel &note = notes[i];
        if (note.offset_s > from_s && note.onset_s < to_s && note.midi + 1 > low_row &&
            note.midi < high_row) {
            selected.push_back(i);
        }
    }
    set_selection(std::move(selected));
    ImPlot::PushPlotClipRect();
    ImDrawList *draw = ImPlot::GetPlotDrawList();
    const ImVec2 top_left = ImPlot::PlotToPixels(from_s, high_row);
    const ImVec2 bottom_right = ImPlot::PlotToPixels(to_s, low_row);
    draw->AddRectFilled(top_left, bottom_right, kBoxFill);
    draw->AddRect(top_left, bottom_right, kBoxBorder);
    ImPlot::PopPlotClipRect();
}

void BassRollView::handle_keys() {
    LabelEditor &labels = session_.labels();
    if (ImGui::GetIO().WantTextInput || drag_ != Drag::none || boxing_) {
        return;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
        select_in_view();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        selection_.clear();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C) && !selection_.empty()) {
        clipboard_ = selection_pattern();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
        paste(snapped(mouse_time_s_.value_or(session_.playhead_s())));
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
        duplicate(repeat_count_, std::numeric_limits<double>::infinity());
    }
    if (selection_.empty()) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        labels.delete_notes(selection_);
        selection_.clear();
        return;
    }
    const int step = ImGui::GetIO().KeyShift ? 12 : 1;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        move_selection(0.0, step);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        move_selection(0.0, -step);
    }
}

void BassRollView::draw_inspector() {
    ImGui::Begin("Note inspector");
    if (!session_.has_track()) {
        ImGui::TextUnformatted("No track loaded.");
        ImGui::End();
        return;
    }
    const LoadedTrack &track = session_.track();
    LabelEditor &labels = session_.labels();

    ImGui::TextWrapped("Labels: %s", path_to_utf8(session_.labels_path().filename()).c_str());
    if (ImGui::Button(labels.dirty() ? "Save (Ctrl+S) *" : "Save (Ctrl+S)")) {
        if (const Result<void> saved = session_.save_labels(); !saved) {
            spdlog::error("{}", saved.error().message);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!labels.can_undo());
    if (ImGui::Button("Undo")) {
        labels.undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!labels.can_redo());
    if (ImGui::Button("Redo")) {
        labels.redo();
    }
    ImGui::EndDisabled();
    sync_selection();
    if (!session_.status().empty()) {
        ImGui::TextWrapped("%s", session_.status().c_str());
    }

    ImGui::SeparatorText("Selection");
    draw_inspector_selection();
    if (selection_.size() == 1) {
        ImGui::SeparatorText("Selected note");
        draw_inspector_note();
    }
    ImGui::SeparatorText("Labelled regions");
    draw_inspector_regions(track);
    ImGui::End();
}

void BassRollView::draw_inspector_note() {
    LabelEditor &labels = session_.labels();
    const int selected = selection_.front();
    BassNoteLabel note = labels.labels().bass_notes[selected];
    bool changed = false;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(9.0f * em);
    changed |= ImGui::InputDouble("Onset (s)", &note.onset_s, 0.001, 0.01, "%.4f");
    ImGui::SetNextItemWidth(9.0f * em);
    changed |= ImGui::InputDouble("Offset (s)", &note.offset_s, 0.001, 0.01, "%.4f");
    const double slot_s = 60.0 / labels.labels().grid.bpm / 4.0;
    ImGui::Text("Duration %.1f ms, gate ratio %.3f (slot %.2f ms)",
                1000.0 * (note.offset_s - note.onset_s), (note.offset_s - note.onset_s) / slot_s,
                1000.0 * slot_s);

    if (!editing_pitch_) {
        std::snprintf(pitch_text_.data(), pitch_text_.size(), "%s",
                      midi_to_note_name(note.midi).c_str());
    }
    ImGui::SetNextItemWidth(6.0f * em);
    if (ImGui::InputText("Pitch (F1 or 29)", pitch_text_.data(), pitch_text_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (const Result<int> midi = parse_pitch(pitch_text_.data()); midi) {
            note.midi = midi.value();
            changed = true;
            pitch_error_.clear();
        } else {
            pitch_error_ = midi.error().message;
        }
    }
    editing_pitch_ = ImGui::IsItemActive();
    ImGui::SameLine();
    ImGui::Text("MIDI %d", note.midi);
    if (!pitch_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", pitch_error_.c_str());
    }
    ImGui::SetNextItemWidth(9.0f * em);
    changed |= ImGui::InputDouble("Cents", &note.cents, 1.0, 10.0, "%.0f");
    changed |= ImGui::Checkbox("Slide in", &note.slide_in);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Slide out", &note.slide_out);
    float velocity = static_cast<float>(note.velocity);
    ImGui::SetNextItemWidth(9.0f * em);
    if (ImGui::SliderFloat("Velocity", &velocity, 0.0f, 1.0f, "%.2f")) {
        note.velocity = velocity;
        changed = true;
    }
    if (changed) {
        set_selection({labels.set_note(selected, note)});
    }
}

void BassRollView::draw_inspector_selection() {
    LabelEditor &labels = session_.labels();
    if (selection_.empty()) {
        ImGui::TextDisabled("Click a note, or drag a box around notes. Ctrl+A selects the notes "
                            "in view.");
    } else {
        ImGui::Text("%zu note%s selected", selection_.size(), selection_.size() == 1 ? "" : "s");
    }
    ImGui::BeginDisabled(selection_.empty());
    if (ImGui::Button("Copy (Ctrl+C)")) {
        clipboard_ = selection_pattern();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete (Del)")) {
        labels.delete_notes(selection_);
        selection_.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(clipboard_.empty());
    if (ImGui::Button("Paste at playhead")) {
        paste(snapped(session_.playhead_s()));
    }
    ImGui::EndDisabled();

    // Duplicate: the selection repeated right after itself, replacing the notes there.
    const NotePattern pattern = selection_pattern();
    const float em = ImGui::GetFontSize();
    const std::string auto_label = repeat_period_choice_ == 0 && !pattern.empty()
                                       ? "Auto (" + describe_slots(pattern.period_slots) + ")"
                                       : std::string(kRepeatPeriods[0].label);
    ImGui::SetNextItemWidth(9.0f * em);
    if (ImGui::BeginCombo("Repeat every", repeat_period_choice_ == 0
                                              ? auto_label.c_str()
                                              : kRepeatPeriods[repeat_period_choice_].label)) {
        for (int i = 0; i < static_cast<int>(kRepeatPeriods.size()); ++i) {
            if (ImGui::Selectable(i == 0 ? auto_label.c_str() : kRepeatPeriods[i].label,
                                  i == repeat_period_choice_)) {
                repeat_period_choice_ = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Auto: the slots the selection's onsets cover, rounded up to whole "
                          "beats.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(6.0f * em);
    if (ImGui::InputInt("times", &repeat_count_)) {
        repeat_count_ = std::clamp(repeat_count_, 1, kMaxRepeatCount);
    }
    ImGui::BeginDisabled(pattern.empty());
    if (ImGui::Button("Duplicate (Ctrl+D)")) {
        duplicate(repeat_count_, std::numeric_limits<double>::infinity());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const LabelRegion *region = nullptr;
    for (const LabelRegion &candidate : labels.labels().regions) {
        if (!pattern.empty() && candidate.start_s <= pattern.anchor_s &&
            pattern.anchor_s < candidate.end_s) {
            region = &candidate;
        }
    }
    const int fill_copies =
        region != nullptr ? copies_until(pattern, session_.grid(), region->end_s) : 0;
    ImGui::BeginDisabled(fill_copies == 0);
    if (ImGui::Button("Fill to region end")) {
        duplicate(fill_copies, region->end_s);
    }
    ImGui::EndDisabled();
    if (region == nullptr) {
        ImGui::SetItemTooltip("Repeats a selection inside a labelled region up to the region's "
                              "end.");
    } else {
        ImGui::SetItemTooltip("Repeats the selection %d times, up to the region's end.",
                              fill_copies);
    }
}

void BassRollView::draw_inspector_regions(const LoadedTrack &track) {
    LabelEditor &labels = session_.labels();
    const LoopRegion &loop = session_.loop();
    ImGui::TextWrapped("A complete region has every note in it labelled; only complete regions "
                       "are scored.");
    ImGui::BeginDisabled(!loop.enabled);
    if (ImGui::Button("Add region from loop")) {
        labels.add_region(
            {std::min(loop.start_s, loop.end_s), std::max(loop.start_s, loop.end_s), false});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Add region from view")) {
        labels.add_region({std::max(0.0, session_.view.min_s),
                           std::min(track.duration_s, session_.view.max_s), false});
    }
    const float em = ImGui::GetFontSize();
    const std::vector<LabelRegion> regions = labels.labels().regions;
    for (int i = 0; i < static_cast<int>(regions.size()); ++i) {
        ImGui::PushID(i);
        LabelRegion region = regions[i];
        bool changed = false;
        ImGui::SetNextItemWidth(7.0f * em);
        changed |= ImGui::InputDouble("##start", &region.start_s, 0.01, 1.0, "%.3f");
        ImGui::SameLine();
        ImGui::TextUnformatted("to");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(7.0f * em);
        changed |= ImGui::InputDouble("s##end", &region.end_s, 0.01, 1.0, "%.3f");
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Complete", &region.complete);
        if (changed) {
            labels.set_region(i, region);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Loop")) {
            session_.set_loop({true, region.start_s, region.end_s});
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            labels.delete_region(i);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
}

} // namespace psyight
