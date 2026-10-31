// Section and phrase labelling (the labelling mode, gui/labelling_view.h) as pure functions of the
// labels: bars and phrases on the labelled grid, marking a section from a time, removing a
// boundary, and the detection as a starting point. Sections stay sorted, the first at 0 s;
// neighbouring sections may share a label (a groove that steps up stays two grooves).
#pragma once

#include "analysis/sections.h"
#include "io/labels.h"

#include <optional>
#include <string>
#include <vector>

namespace psyight {

// The section labels the keys 1 to 7 set, in that order (predrop: the bars just before a drop).
inline constexpr const char *kSectionNames[] = {"intro", "groove", "breakdown", "build",
                                                "drop",  "outro",  "predrop"};
// Roles a section may take (SPEC 6.6.1): grooves main, lead or peak; drops climax or main.
inline constexpr const char *kSectionRoles[] = {"main", "lead", "peak", "climax"};

// The labelled sections as the engine's: a predrop folds into the section before it (its
// predrop_s), phases and roles carried over; sorted, each to the next, the last to duration_s.
std::vector<DetectedSection> engine_sections(const std::vector<SectionLabel> &sections,
                                             double duration_s);

double bar_length_s(const LabelGrid &grid); // 0 without a tempo.
// The nearest downbeat to t_s (t_s itself without a tempo).
double snap_to_bar(const LabelGrid &grid, double t_s);
// The nearest phrase start (phrases of `phrases.bars` bars through phrases.start_s); without a
// phrase grid, the nearest bar.
double snap_to_phrase(const LabelGrid &grid, const LabelPhrases &phrases, double t_s);
// The bar (from the grid's first downbeat, 0-based, negative before it) holding t_s.
long long bar_index_at(const LabelGrid &grid, double t_s);
// Where t_s falls in its phrase (0 to bars - 1), or -1 without a phrase grid.
int bar_in_phrase(const LabelGrid &grid, const LabelPhrases &phrases, double t_s);

// The section with `label` from t_s on: a boundary within tolerance_s of t_s takes the label,
// otherwise a new boundary goes at t_s (t_s within tolerance of 0 relabels the first section).
// Without sections, the track starts as an intro.
std::vector<SectionLabel> mark_section(std::vector<SectionLabel> sections, double t_s,
                                       const std::string &label, double tolerance_s);
// Removes the boundary nearest t_s within tolerance_s, never the first section's: the section
// before it runs on. Unchanged when none is that close.
std::vector<SectionLabel> remove_boundary(std::vector<SectionLabel> sections, double t_s,
                                          double tolerance_s);
// Moves boundary `index` (not the first) to t_s, kept between its neighbours.
std::vector<SectionLabel> move_boundary(std::vector<SectionLabel> sections, size_t index,
                                        double t_s, double min_gap_s);
// The detection as labels: starts snapped to bars, the first at 0 s.
std::vector<SectionLabel> sections_from_detection(const std::vector<DetectedSection> &detected,
                                                  const LabelGrid &grid);
// The boundary strictly before or after t_s (the first section's start counts).
std::optional<double> previous_boundary(const std::vector<SectionLabel> &sections, double t_s);
std::optional<double> next_boundary(const std::vector<SectionLabel> &sections, double t_s);
// The section holding t_s, or -1 before the first.
int section_at(const std::vector<SectionLabel> &sections, double t_s);

// Detected sections against labelled ones (both as labels from 0 s; the first section's start is
// no boundary). Boundaries match one to one within tolerance_bars; bars agree when both name the
// same section at the bar's middle (bars of `grid` from its first downbeat to duration_s).
struct SectionScore {
    int labelled_boundaries = 0;
    int detected_boundaries = 0;
    int matched = 0;
    int bars = 0;
    int bars_agreeing = 0;
    double precision() const {
        return detected_boundaries > 0 ? 1.0 * matched / detected_boundaries : 1.0;
    }
    double recall() const {
        return labelled_boundaries > 0 ? 1.0 * matched / labelled_boundaries : 1.0;
    }
    double f1() const {
        const double p = precision();
        const double r = recall();
        return p + r > 0.0 ? 2.0 * p * r / (p + r) : 0.0;
    }
    double bar_accuracy() const { return bars > 0 ? 1.0 * bars_agreeing / bars : 1.0; }
    SectionScore &operator+=(const SectionScore &o);
};
SectionScore score_sections(const std::vector<SectionLabel> &labelled,
                            const std::vector<SectionLabel> &detected, const LabelGrid &grid,
                            double duration_s, double tolerance_bars = 1.0);

} // namespace psyight
