#include "io/section_marks.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace psyight {

namespace {

void sort_sections(std::vector<SectionLabel> &sections) {
    std::stable_sort(
        sections.begin(), sections.end(),
        [](const SectionLabel &a, const SectionLabel &b) { return a.start_s < b.start_s; });
}

// Every start but the first's: the boundaries.
std::vector<double> boundary_times(const std::vector<SectionLabel> &sections) {
    std::vector<double> times;
    for (size_t i = 1; i < sections.size(); ++i) {
        times.push_back(sections[i].start_s);
    }
    return times;
}

// One-to-one matching: each labelled boundary in turn takes the nearest unused detected one that
// qualifies (qualifies(labelled_s, detected_s)); the earlier one on a tie. The detected index per
// labelled boundary, -1 for none.
template <typename Qualifies>
std::vector<int> match_boundaries(const std::vector<double> &labelled,
                                  const std::vector<double> &found, Qualifies qualifies) {
    std::vector<bool> used(found.size(), false);
    std::vector<int> match;
    for (const double l : labelled) {
        int best = -1;
        for (size_t j = 0; j < found.size(); ++j) {
            const double d = std::fabs(found[j] - l);
            if (!used[j] && qualifies(l, found[j]) &&
                (best < 0 || d < std::fabs(found[static_cast<size_t>(best)] - l))) {
                best = static_cast<int>(j);
            }
        }
        if (best >= 0) {
            used[static_cast<size_t>(best)] = true;
        }
        match.push_back(best);
    }
    return match;
}

// The bar a boundary marks: the one starting at its nearest downbeat.
long long boundary_bar(const LabelGrid &grid, double t_s) {
    const double bar_s = bar_length_s(grid);
    return bar_s > 0.0 ? std::llround((t_s - grid.first_downbeat_s) / bar_s) : 0;
}

// The kind (index into kSectionNames, below kSectionKinds) of the section holding t_s; a predrop
// counts as the section before it (as engine_sections folds it), a first predrop as a build. -1
// before the first section or for a label outside the six.
int section_kind_at(const std::vector<SectionLabel> &sections, double t_s) {
    int i = section_at(sections, t_s);
    while (i > 0 && sections[static_cast<size_t>(i)].label == "predrop") {
        --i;
    }
    if (i < 0) {
        return -1;
    }
    const std::string &label = sections[static_cast<size_t>(i)].label;
    for (size_t k = 0; k < kSectionKinds; ++k) {
        if (label == kSectionNames[k] || (label == "predrop" && k == 3)) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

} // namespace

double bar_length_s(const LabelGrid &grid) { return grid.bpm > 0.0 ? 4.0 * 60.0 / grid.bpm : 0.0; }

double snap_to_bar(const LabelGrid &grid, double t_s) {
    const double bar_s = bar_length_s(grid);
    if (bar_s <= 0.0) {
        return t_s;
    }
    return grid.first_downbeat_s + std::round((t_s - grid.first_downbeat_s) / bar_s) * bar_s;
}

double snap_to_phrase(const LabelGrid &grid, const LabelPhrases &phrases, double t_s) {
    const double bar_s = bar_length_s(grid);
    if (!phrases.set || bar_s <= 0.0 || phrases.bars < 1) {
        return snap_to_bar(grid, t_s);
    }
    const double phrase_s = phrases.bars * bar_s;
    return phrases.start_s + std::round((t_s - phrases.start_s) / phrase_s) * phrase_s;
}

long long bar_index_at(const LabelGrid &grid, double t_s) {
    const double bar_s = bar_length_s(grid);
    if (bar_s <= 0.0) {
        return 0;
    }
    // A hair of tolerance, so a time on a downbeat is in the bar it starts.
    return static_cast<long long>(std::floor((t_s - grid.first_downbeat_s) / bar_s + 1e-9));
}

int bar_in_phrase(const LabelGrid &grid, const LabelPhrases &phrases, double t_s) {
    if (!phrases.set || phrases.bars < 1 || bar_length_s(grid) <= 0.0) {
        return -1;
    }
    const long long from = bar_index_at(grid, t_s) - bar_index_at(grid, phrases.start_s);
    const long long bars = phrases.bars;
    return static_cast<int>(((from % bars) + bars) % bars);
}

std::vector<SectionLabel> mark_section(std::vector<SectionLabel> sections, double t_s,
                                       const std::string &label, double tolerance_s) {
    sort_sections(sections);
    t_s = std::max(0.0, t_s);
    if (sections.empty()) {
        sections.push_back({0.0, "intro"}); // A track starts with its intro until marked otherwise.
    }
    if (t_s <= tolerance_s) {
        sections.front().label = label;
        return sections;
    }
    for (SectionLabel &s : sections) {
        if (std::fabs(s.start_s - t_s) <= tolerance_s) {
            s.label = label;
            return sections;
        }
    }
    sections.push_back({t_s, label});
    sort_sections(sections);
    return sections;
}

std::vector<SectionLabel> remove_boundary(std::vector<SectionLabel> sections, double t_s,
                                          double tolerance_s) {
    sort_sections(sections);
    size_t nearest = 0;
    double best = tolerance_s;
    for (size_t i = 1; i < sections.size(); ++i) {
        const double d = std::fabs(sections[i].start_s - t_s);
        if (d <= best) {
            best = d;
            nearest = i;
        }
    }
    if (nearest > 0) {
        sections.erase(sections.begin() + static_cast<std::ptrdiff_t>(nearest));
    }
    return sections;
}

std::vector<SectionLabel> move_boundary(std::vector<SectionLabel> sections, size_t index,
                                        double t_s, double min_gap_s) {
    if (index == 0 || index >= sections.size()) {
        return sections;
    }
    const double lo = sections[index - 1].start_s + min_gap_s;
    const double hi = index + 1 < sections.size() ? sections[index + 1].start_s - min_gap_s
                                                  : std::numeric_limits<double>::infinity();
    if (lo <= hi) {
        sections[index].start_s = std::clamp(t_s, lo, hi);
    }
    return sections;
}

std::vector<SectionLabel> sections_from_detection(const std::vector<DetectedSection> &detected,
                                                  const LabelGrid &grid) {
    std::vector<SectionLabel> out;
    const auto add = [&](double t_s, const SectionLabel &s) {
        const double start = out.empty() ? 0.0 : std::max(0.0, snap_to_bar(grid, t_s));
        if (!out.empty() && start <= out.back().start_s) {
            const double kept = out.back().start_s;
            out.back() = s; // Snapped onto the one before: the later label wins.
            out.back().start_s = kept;
            return;
        }
        SectionLabel placed = s;
        placed.start_s = start;
        out.push_back(std::move(placed));
    };
    for (const DetectedSection &d : detected) {
        add(d.start_s, {0.0, d.label, phase_name(d.phase), d.role});
        if (d.predrop_s > d.start_s && d.predrop_s < d.end_s) {
            add(d.predrop_s, {0.0, "predrop", "", ""});
        }
    }
    return out;
}

std::vector<DetectedSection> engine_sections(const std::vector<SectionLabel> &sections,
                                             double duration_s) {
    std::vector<SectionLabel> sorted = sections;
    std::sort(sorted.begin(), sorted.end(),
              [](const SectionLabel &a, const SectionLabel &b) { return a.start_s < b.start_s; });
    std::erase_if(sorted, [&](const SectionLabel &s) { return s.start_s >= duration_s; });
    std::vector<DetectedSection> out;
    for (size_t i = 0; i < sorted.size(); ++i) {
        const SectionLabel &s = sorted[i];
        const double end = i + 1 < sorted.size() ? sorted[i + 1].start_s : duration_s;
        if (s.label == "predrop" && !out.empty()) { // Part of the section before it.
            out.back().predrop_s = s.start_s;
            out.back().end_s = end;
            continue;
        }
        DetectedSection d;
        d.start_s = out.empty() ? 0.0 : s.start_s;
        d.end_s = end;
        d.label = s.label == "predrop" ? "build" : s.label;
        d.confidence = 1.0;
        d.phase = phase_from_name(s.phase);
        d.role = s.role;
        if (d.end_s > d.start_s) {
            out.push_back(std::move(d));
        }
    }
    return out;
}

std::optional<double> previous_boundary(const std::vector<SectionLabel> &sections, double t_s) {
    std::optional<double> found;
    for (const SectionLabel &s : sections) {
        if (s.start_s < t_s - 1e-6) {
            found = found ? std::max(*found, s.start_s) : s.start_s;
        }
    }
    return found;
}

std::optional<double> next_boundary(const std::vector<SectionLabel> &sections, double t_s) {
    std::optional<double> found;
    for (const SectionLabel &s : sections) {
        if (s.start_s > t_s + 1e-6) {
            found = found ? std::min(*found, s.start_s) : s.start_s;
        }
    }
    return found;
}

SectionScore &SectionScore::operator+=(const SectionScore &o) {
    labelled_boundaries += o.labelled_boundaries;
    detected_boundaries += o.detected_boundaries;
    matched += o.matched;
    bars += o.bars;
    bars_agreeing += o.bars_agreeing;
    return *this;
}

SectionScore score_sections(const std::vector<SectionLabel> &labelled,
                            const std::vector<SectionLabel> &detected, const LabelGrid &grid,
                            double duration_s, double tolerance_bars) {
    SectionScore score;
    const double bar_s = bar_length_s(grid);
    // Inclusive: a boundary exactly a bar off is within a bar (despite rounding).
    const double tolerance_s = (bar_s > 0.0 ? tolerance_bars * bar_s : 1.0) + 1e-6;
    const std::vector<double> found = boundary_times(detected);
    score.detected_boundaries = static_cast<int>(found.size());
    for (const int j : match_boundaries(boundary_times(labelled), found, [&](double l, double d) {
             return std::fabs(d - l) <= tolerance_s;
         })) {
        ++score.labelled_boundaries;
        score.matched += j >= 0;
    }
    if (bar_s > 0.0) {
        for (double start = first_downbeat_in_audio_s(grid); start + bar_s <= duration_s + 1e-9;
             start += bar_s) {
            const double middle = start + 0.5 * bar_s;
            const int a = section_at(labelled, middle);
            const int b = section_at(detected, middle);
            ++score.bars;
            score.bars_agreeing +=
                a >= 0 && b >= 0 &&
                labelled[static_cast<size_t>(a)].label == detected[static_cast<size_t>(b)].label;
        }
    }
    return score;
}

BoundaryHits &BoundaryHits::operator+=(const BoundaryHits &o) {
    labelled += o.labelled;
    detected += o.detected;
    matched += o.matched;
    return *this;
}

BoundaryOffsets &BoundaryOffsets::operator+=(const BoundaryOffsets &o) {
    labelled += o.labelled;
    matched += o.matched;
    for (size_t i = 0; i < bars.size(); ++i) {
        bars[i] += o.bars[i];
    }
    none += o.none;
    return *this;
}

BoundaryScores &BoundaryScores::operator+=(const BoundaryScores &o) {
    right_bar += o.right_bar;
    within_half_s += o.within_half_s;
    within_3_s += o.within_3_s;
    all += o.all;
    kick_change += o.kick_change;
    kick_continuous += o.kick_continuous;
    for (const auto &[name, counts] : o.transitions) {
        transitions[name].first += counts.first;
        transitions[name].second += counts.second;
    }
    for (size_t r = 0; r < kSectionKinds; ++r) {
        for (size_t c = 0; c < kSectionKinds; ++c) {
            confusion[r][c] += o.confusion[r][c];
        }
    }
    return *this;
}

BoundaryScores score_boundaries(const std::vector<SectionLabel> &labelled,
                                const std::vector<SectionLabel> &detected, const LabelGrid &grid,
                                double duration_s,
                                const std::function<bool(double)> &kick_change_at) {
    BoundaryScores score;
    const double bar_s = bar_length_s(grid);
    const std::vector<double> marks = boundary_times(labelled);
    const std::vector<double> found = boundary_times(detected);
    const auto hits = [&](auto qualifies) {
        BoundaryHits h;
        h.labelled = static_cast<int>(marks.size());
        h.detected = static_cast<int>(found.size());
        for (const int j : match_boundaries(marks, found, qualifies)) {
            h.matched += j >= 0;
        }
        return h;
    };
    score.right_bar = hits([&](double l, double d) {
        return bar_s > 0.0 ? boundary_bar(grid, d) == boundary_bar(grid, l)
                           : std::fabs(d - l) <= 1e-6;
    });
    score.within_half_s = hits([](double l, double d) { return std::fabs(d - l) <= 0.5 + 1e-6; });
    score.within_3_s = hits([](double l, double d) { return std::fabs(d - l) <= 3.0 + 1e-6; });
    // Within a bar, as score_sections.
    const double tolerance_s = (bar_s > 0.0 ? bar_s : 1.0) + 1e-6;
    const std::vector<int> within_bar = match_boundaries(
        marks, found, [&](double l, double d) { return std::fabs(d - l) <= tolerance_s; });
    for (size_t i = 0; i < marks.size(); ++i) {
        const double t = marks[i];
        const bool hit = within_bar[i] >= 0;
        int nearest = -1;
        for (size_t j = 0; j < found.size(); ++j) {
            const long long off = boundary_bar(grid, found[j]) - boundary_bar(grid, t);
            if (bar_s > 0.0 && std::llabs(off) <= kOffsetBars &&
                (nearest < 0 ||
                 std::fabs(found[j] - t) < std::fabs(found[static_cast<size_t>(nearest)] - t))) {
                nearest = static_cast<int>(j);
            }
        }
        BoundaryOffsets &kind = kick_change_at(t) ? score.kick_change : score.kick_continuous;
        for (BoundaryOffsets *o : {&score.all, &kind}) {
            ++o->labelled;
            o->matched += hit;
            if (nearest < 0) {
                ++o->none;
            } else {
                const long long off =
                    boundary_bar(grid, found[static_cast<size_t>(nearest)]) - boundary_bar(grid, t);
                ++o->bars[static_cast<size_t>(off + kOffsetBars)];
            }
        }
        std::pair<int, int> &transition =
            score.transitions[labelled[i].label + " to " + labelled[i + 1].label];
        ++transition.first;
        transition.second += hit;
    }
    if (bar_s > 0.0) {
        for (double start = first_downbeat_in_audio_s(grid); start + bar_s <= duration_s + 1e-9;
             start += bar_s) {
            const double middle = start + 0.5 * bar_s;
            const int a = section_kind_at(labelled, middle);
            const int b = section_kind_at(detected, middle);
            if (a >= 0 && b >= 0) {
                ++score.confusion[static_cast<size_t>(a)][static_cast<size_t>(b)];
            }
        }
    }
    return score;
}

std::uint32_t fnv1a_32(std::string_view bytes) {
    std::uint32_t hash = 2166136261u;
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 16777619u;
    }
    return hash;
}

bool held_out_track(std::string_view file_name_utf8) {
    std::string lower(file_name_utf8);
    for (char &c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return (fnv1a_32(lower) & 1u) != 0;
}

DetailScore &DetailScore::operator+=(const DetailScore &o) {
    phase_pairs += o.phase_pairs;
    phase_pairs_agreeing += o.phase_pairs_agreeing;
    role_bars += o.role_bars;
    role_bars_agreeing += o.role_bars_agreeing;
    labelled_events += o.labelled_events;
    detected_events += o.detected_events;
    matched_events += o.matched_events;
    return *this;
}

DetailScore score_section_detail(const std::vector<SectionLabel> &labelled,
                                 const std::vector<SectionLabel> &detected,
                                 const std::vector<EventLabel> &labelled_events,
                                 const std::vector<EventLabel> &detected_events,
                                 const LabelGrid &grid, double duration_s) {
    DetailScore score;
    const double bar_s = bar_length_s(grid);
    if (bar_s <= 0.0) {
        return score;
    }
    std::vector<std::pair<std::string, std::string>> phases; // Per bar with both phases.
    for (double start = first_downbeat_in_audio_s(grid); start + bar_s <= duration_s + 1e-9;
         start += bar_s) {
        const double middle = start + 0.5 * bar_s;
        const int a = section_at(labelled, middle);
        const int b = section_at(detected, middle);
        if (a < 0 || b < 0) {
            continue;
        }
        const SectionLabel &l = labelled[static_cast<size_t>(a)];
        const SectionLabel &d = detected[static_cast<size_t>(b)];
        if (!l.phase.empty() && !d.phase.empty()) {
            phases.emplace_back(l.phase, d.phase);
        }
        if (!l.role.empty()) {
            ++score.role_bars;
            score.role_bars_agreeing += l.role == d.role;
        }
    }
    for (size_t i = 0; i < phases.size(); ++i) {
        for (size_t j = i + 1; j < phases.size(); ++j) {
            ++score.phase_pairs;
            score.phase_pairs_agreeing +=
                (phases[i].first == phases[j].first) == (phases[i].second == phases[j].second);
        }
    }
    score.labelled_events = static_cast<int>(labelled_events.size());
    score.detected_events = static_cast<int>(detected_events.size());
    std::vector<bool> used(detected_events.size(), false);
    for (const EventLabel &l : labelled_events) {
        for (size_t j = 0; j < detected_events.size(); ++j) {
            if (!used[j] && detected_events[j].kind == l.kind &&
                std::fabs(detected_events[j].start_s - l.start_s) <= bar_s + 1e-6) {
                used[j] = true;
                ++score.matched_events;
                break;
            }
        }
    }
    return score;
}

int section_at(const std::vector<SectionLabel> &sections, double t_s) {
    int at = -1;
    for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].start_s <= t_s + 1e-9) {
            at = static_cast<int>(i);
        }
    }
    return at;
}

} // namespace psyight
