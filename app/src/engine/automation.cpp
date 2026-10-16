#include "engine/automation.h"

#include <algorithm>

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

int palette_index(const Scene &scene, int shift, size_t palette_count) {
    const auto n = static_cast<int>(palette_count);
    return ((scene.palette + shift) % n + n) % n;
}

void mix_into(Frame &frame, const Frame &other, double other_weight) {
    const auto w = static_cast<float>(std::clamp(other_weight, 0.0, 1.0));
    for (size_t i = 0; i < frame.size() && i < other.size(); ++i) {
        frame[i] = blend(frame[i], other[i], BlendMode::alpha, w);
    }
}

} // namespace

int scene_for_section(const std::vector<Scene> &scenes,
                      const std::vector<DetectedSection> &sections, size_t index) {
    if (scenes.empty() || index >= sections.size()) {
        return 0;
    }
    const std::string &label = sections[index].label;
    std::vector<int> candidates = tagged(scenes, label);
    if (candidates.empty() && label == "drop") {
        candidates = tagged(scenes, "groove");
    }
    if (candidates.empty()) {
        return 0;
    }
    size_t occurrence = 0;
    for (size_t j = 0; j < index; ++j) {
        occurrence += sections[j].label == label;
    }
    return candidates[occurrence % candidates.size()];
}

SceneMix automate(const std::vector<Scene> &scenes, const TrackAnalysis &analysis, const Grid &grid,
                  const EngineInputs &inputs, const AutomationParams &params, int manual_scene) {
    SceneMix mix;
    const double beat_s = grid.bpm > 0.0 ? 60.0 / grid.bpm : 0.5;

    // Palette rotation by phrase.
    if (params.palette_every_phrases > 0 && inputs.phrase_index >= 0) {
        const int n = params.palette_every_phrases;
        mix.palette_shift = inputs.phrase_index / n;
        mix.palette_shift_from = mix.palette_shift;
        const auto phrase = static_cast<size_t>(inputs.phrase_index);
        const double since_s = inputs.t_s - analysis.phrase_starts_s[phrase];
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
    mix.to = scene_for_section(scenes, analysis.sections, index);
    if (index == 0 || cuts_into(inputs.section)) {
        return mix;
    }
    const int previous = scene_for_section(scenes, analysis.sections, index - 1);
    const double fade_s = std::min(params.crossfade_beats * beat_s, 0.5 * inputs.section_length_s);
    if (previous != mix.to && fade_s > 0.0 && inputs.since_section_s < fade_s) {
        mix.from = previous;
        mix.to_weight = inputs.since_section_s / fade_s;
    }
    return mix;
}

Frame render_mix(const std::vector<Scene> &scenes, const SceneMix &mix, const Rig &rig,
                 const EngineInputs &inputs, const std::vector<Palette> &palettes,
                 std::uint64_t seed) {
    if (scenes.empty() || palettes.empty() || mix.to < 0 ||
        mix.to >= static_cast<int>(scenes.size())) {
        return Frame(rig.pixels.size());
    }
    const auto render = [&](int scene, int shift) {
        const Scene &s = scenes[static_cast<size_t>(scene)];
        return render_scene(s, rig, inputs,
                            palettes[static_cast<size_t>(palette_index(s, shift, palettes.size()))],
                            seed);
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
