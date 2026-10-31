#include "analysis/section_detail.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kTolS = 1e-6;

bool kicking(const std::string &label) { return label == "groove" || label == "drop"; }

// The bars a section holds (their indices).
std::vector<size_t> bars_in(const DetectedSection &s, const std::vector<SectionBar> &bars) {
    std::vector<size_t> out;
    for (size_t b = 0; b < bars.size(); ++b) {
        if (bars[b].start_s >= s.start_s - kTolS && bars[b].start_s < s.end_s - kTolS) {
            out.push_back(b);
        }
    }
    return out;
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// Per bar, its texture and bass (notes, lowest and highest pitch, gate), each scaled by its
// spread over the track's bars (centred on the mean), as texture_step measures a change.
std::vector<std::vector<double>> scaled_features(const std::vector<SectionBar> &bars,
                                                 const std::vector<BassAggregate> &bass_bars) {
    std::vector<std::vector<double>> f;
    f.reserve(bars.size());
    for (const SectionBar &bar : bars) {
        std::vector<double> v = bar.texture;
        const auto bass =
            std::upper_bound(bass_bars.begin(), bass_bars.end(), bar.start_s + kTolS,
                             [](double t, const BassAggregate &a) { return t < a.start_s; });
        if (bass != bass_bars.begin()) {
            const BassAggregate &a = *(bass - 1);
            v.push_back(a.notes);
            v.push_back(a.pitch_low);
            v.push_back(a.pitch_high);
            v.push_back(a.gate_mean);
        } else {
            v.insert(v.end(), 4, 0.0);
        }
        f.push_back(std::move(v));
    }
    if (f.empty()) {
        return f;
    }
    const size_t width = f.front().size();
    for (size_t d = 0; d < width; ++d) {
        double mean = 0.0;
        for (const std::vector<double> &v : f) {
            mean += d < v.size() ? v[d] : 0.0;
        }
        mean /= static_cast<double>(f.size());
        double var = 0.0;
        for (const std::vector<double> &v : f) {
            const double x = d < v.size() ? v[d] - mean : 0.0;
            var += x * x;
        }
        const double sd = std::sqrt(var / static_cast<double>(f.size()));
        for (std::vector<double> &v : f) {
            if (d < v.size()) {
                v[d] = sd > 1e-9 ? (v[d] - mean) / sd : 0.0;
            }
        }
    }
    return f;
}

double distance(const std::vector<double> &a, const std::vector<double> &b) {
    double sum = 0.0;
    for (size_t d = 0; d < a.size() && d < b.size(); ++d) {
        sum += (a[d] - b[d]) * (a[d] - b[d]);
    }
    return std::sqrt(sum);
}

std::vector<double> mean_of(const std::vector<std::vector<double>> &f,
                            const std::vector<size_t> &rows) {
    std::vector<double> m(f.empty() ? 0 : f.front().size(), 0.0);
    for (const size_t r : rows) {
        for (size_t d = 0; d < m.size() && d < f[r].size(); ++d) {
            m[d] += f[r][d] / static_cast<double>(rows.size());
        }
    }
    return m;
}

void assign_phases(std::vector<DetectedSection> &sections, const std::vector<SectionBar> &bars,
                   const std::vector<std::vector<double>> &features, double max_distance) {
    struct Phase {
        std::vector<double> sum;
        int members = 0;
        std::vector<double> mean() const {
            std::vector<double> m = sum;
            for (double &x : m) {
                x /= members;
            }
            return m;
        }
    };
    std::vector<Phase> phases;
    for (DetectedSection &s : sections) {
        s.phase = -1;
        if (!kicking(s.label)) {
            continue;
        }
        const std::vector<size_t> rows = bars_in(s, bars);
        if (rows.empty()) {
            continue;
        }
        const std::vector<double> here = mean_of(features, rows);
        int best = -1;
        double best_distance = max_distance;
        for (size_t p = 0; p < phases.size(); ++p) {
            const double d = distance(here, phases[p].mean());
            if (d < best_distance) {
                best_distance = d;
                best = static_cast<int>(p);
            }
        }
        if (best < 0) {
            phases.push_back({std::vector<double>(here.size(), 0.0), 0});
            best = static_cast<int>(phases.size()) - 1;
        }
        Phase &phase = phases[static_cast<size_t>(best)];
        for (size_t d = 0; d < here.size() && d < phase.sum.size(); ++d) {
            phase.sum[d] += here[d];
        }
        ++phase.members;
        s.phase = best;
    }
}

void assign_roles(std::vector<DetectedSection> &sections, const std::vector<SectionBar> &bars,
                  const std::vector<float> &intensity, const SectionParams &p) {
    std::vector<double> kicking_intensity;
    std::vector<double> flatness;
    std::vector<double> centroid;
    for (const DetectedSection &s : sections) {
        if (!kicking(s.label)) {
            continue;
        }
        for (const size_t b : bars_in(s, bars)) {
            if (b < intensity.size()) {
                kicking_intensity.push_back(intensity[b]);
            }
            if (s.label == "groove") {
                flatness.push_back(bars[b].flatness);
                centroid.push_back(bars[b].centroid_hz);
            }
        }
    }
    std::sort(kicking_intensity.begin(), kicking_intensity.end());
    const double peak_from =
        kicking_intensity.empty()
            ? 2.0
            : kicking_intensity[std::min(
                  kicking_intensity.size() - 1,
                  static_cast<size_t>((1.0 - p.peak_share) *
                                      static_cast<double>(kicking_intensity.size())))];
    const double flat_median = median(flatness);
    const double centroid_median = median(centroid);
    const auto mean_intensity = [&](const DetectedSection &s) {
        double sum = 0.0;
        int n = 0;
        for (const size_t b : bars_in(s, bars)) {
            if (b < intensity.size()) {
                sum += intensity[b];
                ++n;
            }
        }
        return n > 0 ? sum / n : 0.0;
    };
    int climax = -1;
    double climax_intensity = -1.0;
    for (size_t i = 0; i < sections.size(); ++i) {
        DetectedSection &s = sections[i];
        s.role.clear();
        if (s.label == "drop") {
            s.role = "main";
            if (const double m = mean_intensity(s); m > climax_intensity) {
                climax_intensity = m;
                climax = static_cast<int>(i);
            }
            continue;
        }
        if (s.label != "groove") {
            continue;
        }
        if (mean_intensity(s) >= peak_from) {
            s.role = "peak";
            continue;
        }
        const std::vector<size_t> rows = bars_in(s, bars);
        int tonal = 0;
        for (const size_t b : rows) {
            tonal += bars[b].flatness < p.lead_flatness_ratio * flat_median &&
                     bars[b].centroid_hz > p.lead_centroid_ratio * centroid_median;
        }
        s.role = !rows.empty() && centroid_median > 0.0 &&
                         tonal >= p.lead_bars_share * static_cast<double>(rows.size())
                     ? "lead"
                     : "main";
    }
    if (climax >= 0) {
        sections[static_cast<size_t>(climax)].role = "climax";
    }
}

void find_predrops(std::vector<DetectedSection> &sections, const std::vector<SectionBar> &bars,
                   const SectionParams &p) {
    for (size_t i = 1; i < sections.size(); ++i) {
        DetectedSection &before = sections[i - 1];
        before.predrop_s = -1.0;
        if (sections[i].label != "drop" ||
            (before.label != "build" && before.label != "breakdown")) {
            continue;
        }
        const std::vector<size_t> rows = bars_in(before, bars);
        if (rows.size() < 2) {
            continue;
        }
        std::vector<double> levels;
        for (const size_t b : rows) {
            levels.push_back(bars[b].level_db);
        }
        const double gap_below = median(levels) - p.predrop_drop_db;
        size_t first = rows.size();
        for (size_t k = rows.size();
             k-- > 0 && rows.size() - k <= static_cast<size_t>(p.predrop_max_bars);) {
            if (bars[rows[k]].level_db > gap_below) {
                break;
            }
            first = k;
        }
        if (first < rows.size() && first > 0) {
            before.predrop_s = bars[rows[first]].start_s;
        }
    }
}

std::vector<DetectedEvent> find_events(const std::vector<DetectedSection> &sections,
                                       const std::vector<SectionBar> &bars,
                                       const std::vector<std::vector<double>> &features,
                                       const std::vector<double> &phrase_starts, double bar_s,
                                       const SectionParams &p) {
    std::vector<DetectedEvent> events;
    std::vector<bool> in_break(bars.size(), false);
    for (const DetectedSection &s : sections) {
        if (!kicking(s.label)) {
            continue;
        }
        const std::vector<size_t> rows = bars_in(s, bars);
        // Breaks: short runs of bars without the kick, inside the section (not at its edges).
        for (size_t k = 1; k + 1 < rows.size();) {
            if (bars[rows[k]].kick_beats > 1) {
                ++k;
                continue;
            }
            size_t end = k;
            while (end + 1 < rows.size() && bars[rows[end]].kick_beats <= 1) {
                ++end;
            }
            const size_t length = end - k;
            if (length >= 1 && length <= static_cast<size_t>(p.break_max_bars) &&
                end < rows.size() - 1) {
                events.push_back(
                    {"break", bars[rows[k]].start_s, static_cast<double>(length) * bar_s});
                for (size_t m = k; m < end; ++m) {
                    in_break[rows[m]] = true;
                }
            }
            k = std::max(end, k + 1);
        }
        // Fills: a phrase's last bar unlike the rest of the phrase.
        for (size_t q = 0; q + 1 < phrase_starts.size(); ++q) {
            const double from = phrase_starts[q];
            const double to = phrase_starts[q + 1];
            if (from < s.start_s - kTolS || to > s.end_s + kTolS) {
                continue;
            }
            std::vector<size_t> phrase;
            for (const size_t b : rows) {
                if (bars[b].start_s >= from - kTolS && bars[b].start_s < to - kTolS) {
                    phrase.push_back(b);
                }
            }
            if (phrase.size() < 3) {
                continue;
            }
            const size_t last = phrase.back();
            phrase.pop_back();
            if (!in_break[last] &&
                distance(features[last], mean_of(features, phrase)) >= p.fill_distance) {
                events.push_back({"fill", bars[last].start_s, bar_s});
            }
        }
    }
    std::sort(events.begin(), events.end(),
              [](const DetectedEvent &a, const DetectedEvent &b) { return a.start_s < b.start_s; });
    return events;
}

} // namespace

std::vector<DetectedEvent> detect_section_detail(std::vector<DetectedSection> &sections,
                                                 const std::vector<SectionBar> &bars,
                                                 const std::vector<BassAggregate> &bass_bars,
                                                 const std::vector<float> &intensity,
                                                 const std::vector<double> &phrase_starts,
                                                 double bar_s, const SectionParams &params) {
    if (bars.empty()) {
        return {};
    }
    const std::vector<std::vector<double>> features = scaled_features(bars, bass_bars);
    assign_phases(sections, bars, features, params.phase_distance);
    assign_roles(sections, bars, intensity, params);
    find_predrops(sections, bars, params);
    return find_events(sections, bars, features, phrase_starts, bar_s, params);
}

} // namespace psyight
