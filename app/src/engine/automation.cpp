#include "engine/automation.h"

#include "analysis/intensity.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace psyight {

namespace {

std::vector<int> tagged(const std::vector<Scene> &scenes, const std::string &label) {
    std::vector<int> out;
    for (size_t i = 0; i < scenes.size(); ++i) {
        const std::vector<std::string> &s = scenes[i].sections;
        if (std::find(s.begin(), s.end(), label) != s.end()) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

bool cuts_into(const std::string &label) { return label == "groove" || label == "drop"; }

constexpr double kPhraseToleranceS = 0.05;

// Too short to change the scene for.
bool held(const DetectedSection &section, double min_dwell_s, HoldRule rule) {
    const double dwell_s =
        rule == HoldRule::tolerant ? min_dwell_s - kPhraseToleranceS : min_dwell_s;
    return section.label != "build" && section.label != "drop" &&
           section.end_s - section.start_s < dwell_s;
}

// A build shorter than a phrase (tolerant rule): sections held after it look past it.
bool short_build(const DetectedSection &section, double min_dwell_s) {
    return section.label == "build" &&
           section.end_s - section.start_s < min_dwell_s - kPhraseToleranceS;
}

int wrap(int i, int n) { return (i % n + n) % n; }

// The section whose scene section `index` shows: itself, or the one before a held blip (tolerant:
// and before a short build that held sections follow).
size_t own_section(const std::vector<DetectedSection> &sections, size_t index, double min_dwell_s,
                   HoldRule rule) {
    if (rule == HoldRule::strict) {
        while (index > 0 && held(sections[index], min_dwell_s, rule)) {
            --index;
        }
        return index;
    }
    const size_t from = index;
    while (index > 0 && (held(sections[index], min_dwell_s, rule) ||
                         (index != from && short_build(sections[index], min_dwell_s)))) {
        --index;
    }
    return index;
}

// Earlier sections with the same label (blips aside): the rotation between sections.
size_t occurrence_of(const std::vector<DetectedSection> &sections, size_t index, double min_dwell_s,
                     HoldRule rule) {
    size_t occurrence = 0;
    for (size_t j = 0; j < index; ++j) {
        occurrence +=
            sections[j].label == sections[index].label && !held(sections[j], min_dwell_s, rule);
    }
    return occurrence;
}

// Phrases a section starting at start_s has run by t_s: phrase starts after its own, up to t_s.
long long phrases_run(const std::vector<double> &phrase_starts, double start_s, double t_s) {
    const auto from =
        std::upper_bound(phrase_starts.begin(), phrase_starts.end(), start_s + kPhraseToleranceS);
    const auto to = std::upper_bound(phrase_starts.begin(), phrase_starts.end(), t_s);
    return std::max<long long>(0, to - from);
}

// Rotations within a section after `phrases` phrases (none in builds).
size_t rotation_step(const AutomationParams &params, const std::string &label, long long phrases) {
    if (params.rotate_every_phrases <= 0 || label == "build" || phrases <= 0) {
        return 0;
    }
    return static_cast<size_t>(phrases / params.rotate_every_phrases);
}

// The fill kind: `fill`, or for mix one of the five, seeded by the key and the phrase.
PhraseFill pick_fill(PhraseFill fill, std::uint64_t key, long long phrase) {
    if (fill != PhraseFill::mix) {
        return fill;
    }
    std::uint64_t h = key * 0x9e3779b97f4a7c15ull + static_cast<std::uint64_t>(phrase);
    h ^= h >> 29;
    h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 32;
    return kPhraseFills[1 + h % 5];
}

// The fill over the end of phrase `phrase` (0-based within its section) that ends at end_s, if
// t_s is inside it. last_phrase: the section ends with it (no fill). key: varies the mix's pick.
void place_fill(SceneMix &mix, const AutomationParams &params, const std::string &label,
                long long phrase, double end_s, bool last_phrase, double t_s, double beat_s,
                std::uint64_t key) {
    if (params.fill == PhraseFill::off || params.fill_beats <= 0.0 || last_phrase ||
        (label != "groove" && label != "drop")) {
        return;
    }
    const bool every =
        params.fill_every_phrases > 0 && (phrase + 1) % params.fill_every_phrases == 0;
    const bool before_rotation =
        rotation_step(params, label, phrase + 1) != rotation_step(params, label, phrase);
    const double fill_s = params.fill_beats * beat_s;
    if (!(every || before_rotation) || t_s < end_s - fill_s || t_s >= end_s) {
        return;
    }
    mix.fill = pick_fill(params.fill, key, phrase);
    mix.fill_s = fill_s;
    mix.fill_progress = (t_s - (end_s - fill_s)) / fill_s;
}

// Fills from events (file mode): a break_fill over each break; with fill_source events or both,
// a fill event in a groove or drop plays its fill (pick_fill, seeded as a phrase-end fill), and
// with both a phrase-end fill overlapping a fill event is dropped.
void place_event_fills(SceneMix &mix, const AutomationParams &params,
                       const std::vector<DetectedEvent> &events, const std::string &label,
                       long long phrase, double t_s, std::uint64_t key) {
    const auto inside = [&](const DetectedEvent &e) {
        return e.length_s > 0.0 && t_s >= e.start_s && t_s < e.start_s + e.length_s;
    };
    if (params.fill_source != FillSource::phrase) {
        if (params.fill_source == FillSource::events) {
            mix.fill = PhraseFill::off;
        } else if (mix.fill != PhraseFill::off) {
            const double from = t_s - mix.fill_progress * mix.fill_s;
            const double to = from + mix.fill_s;
            for (const DetectedEvent &e : events) {
                if (e.kind == "fill" && e.start_s < to && e.start_s + e.length_s > from) {
                    mix.fill = PhraseFill::off;
                }
            }
        }
        if (params.fill != PhraseFill::off && (label == "groove" || label == "drop")) {
            for (const DetectedEvent &e : events) {
                if (e.kind == "fill" && inside(e)) {
                    mix.fill = pick_fill(params.fill, key, phrase);
                    mix.fill_s = e.length_s;
                    mix.fill_progress = (t_s - e.start_s) / e.length_s;
                }
            }
        }
    }
    if (params.break_fill != PhraseFill::off && params.break_fill != PhraseFill::mix) {
        for (const DetectedEvent &e : events) {
            if (e.kind == "break" && inside(e)) {
                mix.fill = params.break_fill;
                mix.fill_s = e.length_s;
                mix.fill_progress = (t_s - e.start_s) / e.length_s;
            }
        }
    }
}

// Phrases into the current rotation step.
long long phrases_into_step(const AutomationParams &params, const std::string &label,
                            long long phrases) {
    return params.rotate_every_phrases > 0 && label != "build"
               ? phrases % params.rotate_every_phrases
               : phrases;
}

// The variation `phrases` phrases into a rotation step: the scene itself for the first period,
// then seeded by key, never the same twice running.
int variant_at(const AutomationParams &params, const std::string &label, long long phrases,
               std::uint64_t key) {
    if (params.vary_every_phrases <= 0 || label == "build") {
        return 0;
    }
    const long long periods = phrases / params.vary_every_phrases;
    int previous = 0;
    for (long long m = 1; m <= periods; ++m) {
        std::uint64_t h = key * 0x9e3779b97f4a7c15ull + static_cast<std::uint64_t>(m);
        h ^= h >> 31;
        h *= 0xbf58476d1ce4e5b9ull;
        h ^= h >> 29;
        int v = 1 + static_cast<int>(h % (kSceneVariants - 1));
        if (v == previous) {
            v = v % (kSceneVariants - 1) + 1;
        }
        previous = v;
    }
    return previous;
}

struct Look {
    int scene = 0;
    int variant = 0;
    bool operator==(const Look &) const = default;
};

// The look `phrases` phrases into a section (by label, occurrence and energy; key seeds its
// variations).
Look look_in_section(const std::vector<Scene> &scenes, const AutomationParams &params,
                     const std::string &label, size_t occurrence, int energy, long long phrases,
                     std::uint64_t key) {
    const size_t step = rotation_step(params, label, phrases);
    return {scene_for_label(scenes, label, occurrence, energy, step),
            variant_at(params, label, phrases_into_step(params, label, phrases), key + step)};
}

// The look changed at the start of the current phrase (phrases > 0), `since_s` ago: a fade from
// the one before, as a rotation fades.
void fade_change(SceneMix &mix, const Look &before, double since_s, double beat_s,
                 const AutomationParams &params) {
    const double fade_s = params.rotate_fade_beats * beat_s;
    if (before != Look{mix.to, mix.variant} && fade_s > 0.0 && since_s >= 0.0 && since_s < fade_s) {
        mix.from = before.scene;
        mix.variant_from = before.variant;
        mix.to_weight = since_s / fade_s;
    }
}

// A phrase-end fill over the finished frame (SceneMix::fill).
void apply_fill(Frame &frame, const SceneMix &mix, const Rig &rig, const EngineInputs &inputs) {
    const double p = std::clamp(mix.fill_progress, 0.0, 1.0);
    const double cx = 0.5 * (rig.min.x + rig.max.x);
    const double cy = 0.5 * (rig.min.y + rig.max.y);
    const double width = std::max(rig.max.x - rig.min.x, 1e-6);
    const double radius =
        std::max({0.5 * (rig.max.x - rig.min.x), 0.5 * (rig.max.y - rig.min.y), 1e-6});
    const auto light = [](Rgb &c, double k) { // Towards white, keeping what is lit.
        const auto w = static_cast<float>(std::clamp(k, 0.0, 1.0));
        c = {std::max(c.r, w), std::max(c.g, w), std::max(c.b, w)};
    };
    for (size_t i = 0; i < frame.size() && i < rig.pixels.size(); ++i) {
        Rgb &c = frame[i];
        const Vec3 &q = rig.pixels[i].position;
        switch (mix.fill) {
        case PhraseFill::gap: // Dark, after a 50 ms fade.
            c = scaled(c, std::max(0.0, 1.0 - p * mix.fill_s / 0.05));
            break;
        case PhraseFill::stutter: // Open for the first half of each 16th; a dip, not dark (the
                                  // flash limiter would flatten a full 10 Hz chop).
            c = scaled(c, inputs.slot_phase < 0.5 ? 1.0 : 0.35);
            break;
        case PhraseFill::sweep: { // A band from left to right.
            const double band = 0.12 * width;
            const double at = rig.min.x - band + p * (width + 2.0 * band);
            const double d = (q.x - at) / band;
            light(c, 0.8 * std::exp(-d * d));
            break;
        }
        case PhraseFill::implode: { // A ring closing on the centre.
            const double r = std::hypot(q.x - cx, q.y - cy) / radius;
            const double d = (r - (1.0 - p)) / 0.08;
            light(c, 0.8 * std::exp(-d * d));
            break;
        }
        case PhraseFill::pulse: { // Pumping on the beats.
            const double fall = 1.0 - inputs.beat_phase;
            c = scaled(c, 0.3 + 0.7 * fall * fall);
            break;
        }
        case PhraseFill::off:
        case PhraseFill::mix:
            return;
        }
    }
}

void mix_into(Frame &frame, const Frame &other, double other_weight) {
    const auto w = static_cast<float>(std::clamp(other_weight, 0.0, 1.0));
    for (size_t i = 0; i < frame.size() && i < other.size(); ++i) {
        frame[i] = blend(frame[i], other[i], BlendMode::alpha, w);
    }
}

} // namespace

int scene_for_label(const std::vector<Scene> &scenes, const std::string &label, size_t occurrence,
                    int energy, size_t step) {
    std::vector<int> candidates = tagged(scenes, label);
    if (candidates.empty() && label == "drop") {
        candidates = tagged(scenes, "groove");
    }
    if (energy >= 0 && energy < static_cast<int>(kEnergyTags.size())) {
        const auto has = [&](int scene, const char *tag) {
            const std::vector<std::string> &s = scenes[static_cast<size_t>(scene)].sections;
            return std::find(s.begin(), s.end(), tag) != s.end();
        };
        std::vector<int> matching;
        std::vector<int> untagged;
        for (const int c : candidates) {
            if (has(c, kEnergyTags[static_cast<size_t>(energy)])) {
                matching.push_back(c);
            } else if (std::none_of(kEnergyTags.begin(), kEnergyTags.end(),
                                    [&](const char *tag) { return has(c, tag); })) {
                untagged.push_back(c);
            }
        }
        if (!matching.empty()) {
            candidates = std::move(matching);
        } else if (!untagged.empty()) {
            candidates = std::move(untagged);
        }
    }
    if (candidates.empty()) {
        return 0;
    }
    return candidates[(occurrence + step) % candidates.size()];
}

int scene_for_section(const std::vector<Scene> &scenes,
                      const std::vector<DetectedSection> &sections, size_t index,
                      double min_dwell_s, HoldRule rule) {
    if (scenes.empty() || index >= sections.size()) {
        return 0;
    }
    index = own_section(sections, index, min_dwell_s, rule);
    return scene_for_label(scenes, sections[index].label,
                           occurrence_of(sections, index, min_dwell_s, rule),
                           sections[index].energy);
}

SceneMix automate(const std::vector<Scene> &scenes, const TrackAnalysis &analysis, const Grid &grid,
                  const EngineInputs &inputs, const AutomationParams &params, int manual_scene,
                  const SectionTimeline *timeline) {
    SceneMix mix;
    mix.palette_rotation = params.palette_rotation;
    const std::vector<double> &phrase_starts =
        timeline != nullptr ? timeline->phrase_starts_s : analysis.phrase_starts_s;
    const std::vector<DetectedSection> &sections =
        timeline != nullptr ? timeline->sections : analysis.sections;
    const double beat_s = grid.bpm > 0.0 ? 60.0 / grid.bpm : 0.5;
    const double phrase_s = 4.0 * BeatTrackerParams{}.bars_per_phrase * beat_s;

    // Palette rotation by phrase.
    if (params.palette_every_phrases > 0 && inputs.phrase_index >= 0) {
        const int n = params.palette_every_phrases;
        mix.palette_shift = inputs.phrase_index / n;
        mix.palette_shift_from = mix.palette_shift;
        const auto phrase = static_cast<size_t>(inputs.phrase_index);
        const double since_s =
            phrase < phrase_starts.size() ? inputs.t_s - phrase_starts[phrase] : 0.0;
        const double slew_s = params.palette_slew_beats * beat_s;
        if (inputs.phrase_index % n == 0 && inputs.phrase_index > 0 && since_s < slew_s) {
            mix.palette_shift_from = mix.palette_shift - 1;
            mix.palette_weight = since_s / slew_s;
        }
    }

    mix.brightness = 1.0 - std::clamp(params.energy_dim, 0.0, 1.0) * (1.0 - inputs.intensity);
    if (scenes.empty()) {
        return mix;
    }
    if (manual_scene >= 0 && manual_scene < static_cast<int>(scenes.size())) {
        mix.to = manual_scene;
        return mix;
    }
    if (inputs.section_index < 0) {
        mix.to = 0;
        return mix;
    }
    const auto index = static_cast<size_t>(inputs.section_index);
    { // A phrase-end fill, and the fills from events.
        const DetectedSection &own =
            sections[own_section(sections, index, phrase_s, params.hold_rule)];
        const long long phrase = phrases_run(phrase_starts, own.start_s, inputs.t_s);
        const auto next = static_cast<size_t>(inputs.phrase_index + 1);
        const auto key = static_cast<std::uint64_t>(std::llround(own.start_s * 1000.0));
        if (inputs.phrase_index >= 0 && next < phrase_starts.size()) {
            const double end_s = phrase_starts[next];
            place_fill(mix, params, own.label, phrase, end_s,
                       end_s >= sections[index].end_s - kPhraseToleranceS, inputs.t_s, beat_s, key);
        }
        if (params.fill_source != FillSource::phrase || params.break_fill != PhraseFill::off) {
            place_event_fills(mix, params,
                              engine_events(analysis, timeline, params.detected_events), own.label,
                              phrase, inputs.t_s, key);
        }
    }
    // The energy of a section's p-th phrase: its bars' mean intensity in thirds (energy_scenes),
    // else the section's.
    const auto phrase_energy = [&](const DetectedSection &s, long long p) {
        if (!params.energy_scenes || analysis.intensity.empty()) {
            return s.energy;
        }
        const auto first =
            static_cast<size_t>(std::lower_bound(phrase_starts.begin(), phrase_starts.end(),
                                                 s.start_s - kPhraseToleranceS) -
                                phrase_starts.begin());
        const size_t k = first + static_cast<size_t>(std::max(0LL, p));
        const double from = k < phrase_starts.size() ? phrase_starts[k] : s.start_s;
        const double to =
            k + 1 < phrase_starts.size() ? std::min(phrase_starts[k + 1], s.end_s) : s.end_s;
        double sum = 0.0;
        int count = 0;
        for (size_t b = 0; b < analysis.section_bars.size() && b < analysis.intensity.size(); ++b) {
            const double start = analysis.section_bars[b].start_s;
            if (start >= from - kPhraseToleranceS && start < to - kPhraseToleranceS) {
                sum += analysis.intensity[b];
                ++count;
            }
        }
        const double mean =
            count > 0 ? sum / count
                      : intensity_at(analysis.section_bars, analysis.intensity, 0.5 * (from + to));
        return mean < 1.0 / 3.0 ? 0 : mean < 2.0 / 3.0 ? 1 : 2;
    };
    // The look of section i at time t_s: its own section's (or the one before a blip), rotated
    // and varied by the phrases it has run.
    const auto look_at = [&](size_t i, double t_s, long long *phrases_out) {
        const size_t own = own_section(sections, i, phrase_s, params.hold_rule);
        const DetectedSection &s = sections[own];
        const long long phrases = phrases_run(phrase_starts, s.start_s, t_s);
        if (phrases_out != nullptr) {
            *phrases_out = phrases;
        }
        const auto key = static_cast<std::uint64_t>(std::llround(s.start_s * 1000.0));
        const size_t occurrence = occurrence_of(sections, own, phrase_s, params.hold_rule);
        return std::pair{look_in_section(scenes, params, s.label, occurrence,
                                         phrase_energy(s, phrases), phrases, key),
                         phrases > 0
                             ? look_in_section(scenes, params, s.label, occurrence,
                                               phrase_energy(s, phrases - 1), phrases - 1, key)
                             : Look{-1, 0}};
    };
    long long phrases = 0;
    const auto [now, before] = look_at(index, inputs.t_s, &phrases);
    mix.to = now.scene;
    mix.variant = now.variant;
    if (phrases > 0 && before != now && inputs.phrase_index >= 0 &&
        static_cast<size_t>(inputs.phrase_index) < phrase_starts.size()) {
        // This phrase began a rotation or a new variation.
        fade_change(mix, before,
                    inputs.t_s - phrase_starts[static_cast<size_t>(inputs.phrase_index)], beat_s,
                    params);
        return mix;
    }
    if (index == 0 || cuts_into(inputs.section)) {
        return mix;
    }
    // The look the section before ended on.
    const Look previous = look_at(index - 1, sections[index].start_s - 1e-3, nullptr).first;
    const double fade_s = std::min(params.crossfade_beats * beat_s, 0.5 * inputs.section_length_s);
    if (previous != now && fade_s > 0.0 && inputs.since_section_s < fade_s) {
        mix.from = previous.scene;
        mix.variant_from = previous.variant;
        mix.to_weight = inputs.since_section_s / fade_s;
    }
    return mix;
}

SceneMix automate_live(const std::vector<Scene> &scenes, const LiveState &state,
                       const EngineInputs &inputs, const AutomationParams &params,
                       int manual_scene) {
    SceneMix mix;
    mix.palette_rotation = params.palette_rotation;
    const double beat_s = state.tempo_known ? state.period_s : 0.5;
    if (params.palette_every_phrases > 0 && inputs.phrase_index >= 0) {
        const int n = params.palette_every_phrases;
        mix.palette_shift = inputs.phrase_index / n;
        mix.palette_shift_from = mix.palette_shift;
        const double since_s = inputs.phrase_phase * 4.0 * state.phrase_bars * beat_s;
        const double slew_s = params.palette_slew_beats * beat_s;
        if (inputs.phrase_index % n == 0 && inputs.phrase_index > 0 && since_s < slew_s) {
            mix.palette_shift_from = mix.palette_shift - 1;
            mix.palette_weight = since_s / slew_s;
        }
    }
    mix.brightness = 1.0 - std::clamp(params.energy_dim, 0.0, 1.0) * (1.0 - inputs.intensity);
    if (scenes.empty()) {
        return mix;
    }
    if (manual_scene >= 0 && manual_scene < static_cast<int>(scenes.size())) {
        mix.to = manual_scene;
        return mix;
    }
    if (state.section_count == 0) {
        mix.to = 0;
        return mix;
    }
    const auto occurrence = [&](LiveSection s) {
        return static_cast<size_t>(
            std::max(0, state.section_occurrences[static_cast<size_t>(s)] - 1));
    };
    // Phrases restart at each section (LiveBeat::set_section), phrase_bars bars each.
    const double phrase_s = 4.0 * state.phrase_bars * beat_s;
    const double into_s = std::max(0.0, inputs.t_s - state.section_start_s);
    const auto phrases = static_cast<long long>(std::floor(into_s / phrase_s + 1e-9));
    const auto ended_phrases = [&](double length_s) {
        return static_cast<long long>(std::floor(std::max(0.0, length_s / phrase_s - 1e-6)));
    };
    // The section whose look this one shows: itself, or (live_resume) the groove or drop before
    // a short breakdown, its phrases going on from where it ended.
    const bool resume = params.live_resume && state.section == LiveSection::groove &&
                        state.has_previous && state.previous_section == LiveSection::breakdown &&
                        state.previous_length_s < phrase_s - 1e-6 && state.has_earlier &&
                        (state.earlier_section == LiveSection::groove ||
                         state.earlier_section == LiveSection::drop);
    const std::string label = resume ? live_section_name(state.earlier_section) : inputs.section;
    const size_t shown_occurrence =
        resume ? static_cast<size_t>(state.earlier_occurrence) : occurrence(state.section);
    const int energy = resume ? state.earlier_energy : state.section_energy;
    const long long shown_phrases =
        resume ? ended_phrases(state.earlier_length_s) + phrases : phrases;
    const auto key = static_cast<std::uint64_t>(state.section_count - (resume ? 2 : 0));
    place_fill(mix, params, inputs.section, shown_phrases,
               state.section_start_s + static_cast<double>(phrases + 1) * phrase_s, false,
               inputs.t_s, beat_s, key);
    const Look now =
        look_in_section(scenes, params, label, shown_occurrence, energy, shown_phrases, key);
    mix.to = now.scene;
    mix.variant = now.variant;
    // The look a section ended on (`back` sections before this one).
    const auto ended_look = [&](LiveSection section, int occ, int energy, double length_s,
                                int back) {
        return look_in_section(scenes, params, live_section_name(section), static_cast<size_t>(occ),
                               energy, ended_phrases(length_s),
                               static_cast<std::uint64_t>(state.section_count - back));
    };
    // The look the section before ended on, as shown: a breakdown shorter than the commit delay
    // never showed its own, so then the one before it.
    const double wait_s = std::clamp(params.live_breakdown_commit_bars, 0, 8) * 4.0 * beat_s;
    const auto previous_look = [&] {
        if (wait_s > 0.0 && state.previous_section == LiveSection::breakdown &&
            state.previous_length_s < wait_s && state.has_earlier) {
            return ended_look(state.earlier_section, state.earlier_occurrence, state.earlier_energy,
                              state.earlier_length_s, 2);
        }
        return ended_look(state.previous_section, state.previous_occurrence, state.previous_energy,
                          state.previous_length_s, 1);
    };
    const bool waiting = wait_s > 0.0 && state.section == LiveSection::breakdown;
    if (waiting && state.has_previous && into_s < wait_s) { // Not committed yet.
        const Look held = previous_look();
        mix.to = held.scene;
        mix.variant = held.variant;
        return mix;
    }
    if (phrases > 0) {
        const Look before = look_in_section(scenes, params, label, shown_occurrence, energy,
                                            shown_phrases - 1, key);
        if (before != now) { // This phrase began a rotation or a new variation.
            fade_change(mix, before, into_s - static_cast<double>(phrases) * phrase_s, beat_s,
                        params);
            return mix;
        }
    }
    if (!state.has_previous || cuts_into(inputs.section)) {
        return mix;
    }
    const Look previous = previous_look();
    // A breakdown that waited crossfades from the end of its wait.
    const double since_s = waiting ? into_s - wait_s : inputs.since_section_s;
    const double fade_s = std::min(params.crossfade_beats * beat_s, 0.5 * inputs.section_length_s);
    if (previous != now && fade_s > 0.0 && since_s < fade_s) {
        mix.from = previous.scene;
        mix.variant_from = previous.variant;
        mix.to_weight = since_s / fade_s;
    }
    return mix;
}

const char *phrase_fill_id(PhraseFill fill) {
    switch (fill) {
    case PhraseFill::off:
        return "off";
    case PhraseFill::gap:
        return "gap";
    case PhraseFill::stutter:
        return "stutter";
    case PhraseFill::sweep:
        return "sweep";
    case PhraseFill::implode:
        return "implode";
    case PhraseFill::pulse:
        return "pulse";
    case PhraseFill::mix:
        return "mix";
    }
    return "off";
}

const char *phrase_fill_name(PhraseFill fill) {
    switch (fill) {
    case PhraseFill::off:
        return "Off";
    case PhraseFill::gap:
        return "Gap";
    case PhraseFill::stutter:
        return "Stutter";
    case PhraseFill::sweep:
        return "Sweep";
    case PhraseFill::implode:
        return "Implode";
    case PhraseFill::pulse:
        return "Pulse";
    case PhraseFill::mix:
        return "Mix";
    }
    return "Off";
}

std::optional<PhraseFill> phrase_fill_from_id(const std::string &id) {
    for (const PhraseFill f : kPhraseFills) {
        if (id == phrase_fill_id(f)) {
            return f;
        }
    }
    return std::nullopt;
}

const char *scene_launch_id(SceneLaunch launch) {
    switch (launch) {
    case SceneLaunch::now:
        return "now";
    case SceneLaunch::bar:
        return "bar";
    case SceneLaunch::phrase:
        return "phrase";
    }
    return "bar";
}

const char *scene_launch_name(SceneLaunch launch) {
    switch (launch) {
    case SceneLaunch::now:
        return "Now";
    case SceneLaunch::bar:
        return "Next bar";
    case SceneLaunch::phrase:
        return "Next phrase";
    }
    return "Next bar";
}

int scene_palette(const Scene &scene, int shift, size_t palette_count, PaletteRotation rotation) {
    const auto n = static_cast<int>(palette_count);
    if (rotation == PaletteRotation::all) {
        return wrap(scene.palette + shift, n);
    }
    if (scene.palette_list.empty()) {
        return wrap(scene.palette, n);
    }
    const auto size = static_cast<int>(scene.palette_list.size());
    return wrap(scene.palette_list[static_cast<size_t>(wrap(shift, size))], n);
}

const char *palette_rotation_id(PaletteRotation rotation) {
    return rotation == PaletteRotation::scene_list ? "scene_list" : "all";
}

std::optional<PaletteRotation> palette_rotation_from_id(const std::string &id) {
    if (id == "all") {
        return PaletteRotation::all;
    }
    if (id == "scene_list") {
        return PaletteRotation::scene_list;
    }
    return std::nullopt;
}

const char *fill_source_id(FillSource source) {
    return source == FillSource::events ? "events" : source == FillSource::both ? "both" : "phrase";
}

std::optional<FillSource> fill_source_from_id(const std::string &id) {
    for (const FillSource s : {FillSource::phrase, FillSource::events, FillSource::both}) {
        if (id == fill_source_id(s)) {
            return s;
        }
    }
    return std::nullopt;
}

const char *hold_rule_id(HoldRule rule) { return rule == HoldRule::strict ? "strict" : "tolerant"; }

std::optional<HoldRule> hold_rule_from_id(const std::string &id) {
    if (id == "strict") {
        return HoldRule::strict;
    }
    if (id == "tolerant") {
        return HoldRule::tolerant;
    }
    return std::nullopt;
}

std::optional<SceneLaunch> scene_launch_from_id(const std::string &id) {
    for (const SceneLaunch l : kSceneLaunches) {
        if (id == scene_launch_id(l)) {
            return l;
        }
    }
    return std::nullopt;
}

int SceneLauncher::update(int requested, const EngineInputs &inputs, SceneLaunch launch) {
    if (!started_) {
        started_ = true;
        active_ = requested;
        waiting_ = false;
        t_s_ = inputs.t_s;
        return active_;
    }
    const bool moving = inputs.t_s != t_s_;
    t_s_ = inputs.t_s;
    if (requested == active_) {
        waiting_ = false;
        return active_;
    }
    if (!waiting_ || target_ != requested) {
        waiting_ = true;
        target_ = requested;
        bar_ = inputs.bar_index;
        phrase_ = inputs.phrase_index;
    }
    const bool go = launch == SceneLaunch::now || !moving ||
                    (launch == SceneLaunch::bar && inputs.bar_index != bar_) ||
                    (launch == SceneLaunch::phrase && inputs.phrase_index != phrase_);
    if (go) {
        active_ = requested;
        waiting_ = false;
    }
    return active_;
}

Frame render_mix(const std::vector<Scene> &scenes, const SceneMix &mix, const Rig &rig,
                 const EngineInputs &inputs, const std::vector<Palette> &palettes,
                 std::uint64_t seed, ModMemory *memory, const SceneVariants *variants,
                 PostMemory *post) {
    if (scenes.empty() || palettes.empty() || mix.to < 0 ||
        mix.to >= static_cast<int>(scenes.size())) {
        return Frame(rig.pixels.size());
    }
    if (memory != nullptr) {
        memory->resize(scenes.size());
    }
    if (post != nullptr) {
        post->resize(scenes.size());
    }
    // advance: step the post-effects' memory (once a frame per scene: not for the palette slew's
    // second drawing).
    const auto render = [&](int scene, int variant, int shift, bool advance) {
        const auto i = static_cast<size_t>(scene);
        const auto v = static_cast<size_t>(variant);
        // A variation shares its scene's route memory: the same routes, the same render time.
        const Scene &s =
            variants != nullptr && i < variants->size() && v > 0 && v < (*variants)[i].size()
                ? (*variants)[i][v]
                : scenes[i];
        // A scene rendered twice in a frame (a palette slew) steps its routes at the same render
        // time twice, which returns the same values.
        Frame out = render_scene(
            s, rig, inputs,
            palettes[static_cast<size_t>(
                scene_palette(s, shift, palettes.size(), mix.palette_rotation))],
            seed, memory != nullptr ? &(*memory)[static_cast<size_t>(scene)] : nullptr);
        if (post != nullptr) {
            apply_post(s, rig, inputs, out, &(*post)[i], advance);
        }
        return out;
    };
    // The scene crossfade at one palette shift.
    const auto render_scenes = [&](int shift, bool advance) {
        Frame incoming = render(mix.to, mix.variant, shift, advance);
        if (mix.from < 0 || mix.to_weight >= 1.0) {
            return incoming;
        }
        Frame frame = render(mix.from, mix.variant_from, shift, advance);
        mix_into(frame, incoming, mix.to_weight);
        return frame;
    };
    Frame frame = render_scenes(mix.palette_shift, true);
    if (mix.palette_weight < 1.0) {
        Frame old = render_scenes(mix.palette_shift_from, false);
        mix_into(old, frame, mix.palette_weight);
        frame = std::move(old);
    }
    if (mix.brightness < 1.0) {
        for (Rgb &c : frame) {
            c = scaled(c, std::max(0.0, mix.brightness));
        }
    }
    if (mix.fill != PhraseFill::off) {
        apply_fill(frame, mix, rig, inputs);
    }
    return frame;
}

} // namespace psyight
