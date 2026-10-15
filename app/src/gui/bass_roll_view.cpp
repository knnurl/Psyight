#include "gui/bass_roll_view.h"

#include "core/paths.h"
#include "core/pitch.h"
#include "gui/time_plot.h"

#include <imgui.h>
#include <implot.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
constexpr ImU32 kCompleteRegion = IM_COL32(80, 200, 120, 28);
constexpr ImU32 kIncompleteRegion = IM_COL32(200, 200, 200, 18);

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
    ImGui::Checkbox("Snap to 1/16 (hold Alt for free)", &snap_);
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Double-click: new note. Drag: move or resize. Right-click: delete. Middle-drag: pan. "
        "Ctrl+click: seek.");
    ImGui::SameLine();
    ImGui::Text("| %zu notes%s", labels.labels().bass_notes.size(),
                labels.dirty() ? ", unsaved" : "");

    // In the bass roll the left button edits, so panning moves to the middle button here only.
    ImPlotInputMap &input = ImPlot::GetInputMap();
    const ImGuiMouseButton pan_button = input.Pan;
    input.Pan = ImGuiMouseButton_Middle;
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
        for (int midi = kLowestMidi; midi <= kHighestMidi; ++midi) {
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
        handle_mouse(track);
        ImPlot::EndPlot();
    }
    input.Pan = pan_button;
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
        const bool selected = static_cast<int>(i) == selected_;
        draw->AddRect(top_left, bottom_right, selected ? kSelectedBorder : kNoteBorder, 2.0f, 0,
                      selected ? 2.0f : 1.0f);
        if (note.slide_in && i > 0) {
            const BassNoteLabel &previous = notes[i - 1];
            draw->AddLine(ImPlot::PlotToPixels(previous.offset_s, previous.midi + 0.5),
                          ImPlot::PlotToPixels(note.onset_s, note.midi + 0.5), kSlideLine, 2.0f);
        }
    }
}

void BassRollView::handle_mouse(const LoadedTrack &track) {
    LabelEditor &labels = session_.labels();
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    bool near_start = false;
    bool near_end = false;
    const bool hovered = ImPlot::IsPlotHovered();
    const int under_mouse = hovered ? hovered_note(near_start, near_end) : -1;

    if (hovered && drag_ == Drag::none) {
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
        const double onset_s = snapped(mouse.x);
        const int midi = std::clamp(static_cast<int>(std::floor(mouse.y)), 0, 127);
        selected_ =
            labels.add_note({onset_s, onset_s + default_length_s_, midi, 0.0, false, false, 1.0});
        drag_ = Drag::none;
        return;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (under_mouse >= 0) {
            selected_ = under_mouse;
            drag_ = near_start ? Drag::resize_start : near_end ? Drag::resize_end : Drag::move;
            grab_offset_s_ = mouse.x - labels.labels().bass_notes[under_mouse].onset_s;
            labels.begin_gesture();
        } else {
            // A plain click only deselects: seeking would scroll a following view between the
            // two clicks of a double-click and put the new note somewhere else.
            selected_ = -1;
        }
        if (under_mouse < 0 && ImGui::GetIO().KeyCtrl) {
            session_.transport().seek(seconds_to_samples(std::clamp(mouse.x, 0.0, track.duration_s),
                                                         track.sample_rate_hz));
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && under_mouse >= 0) {
        labels.delete_note(under_mouse);
        selected_ = -1;
    }

    if (drag_ != Drag::none && selected_ >= 0) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            const BassNoteLabel note = labels.labels().bass_notes[selected_];
            switch (drag_) {
            case Drag::move:
                selected_ =
                    labels.move_note(selected_, snapped(mouse.x - grab_offset_s_),
                                     std::clamp(static_cast<int>(std::floor(mouse.y)), 0, 127));
                break;
            case Drag::resize_start:
                selected_ = labels.resize_note(
                    selected_,
                    std::min(snapped(mouse.x), note.offset_s - LabelEditor::kMinNoteLengthS),
                    note.offset_s);
                break;
            case Drag::resize_end:
                selected_ = labels.resize_note(
                    selected_, note.onset_s,
                    std::max(snapped(mouse.x), note.onset_s + LabelEditor::kMinNoteLengthS));
                break;
            case Drag::none:
                break;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const BassNoteLabel &note = labels.labels().bass_notes[selected_];
            default_length_s_ = note.offset_s - note.onset_s;
            labels.end_gesture();
            drag_ = Drag::none;
        }
    }
}

void BassRollView::handle_keys() {
    LabelEditor &labels = session_.labels();
    if (selected_ < 0 || selected_ >= static_cast<int>(labels.labels().bass_notes.size()) ||
        ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        labels.delete_note(selected_);
        selected_ = -1;
        return;
    }
    const int step = ImGui::GetIO().KeyShift ? 12 : 1;
    const BassNoteLabel &note = labels.labels().bass_notes[selected_];
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        selected_ = labels.move_note(selected_, note.onset_s, std::min(127, note.midi + step));
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        selected_ = labels.move_note(selected_, note.onset_s, std::max(0, note.midi - step));
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
    if (!session_.status().empty()) {
        ImGui::TextWrapped("%s", session_.status().c_str());
    }

    ImGui::SeparatorText("Selected note");
    draw_inspector_note();
    ImGui::SeparatorText("Labelled regions");
    draw_inspector_regions(track);
    ImGui::End();
}

void BassRollView::draw_inspector_note() {
    LabelEditor &labels = session_.labels();
    const std::vector<BassNoteLabel> &notes = labels.labels().bass_notes;
    if (selected_ < 0 || selected_ >= static_cast<int>(notes.size())) {
        selected_ = -1;
        ImGui::TextDisabled("Click a note in the bass roll.");
        return;
    }
    BassNoteLabel note = notes[selected_];
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
        selected_ = labels.set_note(selected_, note);
    }
    if (ImGui::Button("Delete note")) {
        labels.delete_note(selected_);
        selected_ = -1;
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
