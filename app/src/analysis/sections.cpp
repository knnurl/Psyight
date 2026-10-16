#include "analysis/sections.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr int kBeatsPerBar = kSlotsPerBar / kSlotsPerBeat;

int band_index(const FrontEndResult &features, const char *name) {
    for (size_t b = 0; b < features.bands.size(); ++b) {
        if (features.bands[b].band.name == name) {
            return static_cast<int>(b);
        }
    }
    return -1;
}

} // namespace

Result<std::vector<DetectedSection>>
detect_sections(const FrontEndResult &f, const BeatTrackResult &beats, const SectionParams &p) {
    const int hats = band_index(f, "hats");
    if (hats < 0) {
        return Error{"section detection needs the front-end band hats"};
    }
    std::vector<DetectedSection> sections;
    if (beats.bpm <= 0.0 || beats.beat_has_kick.empty()) {
        return sections;
    }
    const double frame_s = static_cast<double>(f.hop_samples) / f.sample_rate_hz;
    const double duration_s = f.frames * frame_s;
    const double beat_s = 60.0 / beats.bpm;
    const double bar_s = kBeatsPerBar * beat_s;
    const double first_downbeat_s =
        samples_to_seconds(beats.grid.first_downbeat, beats.grid.sample_rate_hz);
    const auto first = static_cast<int>(
        std::lround((first_downbeat_s - beats.first_kick_beat_s) / beat_s)); // Beat of bar 0.

    // Per bar: kick state, beats agreeing with it, and the high band's level.
    std::vector<bool> kick;
    std::vector<int> kick_beats;
    std::vector<double> high_db;
    for (int beat = first; beat + kBeatsPerBar <= static_cast<int>(beats.beat_has_kick.size());
         beat += kBeatsPerBar) {
        int count = 0;
        for (int j = 0; j < kBeatsPerBar; ++j) {
            count += beats.beat_has_kick[beat + j];
        }
        kick.push_back(2 * count > kBeatsPerBar);
        kick_beats.push_back(count);
        const double bar_start_s = first_downbeat_s + (beat - first) * beat_s;
        const int begin = std::clamp(static_cast<int>(bar_start_s / frame_s), 0, f.frames - 1);
        const int end =
            std::clamp(static_cast<int>((bar_start_s + bar_s) / frame_s), begin + 1, f.frames);
        double power = 0.0;
        for (int i = begin; i < end; ++i) {
            power += std::pow(10.0, f.bands[hats].energy_db[i] / 10.0);
        }
        high_db.push_back(10.0 * std::log10(power / (end - begin) + 1e-12));
    }
    const int bars = static_cast<int>(kick.size());
    if (bars == 0) {
        return sections;
    }
    // A bar with exactly half its beats kicked is ambiguous (a groove with misses, or a
    // breakdown with stray detections): it takes the state of the nearest decisive bar. Then a
    // single kick bar between kickless bars is a blip inside a breakdown.
    for (int m = 0; m < bars; ++m) {
        if (2 * kick_beats[m] != kBeatsPerBar) {
            continue;
        }
        for (int d = 1; d < bars; ++d) {
            if (m - d >= 0 && 2 * kick_beats[m - d] != kBeatsPerBar) {
                kick[m] = kick[m - d];
                break;
            }
            if (m + d < bars && 2 * kick_beats[m + d] != kBeatsPerBar) {
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
        if (high_db[end - 1] >= high_db[quietest] + p.build_min_rise_db) {
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
    {&SectionParams::build_min_rise_db, "section.build_min_rise_db", "Build rise", "dB", 0.0, 40.0, ParamTier::tune, "High-band rise above the breakdown's quietest bar that marks a build."},
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
        spec.step = entry.member == &SectionParams::build_min_rise_db ? 0.0 : 1.0;
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
