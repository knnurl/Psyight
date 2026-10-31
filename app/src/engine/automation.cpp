#include "engine/automation.h"

#include <algorithm>
#include <cmath>

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

// Too short to change the scene for.
bool held(const DetectedSection &section, double min_dwell_s) {
    return section.label != "build" && section.label != "drop" &&
           section.end_s - section.start_s < min_dwell_s;
}

int palette_index(const Scene &scene, int shift, size_t palette_count) {
    const auto n = static_cast<int>(palette_count);
    return ((scene.palette + shift) % n + n) % n;
}

// The section whose scene section `index` shows: itself, or the one before a held blip.
size_t own_section(const std::vector<DetectedSection> &sections, size_t index, double min_dwell_s) {
    while (index > 0 && held(sections[index], min_dwell_s)) {
        --index;
    }
    return index;
}

// Earlier sections with the same label (blips aside): the rotation between sections.
size_t occurrence_of(const std::vector<DetectedSection> &sections, size_t index,
                     double min_dwell_s) {
    size_t occurrence = 0;
    for (size_t j = 0; j < index; ++j) {
        occurrence += sections[j].label == sections[index].label && !held(sections[j], min_dwell_s);
    }
    return occurrence;
}

constexpr double kPhraseToleranceS = 0.05;

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

// A rotation that began `since_s` ago (at a phrase start) crossfades from the scene before it.
void fade_rotation(SceneMix &mix, int before, double since_s, double beat_s,
                   const AutomationParams &params) {
    const double fade_s = params.rotate_fade_beats * beat_s;
    if (before != mix.to && fade_s > 0.0 && since_s >= 0.0 && since_s < fade_s) {
        mix.from = before;
        mix.to_weight = since_s / fade_s;
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
                      double min_dwell_s) {
    if (scenes.empty() || index >= sections.size()) {
        return 0;
    }
    index = own_section(sections, index, min_dwell_s);
    return scene_for_label(scenes, sections[index].label,
                           occurrence_of(sections, index, min_dwell_s), sections[index].energy);
}

SceneMix automate(const std::vector<Scene> &scenes, const TrackAnalysis &analysis, const Grid &grid,
                  const EngineInputs &inputs, const AutomationParams &params, int manual_scene,
                  const SectionTimeline *timeline) {
    SceneMix mix;
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
    // The scene of section i at time t_s: its own (or the one before a blip), rotated by the
    // phrases it has run.
    const auto scene_at = [&](size_t i, double t_s, size_t *step_out) {
        const size_t own = own_section(sections, i, phrase_s);
        const DetectedSection &s = sections[own];
        const size_t step =
            rotation_step(params, s.label, phrases_run(phrase_starts, s.start_s, t_s));
        if (step_out != nullptr) {
            *step_out = step;
        }
        return scene_for_label(scenes, s.label, occurrence_of(sections, own, phrase_s), s.energy,
                               step);
    };
    size_t step = 0;
    mix.to = scene_at(index, inputs.t_s, &step);
    if (step > 0 && inputs.phrase_index >= 0 &&
        static_cast<size_t>(inputs.phrase_index) < phrase_starts.size()) {
        const DetectedSection &own = sections[own_section(sections, index, phrase_s)];
        const double phrase_start_s = phrase_starts[static_cast<size_t>(inputs.phrase_index)];
        if (rotation_step(params, own.label,
                          phrases_run(phrase_starts, own.start_s, phrase_start_s - 1e-3)) !=
            step) { // This phrase began the rotation.
            fade_rotation(
                mix,
                scene_for_label(
                    scenes, own.label,
                    occurrence_of(sections, own_section(sections, index, phrase_s), phrase_s),
                    own.energy, step - 1),
                inputs.t_s - phrase_start_s, beat_s, params);
            return mix;
        }
    }
    if (index == 0 || cuts_into(inputs.section)) {
        return mix;
    }
    // The scene the section before ended on.
    const int previous = scene_at(index - 1, sections[index].start_s - 1e-3, nullptr);
    const double fade_s = std::min(params.crossfade_beats * beat_s, 0.5 * inputs.section_length_s);
    if (previous != mix.to && fade_s > 0.0 && inputs.since_section_s < fade_s) {
        mix.from = previous;
        mix.to_weight = inputs.since_section_s / fade_s;
    }
    return mix;
}

SceneMix automate_live(const std::vector<Scene> &scenes, const LiveState &state,
                       const EngineInputs &inputs, const AutomationParams &params,
                       int manual_scene) {
    SceneMix mix;
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
    const size_t step = rotation_step(params, inputs.section, phrases);
    mix.to = scene_for_label(scenes, inputs.section, occurrence(state.section),
                             state.section_energy, step);
    if (step > 0 && rotation_step(params, inputs.section, phrases - 1) != step) {
        fade_rotation(mix,
                      scene_for_label(scenes, inputs.section, occurrence(state.section),
                                      state.section_energy, step - 1),
                      into_s - static_cast<double>(phrases) * phrase_s, beat_s, params);
        return mix;
    }
    if (!state.has_previous || cuts_into(inputs.section)) {
        return mix;
    }
    const std::string previous_label = live_section_name(state.previous_section);
    const auto previous_phrases = static_cast<long long>(
        std::floor(std::max(0.0, state.previous_length_s / phrase_s - 1e-6)));
    const int previous = scene_for_label(
        scenes, previous_label, static_cast<size_t>(state.previous_occurrence),
        state.previous_energy, rotation_step(params, previous_label, previous_phrases));
    const double fade_s = std::min(params.crossfade_beats * beat_s, 0.5 * inputs.section_length_s);
    if (previous != mix.to && fade_s > 0.0 && inputs.since_section_s < fade_s) {
        mix.from = previous;
        mix.to_weight = inputs.since_section_s / fade_s;
    }
    return mix;
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
                 std::uint64_t seed, ModMemory *memory) {
    if (scenes.empty() || palettes.empty() || mix.to < 0 ||
        mix.to >= static_cast<int>(scenes.size())) {
        return Frame(rig.pixels.size());
    }
    if (memory != nullptr) {
        memory->resize(scenes.size());
    }
    const auto render = [&](int scene, int shift) {
        const Scene &s = scenes[static_cast<size_t>(scene)];
        // A scene rendered twice in a frame (a palette slew) steps its routes at the same render
        // time twice, which returns the same values.
        return render_scene(
            s, rig, inputs, palettes[static_cast<size_t>(palette_index(s, shift, palettes.size()))],
            seed, memory != nullptr ? &(*memory)[static_cast<size_t>(scene)] : nullptr);
    };
    // The scene crossfade at one palette shift.
    const auto render_scenes = [&](int shift) {
        Frame incoming = render(mix.to, shift);
        if (mix.from < 0 || mix.to_weight >= 1.0) {
            return incoming;
        }
        Frame frame = render(mix.from, shift);
        mix_into(frame, incoming, mix.to_weight);
        return frame;
    };
    Frame frame = render_scenes(mix.palette_shift);
    if (mix.palette_weight >= 1.0) {
        return frame;
    }
    Frame old = render_scenes(mix.palette_shift_from);
    mix_into(old, frame, mix.palette_weight);
    return old;
}

} // namespace psyight
