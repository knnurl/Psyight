#include "gui/scoring_view.h"

#include "analysis/bass_tracker.h"
#include "core/pitch.h"
#include "gui/theme.h"
#include "gui/widgets.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace psyight {

namespace {

constexpr int kOffsetHistogramBins = 40;
constexpr float kHistogramHeightEm = 9.0f;

const char *kind_name(NoteErrorKind kind) {
    switch (kind) {
    case NoteErrorKind::missed:
        return "missed";
    case NoteErrorKind::false_positive:
        return "false positive";
    case NoteErrorKind::octave:
        return "octave";
    case NoteErrorKind::pitch:
        return "pitch";
    case NoteErrorKind::offset:
        return "offset";
    }
    return "";
}

std::string describe(const NoteError &e) {
    const auto length_ms = [](const BassNoteLabel &n) {
        return std::lround(1000.0 * (n.offset_s - n.onset_s));
    };
    char text[160];
    switch (e.kind) {
    case NoteErrorKind::missed:
        std::snprintf(text, sizeof(text), "labelled %s, %ld ms: not detected",
                      midi_to_note_name(e.reference.midi).c_str(), length_ms(e.reference));
        break;
    case NoteErrorKind::false_positive:
        std::snprintf(text, sizeof(text), "detected %s, %ld ms: not labelled",
                      midi_to_note_name(e.estimate.midi).c_str(), length_ms(e.estimate));
        break;
    case NoteErrorKind::octave:
    case NoteErrorKind::pitch:
        std::snprintf(text, sizeof(text), "labelled %s, detected %s (%+.0f cents)",
                      midi_to_note_name(e.reference.midi).c_str(),
                      midi_to_note_name(e.estimate.midi).c_str(), e.error);
        break;
    case NoteErrorKind::offset:
        std::snprintf(text, sizeof(text), "ends %+.0f ms off (labelled %s, %ld ms)",
                      1000.0 * e.error, midi_to_note_name(e.reference.midi).c_str(),
                      length_ms(e.reference));
        break;
    }
    return text;
}

// A statistic, or "n/a" when it has no matched notes (NaN).
void cell(const char *format, double value) {
    if (std::isnan(value)) {
        ImGui::TextDisabled("n/a");
    } else {
        mono_text(format, value);
    }
}

void score_row(const char *name, const NoteScore &s) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(name);
    ImGui::TableNextColumn();
    mono_text("%d", s.reference_count);
    ImGui::TableNextColumn();
    mono_text("%d", s.estimate_count);
    ImGui::TableNextColumn();
    mono_text("%d", s.matched_count);
    ImGui::TableNextColumn();
    cell("%.3f", s.onset_precision);
    ImGui::TableNextColumn();
    cell("%.3f", s.onset_recall);
    ImGui::TableNextColumn();
    cell("%.3f", s.onset_f);
    ImGui::TableNextColumn();
    cell("%+.1f", 1000.0 * s.offset_error_median_s);
    ImGui::TableNextColumn();
    cell("%.1f", 1000.0 * s.offset_error_abs_p90_s);
    ImGui::TableNextColumn();
    cell("%.3f", s.offset_hit_rate);
    ImGui::TableNextColumn();
    cell("%.3f", s.pitch_accuracy);
    ImGui::TableNextColumn();
    mono_text("%d", s.octave_error_count);
    ImGui::TableNextColumn();
    cell("%.3f", s.gate_ratio_mae);
}

} // namespace

void ScoringView::rescore_if_changed() {
    const std::vector<BassNote> *notes = session_.detected_notes();
    const Labels &labels = session_.labels().labels();
    if (notes == scored_notes_ && labels == scored_labels_) {
        return;
    }
    scored_notes_ = notes;
    scored_labels_ = labels;
    selected_error_ = -1;
    if (notes == nullptr) {
        score_ = {};
        errors_.clear();
        offset_errors_ms_.clear();
        return;
    }
    const std::vector<BassNoteLabel> estimates = to_labels(*notes);
    score_ = score_track(labels, estimates, {});
    errors_ = list_note_errors(labels, estimates, {});
    offset_errors_ms_.clear();
    for (const double e_s : score_.total.offset_errors_s) {
        offset_errors_ms_.push_back(1000.0 * e_s);
    }
}

void ScoringView::draw() {
    if (!ImGui::Begin("Scoring")) {
        ImGui::End();
        return;
    }
    if (!session_.has_track()) {
        ImGui::TextUnformatted("No track loaded.");
        ImGui::End();
        return;
    }
    rescore_if_changed();
    if (scored_notes_ == nullptr) {
        ImGui::TextWrapped("No detected notes to score: the analysis found no tempo.");
        ImGui::End();
        return;
    }
    if (score_.regions.empty()) {
        ImGui::TextWrapped("No complete region yet. Label every note in a region, then tick its "
                           "\"Complete\" box in the note inspector: only complete regions are "
                           "scored, because unlabelled notes would count as false positives.");
        ImGui::End();
        return;
    }
    const ScoringParams defaults;
    ImGui::TextWrapped("The %zu detected notes (from the %s) against the labels of %zu complete "
                       "region%s. Onsets match within %.0f ms; pitch is right within %.0f cents.",
                       scored_notes_->size(), session_.detection_name(), score_.regions.size(),
                       score_.regions.size() == 1 ? "" : "s", 1000.0 * defaults.onset_tolerance_s,
                       defaults.pitch_tolerance_cents);
    draw_table();
    draw_offsets();
    draw_errors();
    ImGui::End();
}

void ScoringView::draw_table() const {
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
    const float height =
        ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(score_.regions.size()) + 3.5f);
    if (!ImGui::BeginTable("##scores", 13, kFlags, ImVec2(0.0f, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(1, 1);
    for (const char *heading : {"Region (s)", "Labelled", "Detected", "Matched", "Precision",
                                "Recall", "F", "Offset median (ms)", "Offset |p90| (ms)",
                                "Offset hits", "Pitch", "Octave errors", "Gate MAE"}) {
        ImGui::TableSetupColumn(heading);
    }
    ImGui::TableHeadersRow();
    for (const RegionScore &r : score_.regions) {
        char name[48];
        std::snprintf(name, sizeof(name), "%.2f to %.2f", r.region.start_s, r.region.end_s);
        score_row(name, r.score);
    }
    if (score_.regions.size() > 1) {
        score_row("Total", score_.total);
    }
    ImGui::EndTable();
}

void ScoringView::draw_offsets() const {
    ImGui::SeparatorText("Offset errors (detected minus labelled)");
    if (offset_errors_ms_.empty()) {
        ImGui::TextDisabled("No matched notes.");
        return;
    }
    const float em = ImGui::GetFontSize();
    if (begin_plot("##offset errors", ImVec2(-1.0f, kHistogramHeightEm * em),
                   ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes("ms", "notes", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotHistogram("offsets", offset_errors_ms_.data(),
                              static_cast<int>(offset_errors_ms_.size()), kOffsetHistogramBins);
        end_plot();
    }
}

void ScoringView::draw_errors() {
    int counts[5] = {};
    for (const NoteError &e : errors_) {
        ++counts[static_cast<int>(e.kind)];
    }
    char heading[160];
    std::snprintf(heading, sizeof(heading),
                  "Errors: %d missed, %d false positive%s, %d octave, %d pitch, %d offset",
                  counts[0], counts[1], counts[1] == 1 ? "" : "s", counts[2], counts[3], counts[4]);
    ImGui::SeparatorText(heading);
    if (errors_.empty()) {
        ImGui::TextDisabled("None.");
        return;
    }
    ImGui::TextDisabled("Worst first. Click one to seek there.");
    if (!ImGui::BeginChild("##errors", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(errors_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const NoteError &e = errors_[i];
            char label[224];
            std::snprintf(label, sizeof(label), "%9.3f s  %-14s  %s##%d", e.time_s,
                          kind_name(e.kind), describe(e).c_str(), i);
            if (ImGui::Selectable(label, i == selected_error_)) {
                selected_error_ = i;
                const LoadedTrack &track = session_.track();
                const double t_s = std::clamp(e.time_s, 0.0, track.duration_s);
                session_.transport().seek(seconds_to_samples(t_s, track.sample_rate_hz));
                if (!session_.view.follow_playhead) { // Following moves the view by itself.
                    const double width_s = session_.view.max_s - session_.view.min_s;
                    session_.view.min_s = t_s - 0.5 * width_s;
                    session_.view.max_s = t_s + 0.5 * width_s;
                }
            }
        }
    }
    ImGui::EndChild();
}

} // namespace psyight
