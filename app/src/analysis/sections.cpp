#include "analysis/sections.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr int kBeatsPerBar = kSlotsPerBar / kSlotsPerBeat;

constexpr int kMinRiserBreakdownBars = 4; // Shorter breakdowns have no flatness baseline.
constexpr int kMaxRiserGapBars = 4;       // Bars between a riser's peak and the kick's return.

// The riser build's first bar in the breakdown [begin, end), or -1 without a riser.
int riser_start(const std::vector<SectionBar> &bars, int begin, int end, const SectionParams &p) {
    if (end - begin < kMinRiserBreakdownBars) {
        return -1;
    }
    std::vector<double> flatness;
    for (int k = begin; k < end; ++k) {
        flatness.push_back(bars[k].flatness);
    }
    std::sort(flatness.begin(), flatness.end());
    const double quartile = flatness[flatness.size() / 4];
    const int search_from =
        std::max(begin, end - static_cast<int>(std::lround(p.build_search_bars)));
    int peak = search_from;
    for (int k = search_from; k < end; ++k) {
        peak = bars[k].flatness > bars[peak].flatness ? k : peak;
    }
    const double peak_flatness = bars[peak].flatness;
    if (peak_flatness < p.build_min_flatness || peak_flatness < p.build_flatness_ratio * quartile) {
        return -1;
    }
    const double rising = quartile * std::sqrt(p.build_flatness_ratio);
    int start = peak;
    while (start - 1 >= begin && bars[start - 1].flatness >= rising) {
        --start;
    }
    // A riser grows out of the breakdown and peaks near its end; flatness that is high from the
    // breakdown's first bar is texture (a noisy pad, an FX tail), not a riser.
    if (start == begin || end - 1 - peak > kMaxRiserGapBars) {
        return -1;
    }
    return start;
}

int band_index(const FrontEndResult &features, const char *name) {
    for (size_t b = 0; b < features.bands.size(); ++b) {
        if (features.bands[b].band.name == name) {
            return static_cast<int>(b);
        }
    }
    return -1;
}

} // namespace

Result<std::vector<SectionBar>> section_bars(const FrontEndResult &f, const BeatTrackResult &beats,
                                             const ResidualFeatures *residual) {
    const int hats = band_index(f, "hats");
    if (hats < 0) {
        return Error{"section detection needs the front-end band hats"};
    }
    std::vector<SectionBar> out;
    if (beats.bpm <= 0.0 || beats.beat_has_kick.empty()) {
        return out;
    }
    const double frame_s = static_cast<double>(f.hop_samples) / f.sample_rate_hz;
    const double beat_s = 60.0 / beats.bpm;
    const double bar_s = kBeatsPerBar * beat_s;
    const double first_downbeat_s =
        samples_to_seconds(beats.grid.first_downbeat, beats.grid.sample_rate_hz);
    const auto first = static_cast<int>(
        std::lround((first_downbeat_s - beats.first_kick_beat_s) / beat_s)); // Beat of bar 0.

    // Per bar: kick state, beats agreeing with it, the high band's level and the residual.
    std::vector<bool> kick;
    for (int beat = first; beat + kBeatsPerBar <= static_cast<int>(beats.beat_has_kick.size());
         beat += kBeatsPerBar) {
        SectionBar bar;
        for (int j = 0; j < kBeatsPerBar; ++j) {
            bar.kick_beats += beats.beat_has_kick[beat + j];
        }
        kick.push_back(2 * bar.kick_beats > kBeatsPerBar);
        bar.start_s = first_downbeat_s + (beat - first) * beat_s;
        const int begin = std::clamp(static_cast<int>(bar.start_s / frame_s), 0, f.frames - 1);
        const int end =
            std::clamp(static_cast<int>((bar.start_s + bar_s) / frame_s), begin + 1, f.frames);
        double power = 0.0;
        for (int i = begin; i < end; ++i) {
            power += std::pow(10.0, f.bands[hats].energy_db[i] / 10.0);
        }
        bar.high_db = 10.0 * std::log10(power / (end - begin) + 1e-12);
        if (residual != nullptr) {
            const int last = std::min(end, static_cast<int>(residual->flatness.size()));
            int sounding = 0;
            for (int i = begin; i < last; ++i) {
                if (residual->centroid_hz[i] > 0.0f) { // 0 in silence.
                    bar.flatness += residual->flatness[i];
                    bar.centroid_hz += residual->centroid_hz[i];
                    ++sounding;
                }
            }
            if (sounding > 0) {
                bar.flatness /= sounding;
                bar.centroid_hz /= sounding;
            }
        }
        out.push_back(bar);
    }
    const int bars = static_cast<int>(out.size());
    // A bar with exactly half its beats kicked is ambiguous (a groove with misses, or a
    // breakdown with stray detections): it takes the state of the nearest decisive bar. Then a
    // single kick bar between kickless bars is a blip inside a breakdown.
    for (int m = 0; m < bars; ++m) {
        if (2 * out[m].kick_beats != kBeatsPerBar) {
            continue;
        }
        for (int d = 1; d < bars; ++d) {
            if (m - d >= 0 && 2 * out[m - d].kick_beats != kBeatsPerBar) {
                kick[m] = kick[m - d];
                break;
            }
            if (m + d < bars && 2 * out[m + d].kick_beats != kBeatsPerBar) {
                kick[m] = kick[m + d];
                break;
            }
        }
    }
    for (int m = 1; m + 1 < bars; ++m) {
        if (kick[m] && !kick[m - 1] && !kick[m + 1]) {
            kick[m] = false;
        }
    }
    for (int m = 0; m < bars; ++m) {
        out[m].kick = kick[m];
    }
    return out;
}

Result<std::vector<DetectedSection>> detect_sections(const FrontEndResult &f,
                                                     const BeatTrackResult &beats,
                                                     const SectionParams &p,
                                                     const ResidualFeatures *residual) {
    Result<std::vector<SectionBar>> found = section_bars(f, beats, residual);
    if (!found) {
        return found.error();
    }
    std::vector<DetectedSection> sections;
    const std::vector<SectionBar> &bar_list = found.value();
    const int bars = static_cast<int>(bar_list.size());
    if (bars == 0) {
        return sections;
    }
    const double frame_s = static_cast<double>(f.hop_samples) / f.sample_rate_hz;
    const double duration_s = f.frames * frame_s;
    const double bar_s = kBeatsPerBar * 60.0 / beats.bpm;
    const double first_downbeat_s = bar_list.front().start_s;
    std::vector<bool> kick(bars);
    std::vector<int> kick_beats(bars);
    std::vector<double> high_db(bars);
    for (int m = 0; m < bars; ++m) {
        kick[m] = bar_list[m].kick;
        kick_beats[m] = bar_list[m].kick_beats;
        high_db[m] = bar_list[m].high_db;
    }
    std::vector<std::string> label(bars, "groove");
    const auto first_kick = std::find(kick.begin(), kick.end(), true) - kick.begin();
    const auto last_kick = bars - 1 - (std::find(kick.rbegin(), kick.rend(), true) - kick.rbegin());
    for (int m = 0; m < bars; ++m) {
        if (m < first_kick) {
            label[m] = "intro";
        } else if (m > last_kick) {
            label[m] = "outro";
        }
    }
    // Breakdowns, builds and drops between the first and last kick bars.
    for (int m = static_cast<int>(first_kick); m <= last_kick;) {
        if (kick[m]) {
            ++m;
            continue;
        }
        const int begin = m;
        while (m <= last_kick && !kick[m]) {
            ++m;
        }
        const int end = m; // The bar where the kick returns.
        if (end - begin < p.min_breakdown_bars) {
            continue; // A fill: the groove goes on.
        }
        int quietest = begin;
        for (int k = begin; k < end; ++k) {
            label[k] = "breakdown";
            quietest = high_db[k] < high_db[quietest] ? k : quietest;
        }
        if (const int riser = riser_start(bar_list, begin, end, p); riser >= 0) {
            for (int k = riser; k < end; ++k) {
                label[k] = "build";
            }
        } else if (high_db[end - 1] >= high_db[quietest] + p.build_min_rise_db) {
            int build = end - 1;
            while (build - 1 > quietest &&
                   high_db[build - 1] >= high_db[quietest] + 0.5 * p.build_min_rise_db) {
                --build;
            }
            for (int k = build; k < end; ++k) {
                label[k] = "build";
            }
        }
        if (end - begin >= p.drop_min_breakdown_bars) {
            for (int k = end; k < std::min(bars, end + static_cast<int>(std::lround(p.drop_bars)));
                 ++k) {
                if (!kick[k]) {
                    break;
                }
                label[k] = "drop";
            }
        }
    }

    // Merge runs of equal labels.
    for (int m = 0; m < bars;) {
        int n = m;
        int agree = 0;
        while (n < bars && label[n] == label[m]) {
            agree += kick[n] ? kick_beats[n] : kBeatsPerBar - kick_beats[n];
            ++n;
        }
        DetectedSection section;
        section.start_s = m == 0 ? 0.0 : first_downbeat_s + m * bar_s;
        section.end_s = n == bars ? duration_s : first_downbeat_s + n * bar_s;
        section.label = label[m];
        section.confidence = static_cast<double>(agree) / ((n - m) * kBeatsPerBar);
        sections.push_back(section);
        m = n;
    }
    return sections;
}

std::vector<DetectedSection>
sections_from_starts(std::vector<std::pair<double, std::string>> starts, double duration_s) {
    std::sort(starts.begin(), starts.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    std::erase_if(starts, [&](const auto &s) { return s.first >= duration_s; });
    std::vector<DetectedSection> sections;
    for (size_t i = 0; i < starts.size(); ++i) {
        DetectedSection s;
        s.start_s = i == 0 ? 0.0 : starts[i].first;
        s.end_s = i + 1 < starts.size() ? starts[i + 1].first : duration_s;
        s.label = starts[i].second;
        s.confidence = 1.0;
        if (s.end_s > s.start_s) {
            sections.push_back(std::move(s));
        }
    }
    return sections;
}

std::vector<double> phrase_starts(const Grid &grid, const std::vector<DetectedSection> &sections,
                                  int bars_per_phrase, int phrase_bar_offset, double duration_s) {
    std::vector<double> starts;
    const double bar_s = kSlotsPerBar * grid.slot_length_samples() / grid.sample_rate_hz;
    if (bar_s <= 0.0 || bars_per_phrase < 1) {
        return starts;
    }
    const double first_s = samples_to_seconds(grid.first_downbeat, grid.sample_rate_hz);
    // Count origins: the phrase offset, then each section start after the first.
    std::vector<double> origins{first_s + phrase_bar_offset * bar_s};
    for (size_t i = 1; i < sections.size(); ++i) {
        origins.push_back(sections[i].start_s);
    }
    const double phrase_s = bars_per_phrase * bar_s;
    for (size_t i = 0; i < origins.size(); ++i) {
        const double until_s = i + 1 < origins.size() ? origins[i + 1] : duration_s;
        double t = origins[i];
        if (i == 0) {
            while (t - phrase_s >= 0.0) {
                t -= phrase_s; // Phrases before the offset, back to the start.
            }
        }
        for (; t < until_s - 0.5 * bar_s && t < duration_s; t += phrase_s) {
            if (t >= 0.0) {
                starts.push_back(t);
            }
        }
    }
    return starts;
}

namespace {

struct ParamEntry {
    double SectionParams::*member;
    const char *id;
    const char *label;
    const char *unit;
    double min;
    double max;
    ParamTier tier;
    const char *description;
};

const SectionParams kDefaults;

// clang-format off
const ParamEntry kParamTable[] = {
    {&SectionParams::min_breakdown_bars, "section.min_breakdown_bars", "Breakdown minimum", "bars", 1.0, 16.0, ParamTier::tune, "Bars without kicks that make a breakdown; shorter gaps are fills."},
    {&SectionParams::build_min_rise_db, "section.build_min_rise_db", "Build rise", "dB", 0.0, 40.0, ParamTier::tune, "High-band rise above the breakdown's quietest bar that marks a build, when no riser is found."},
    {&SectionParams::build_flatness_ratio, "section.build_flatness_ratio", "Riser ratio", "x", 1.0, 20.0, ParamTier::tune, "A riser's flatness peak over the breakdown's lower-quartile flatness."},
    {&SectionParams::build_min_flatness, "section.build_min_flatness", "Riser flatness", "", 0.0, 1.0, ParamTier::engineer, "A riser's flatness peak is at least this (0 tonal, 1 white noise)."},
    {&SectionParams::build_search_bars, "section.build_search_bars", "Riser search", "bars", 1.0, 32.0, ParamTier::engineer, "A riser peaks within this many bars before the kick's return."},
    {&SectionParams::drop_min_breakdown_bars, "section.drop_min_breakdown_bars", "Drop after", "bars", 1.0, 64.0, ParamTier::tune, "A breakdown at least this long ends in a drop (SPEC A9)."},
    {&SectionParams::drop_bars, "section.drop_bars", "Drop length", "bars", 1.0, 64.0, ParamTier::tune, "Bars after the kick's return labelled drop."},
};
// clang-format on

} // namespace

Result<void> declare_section_params(ParamRegistry &registry) {
    for (const ParamEntry &entry : kParamTable) {
        ParamSpec spec;
        spec.id = entry.id;
        spec.label = entry.label;
        spec.unit = entry.unit;
        spec.min = entry.min;
        spec.max = entry.max;
        spec.default_value = kDefaults.*entry.member;
        const bool continuous = entry.member == &SectionParams::build_min_rise_db ||
                                entry.member == &SectionParams::build_flatness_ratio ||
                                entry.member == &SectionParams::build_min_flatness;
        spec.step = continuous ? 0.0 : 1.0;
        spec.tier = entry.tier;
        spec.description = entry.description;
        spec.group = "Sections";
        if (Result<void> declared = registry.declare(spec); !declared) {
            return declared;
        }
    }
    return {};
}

SectionParams section_params_from(const ParamValues &values) {
    SectionParams params;
    for (const ParamEntry &entry : kParamTable) {
        if (const auto it = values.find(entry.id); it != values.end()) {
            params.*entry.member = it->second;
        }
    }
    return params;
}

} // namespace psyight
