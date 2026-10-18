#include "engine/presets.h"

#include "engine/effect_spec.h"

#include <algorithm>

namespace psyight {

namespace {

ModRoute route(ModSource source, ModTarget target, int layer, double amount,
               ModCurve curve = ModCurve::linear, const char *param = "",
               ModQuantise quantise = ModQuantise::off, double attack_s = 0.0,
               double release_s = 0.0) {
    ModRoute r;
    r.source = source;
    r.curve = curve;
    r.target = target;
    r.layer = layer;
    r.amount = amount;
    r.param = param;
    r.quantise = quantise;
    r.attack_s = attack_s;
    r.release_s = release_s;
    return r;
}

Palette palette_named(const std::string &name) {
    for (const Palette &p : builtin_palettes()) {
        if (p.name == name) {
            return p;
        }
    }
    return default_palettes().front();
}

// The scenes of a pack from presets, their palettes gathered once each in order.
ShowPack pack_of(std::string name, std::string description, const std::vector<ScenePreset> &looks) {
    ShowPack pack;
    pack.name = std::move(name);
    pack.description = std::move(description);
    for (const ScenePreset &look : looks) {
        auto it = std::find_if(pack.palettes.begin(), pack.palettes.end(),
                               [&](const Palette &p) { return p.name == look.palette.name; });
        if (it == pack.palettes.end()) {
            pack.palettes.push_back(look.palette);
            it = pack.palettes.end() - 1;
        }
        Scene scene = look.scene;
        scene.palette = static_cast<int>(it - pack.palettes.begin());
        pack.scenes.push_back(std::move(scene));
    }
    return pack;
}

const ScenePreset &find(const std::vector<ScenePreset> &all, const std::string &name) {
    for (const ScenePreset &p : all) {
        if (p.name == name) {
            return p;
        }
    }
    return all.front();
}

} // namespace

std::vector<Palette> builtin_palettes() {
    std::vector<Palette> palettes = default_palettes();
    const std::vector<Palette> more = {
        {"Ice", {{0.0f, 0.6f, 1.0f}, {0.2f, 0.9f, 1.0f}, {0.3f, 0.2f, 1.0f}}},
        {"Ember", {{0.9f, 0.05f, 0.0f}, {1.0f, 0.35f, 0.0f}, {0.6f, 0.0f, 0.4f}}},
        {"Magenta storm", {{1.0f, 0.0f, 0.5f}, {0.7f, 0.0f, 1.0f}, {1.0f, 0.2f, 0.8f}}},
        {"Mono violet", {{0.3f, 0.0f, 0.9f}, {0.5f, 0.0f, 1.0f}, {0.2f, 0.0f, 0.6f}}},
        {"Toxic", {{0.4f, 1.0f, 0.0f}, {0.0f, 0.9f, 0.3f}, {0.0f, 0.6f, 0.6f}}},
        {"Sunset", {{1.0f, 0.4f, 0.0f}, {1.0f, 0.0f, 0.4f}, {0.5f, 0.0f, 0.8f}}},
        {"Aurora", {{0.0f, 1.0f, 0.5f}, {0.0f, 0.7f, 0.9f}, {0.6f, 0.0f, 1.0f}}},
        {"Ocean UV", {{0.0f, 0.3f, 1.0f}, {0.4f, 0.0f, 1.0f}, {0.0f, 0.7f, 0.8f}}},
    };
    palettes.insert(palettes.end(), more.begin(), more.end());
    return palettes;
}

std::vector<ScenePreset> builtin_scene_presets() {
    std::vector<ScenePreset> looks;

    { // The groove workhorse: ambience ducks on each kick.
        ScenePreset p{"Rolling conveyor",
                      "Bass conveyor, hat sparkle and kick rings; the "
                      "plasma ducks on each kick.",
                      {},
                      palette_named("UV violet")};
        p.scene.sections = {"groove"};
        p.scene.layers.push_back(
            make_layer({PlasmaParams{0.08, 1.4, 0.2, 0.45}}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add));
        p.scene.layers.push_back(make_layer({SparkleParams{}}, BlendMode::add, 0.8f));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen));
        p.scene.routes.push_back(route(ModSource::kick, ModTarget::opacity, 0, -0.6));
        looks.push_back(std::move(p));
    }
    { // Short and tight: the conveyor's lit length follows each note's velocity.
        ScenePreset p{"Staccato pulse",
                      "Short conveyor steps and tight kick rings, the "
                      "conveyor's brightness from bass velocity.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"groove"};
        p.scene.layers.push_back(
            make_layer({ConveyorParams{6, 4, 28.0, 300.0, 0.01}}, BlendMode::add, 0.6f));
        p.scene.layers.push_back(make_layer({ShockwaveParams{8.0, 0.15, 0.15}}, BlendMode::screen));
        p.scene.routes.push_back(route(ModSource::bass_velocity, ModTarget::opacity, 0, 0.4));
        looks.push_back(std::move(p));
    }
    { // Acid lines open the filter: brighter residual, hue swings.
        ScenePreset p{"Acid swirl",
                      "Fast plasma whose hue follows the acid's brightness, over "
                      "the conveyor; the drift speeds up through each bar.",
                      {},
                      palette_named("Acid")};
        p.scene.sections = {"groove", "drop"};
        p.scene.layers.push_back(make_layer({PlasmaParams{0.3, 0.8, 0.3, 0.5}}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen));
        p.scene.routes.push_back(route(ModSource::residual_brightness, ModTarget::hue_shift, 0,
                                       90.0, ModCurve::linear, "", ModQuantise::off, 0.05, 0.3));
        p.scene.routes.push_back(route(ModSource::bar_phase, ModTarget::setting, 0, 0.2,
                                       ModCurve::exponential, "speed_hz"));
        looks.push_back(std::move(p));
    }
    { // Breakdowns: slow colour, shimmer breathing with the music.
        ScenePreset p{"Floating",
                      "Slow plasma drifting through the palette over each phrase; "
                      "shimmer breathing with the residual.",
                      {},
                      palette_named("Deep sea")};
        p.scene.sections = {"intro", "breakdown", "outro"};
        p.scene.layers.push_back(
            make_layer({PlasmaParams{0.04, 2.0, 0.15, 0.6}}, BlendMode::alpha));
        p.scene.layers.push_back(
            make_layer({ShimmerParams{0.8, 2.0, 0.15, 0.05}}, BlendMode::add, 0.6f));
        p.scene.routes.push_back(
            route(ModSource::lfo_phrase, ModTarget::hue_shift, 0, 60.0, ModCurve::s_curve));
        p.scene.routes.push_back(route(ModSource::residual_level, ModTarget::opacity, 1, 0.4,
                                       ModCurve::linear, "", ModQuantise::off, 0.2, 0.8));
        looks.push_back(std::move(p));
    }
    { // Builds: rings speed up, sparkle thickens towards the drop.
        ScenePreset p{"Tension rise",
                      "Contracting rings that speed up through the build, "
                      "sparkle thickening towards the drop.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        p.scene.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::alpha, 0.4f));
        p.scene.layers.push_back(
            make_layer({BuildParams{0.4, 0.6, 4.0, 0.1, 0.8}}, BlendMode::add));
        p.scene.layers.push_back(make_layer({SparkleParams{0.04, 0.05}}, BlendMode::add, 0.2f));
        p.scene.routes.push_back(
            route(ModSource::section_progress, ModTarget::opacity, 2, 0.8, ModCurve::exponential));
        p.scene.routes.push_back(route(ModSource::section_progress, ModTarget::setting, 2, 0.4,
                                       ModCurve::exponential, "density"));
        looks.push_back(std::move(p));
    }
    { // Drops: the burst, then the groove at full energy.
        ScenePreset p{"Drop impact",
                      "The drop burst over conveyor and kick rings; colours jump "
                      "each bar.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"drop"};
        p.scene.layers.push_back(make_layer({PlasmaParams{0.1, 1.2, 0.2, 0.4}}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen));
        p.scene.layers.push_back(make_layer({DropBurstParams{}}, BlendMode::screen));
        p.scene.routes.push_back(route(ModSource::random_bar, ModTarget::hue_shift, 1, 120.0,
                                       ModCurve::linear, "", ModQuantise::bar));
        looks.push_back(std::move(p));
    }
    { // Dark rooms: little light, bar-to-bar colour steps.
        ScenePreset p{"Minimal dark",
                      "Only the bass conveyor and faint kick rings; its colour "
                      "steps once per bar.",
                      {},
                      palette_named("Mono violet")};
        p.scene.sections = {"groove"};
        p.scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add, 0.6f));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen, 0.4f));
        p.scene.routes.push_back(route(ModSource::random_bar, ModTarget::setting, 0, 0.25,
                                       ModCurve::linear, "hue_offset_deg", ModQuantise::bar));
        looks.push_back(std::move(p));
    }
    { // Live performance: macros 1 to 6 play the scene.
        ScenePreset p{"Macro performer",
                      "Played live with the macros: 1 ambience, 2 hue spin, 3 "
                      "conveyor density, 4 shimmer, 5 drift, 6 fade out.",
                      {},
                      palette_named("Aurora")};
        p.scene.sections = {"groove", "breakdown"};
        p.scene.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::alpha, 0.0f));
        p.scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add));
        p.scene.layers.push_back(make_layer({ShimmerParams{}}, BlendMode::add, 0.0f));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen));
        p.scene.routes.push_back(route(ModSource::macro_1, ModTarget::opacity, 0, 1.0));
        p.scene.routes.push_back(route(ModSource::macro_2, ModTarget::hue_shift, 1, 180.0,
                                       ModCurve::linear, "", ModQuantise::off, 0.1, 0.1));
        p.scene.routes.push_back(route(ModSource::macro_3, ModTarget::setting, 1, -0.3,
                                       ModCurve::linear, "period_segments"));
        p.scene.routes.push_back(route(ModSource::macro_4, ModTarget::opacity, 2, 1.0));
        p.scene.routes.push_back(route(ModSource::macro_5, ModTarget::setting, 0, 0.4,
                                       ModCurve::exponential, "speed_hz"));
        // Inverted, so the macro's default (0) leaves the scene at full brightness.
        ModRoute fade = route(ModSource::macro_6, ModTarget::brightness, 0, 1.0, ModCurve::linear,
                              "", ModQuantise::off, 0.05, 0.05);
        fade.invert = true;
        p.scene.routes.push_back(fade);
        looks.push_back(std::move(p));
    }
    for (ScenePreset &p : looks) {
        p.scene.name = p.name;
        p.scene.palette = 0;
    }
    return looks;
}

std::vector<ShowPack> builtin_show_packs() {
    const std::vector<ScenePreset> looks = builtin_scene_presets();
    std::vector<ShowPack> packs;

    ShowPack original;
    original.name = "Psyight default";
    original.description = "The scenes and palettes a new project starts with.";
    original.scenes = default_scenes();
    original.palettes = default_palettes();
    packs.push_back(std::move(original));

    ShowPack dark =
        pack_of("Dark minimal",
                "Little light for dark rooms: violet, deep sea and ember; the palette "
                "moves every 4 phrases.",
                {find(looks, "Minimal dark"), find(looks, "Staccato pulse"),
                 find(looks, "Floating"), find(looks, "Tension rise"), find(looks, "Drop impact")});
    dark.automation.palette_every_phrases = 4;
    packs.push_back(std::move(dark));

    ShowPack colour =
        pack_of("Full colour",
                "Bright and moving: acid, UV and magenta, a new palette every 2 "
                "phrases, long crossfades.",
                {find(looks, "Acid swirl"), find(looks, "Rolling conveyor"),
                 find(looks, "Floating"), find(looks, "Tension rise"), find(looks, "Drop impact")});
    colour.automation.crossfade_beats = 8.0;
    colour.automation.palette_every_phrases = 2;
    packs.push_back(std::move(colour));

    ShowPack live = pack_of(
        "Live macros",
        "For a controller: the Macro performer scene in grooves and "
        "breakdowns, with named macros.",
        {find(looks, "Macro performer"), find(looks, "Tension rise"), find(looks, "Drop impact")});
    live.automation.palette_every_phrases = 0; // The performer picks colours by hand.
    live.macro_names = {"Ambience", "Hue spin", "Density", "Shimmer", "Drift", "Fade out", "", ""};
    packs.push_back(std::move(live));
    return packs;
}

} // namespace psyight
