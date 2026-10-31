#include "io/section_marks.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace psyight {

namespace {

void sort_sections(std::vector<SectionLabel> &sections) {
    std::stable_sort(
        sections.begin(), sections.end(),
        [](const SectionLabel &a, const SectionLabel &b) { return a.start_s < b.start_s; });
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
    std::vector<double> found;
    for (size_t i = 1; i < detected.size(); ++i) {
        found.push_back(detected[i].start_s);
    }
    std::vector<bool> used(found.size(), false);
    score.detected_boundaries = static_cast<int>(found.size());
    for (size_t i = 1; i < labelled.size(); ++i) {
        ++score.labelled_boundaries;
        int best = -1;
        for (size_t j = 0; j < found.size(); ++j) {
            const double d = std::fabs(found[j] - labelled[i].start_s);
            if (!used[j] && d <= tolerance_s &&
                (best < 0 ||
                 d < std::fabs(found[static_cast<size_t>(best)] - labelled[i].start_s))) {
                best = static_cast<int>(j);
            }
        }
        if (best >= 0) {
            used[static_cast<size_t>(best)] = true;
            ++score.matched;
        }
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
