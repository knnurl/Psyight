#include "engine/presets.h"

#include "engine/effect_spec.h"
#include "engine/space.h"

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

// A layer from an effect id and settings by id (the Hexagon 3 packs, generated from a spec).
Layer layer_by_id(const char *effect, std::initializer_list<std::pair<const char *, double>> params,
                  const char *blend, float opacity, const char *group) {
    EffectSpec spec = default_spec(effect_type_from_id(effect).value());
    const std::vector<EffectParamInfo> infos = effect_param_infos(spec.type());
    for (const auto &[id, value] : params) {
        for (size_t i = 0; i < infos.size(); ++i) {
            if (std::string(infos[i].id) == id) {
                set_effect_param(spec, i, value);
            }
        }
    }
    Layer l = make_layer(spec, blend_mode_from_id(blend).value(), opacity);
    l.group = group;
    return l;
}

ModRoute route_by_id(const char *source, const char *target, int layer, double amount,
                     const char *param, bool invert) {
    ModRoute r = route(mod_source_from_id(source).value(), mod_target_from_id(target).value(),
                       layer, amount, ModCurve::linear, param);
    r.invert = invert;
    return r;
}

// A pack whose scenes each rotate through their own palettes (named, added to the pack as
// needed), palette shift every two phrases.
ShowPack rotating_pack(std::string name, std::string description,
                       const std::vector<ScenePreset> &chosen,
                       const std::vector<std::pair<std::string, std::vector<std::string>>> &lists) {
    ShowPack pack = pack_of(std::move(name), std::move(description), chosen);
    pack.automation.palette_every_phrases = 2;
    pack.automation.palette_rotation = PaletteRotation::scene_list;
    for (Scene &s : pack.scenes) {
        for (const auto &[scene, palettes] : lists) {
            if (s.name != scene) {
                continue;
            }
            for (const std::string &pal : palettes) {
                const auto it = std::find_if(pack.palettes.begin(), pack.palettes.end(),
                                             [&](const Palette &p) { return p.name == pal; });
                if (it == pack.palettes.end()) {
                    pack.palettes.push_back(palette_named(pal));
                    s.palette_list.push_back(static_cast<int>(pack.palettes.size()) - 1);
                } else {
                    s.palette_list.push_back(static_cast<int>(it - pack.palettes.begin()));
                }
            }
        }
    }
    return pack;
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
        // BEGIN Cagan palettes (generated from cagan_packs.py)
        {"Amber", {{1.0f, 0.45f, 0.0f}, {1.0f, 0.2f, 0.0f}, {0.9f, 0.6f, 0.1f}}},
        {"Ember tide", {{0.9f, 0.1f, 0.0f}, {1.0f, 0.4f, 0.0f}, {0.0f, 0.45f, 0.8f}}},
        {"Moss", {{0.0f, 0.5f, 0.1f}, {0.0f, 0.6f, 0.4f}, {0.3f, 0.0f, 0.6f}}},
        {"Fern glow", {{0.1f, 0.8f, 0.2f}, {0.0f, 0.5f, 0.5f}, {1.0f, 0.3f, 0.0f}}},
        {"Abyss", {{0.0f, 0.8f, 1.0f}, {1.0f, 0.0f, 0.6f}, {0.2f, 0.0f, 1.0f}}},
        // END Cagan palettes
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
                      "sparkle thickening as the riser rises.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        p.scene.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::alpha, 0.4f));
        p.scene.layers.push_back(
            make_layer({BuildParams{0.4, 0.6, 4.0, 0.1, 0.8}}, BlendMode::add));
        p.scene.layers.push_back(make_layer({SparkleParams{0.04, 0.05}}, BlendMode::add, 0.2f));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::opacity, 2, 0.8, ModCurve::exponential));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::setting, 2, 0.4, ModCurve::exponential, "density"));
        p.scene.routes.push_back(route(ModSource::riser, ModTarget::brightness, 0, 0.5));
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
    // The pattern effects (engine/pattern_effects.cpp), with energy tags for the show packs below.
    { // Grooves, mid energy: a scanner sweeping the rig each two bars over the conveyor.
        ScenePreset p{"Scanner",
                      "A comet sweeping across the rig and back every two bars, over the bass "
                      "conveyor and kick rings.",
                      {},
                      palette_named("UV violet")};
        p.scene.sections = {"groove", "mid"};
        ChaseParams scan;
        scan.mapping = 1; // Across x.
        scan.clock = 3;   // Bar.
        scan.rate = 0.5;
        scan.count = 1;
        scan.tail = 0.35;
        scan.bounce = 1;
        p.scene.layers.push_back(make_layer({PlasmaParams{0.05, 1.6, 0.1, 0.3}}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({scan}, BlendMode::add, 0.8f));
        p.scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add, 0.7f));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen));
        looks.push_back(std::move(p));
    }
    { // Grooves, high energy: comets stepping with every bass note, sparks on kicks.
        ScenePreset p{"Comet rain",
                      "Four comets stepping along every strip with each bass note, kick bursts "
                      "breaking into sparks, gated on the 16ths.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"groove", "drop", "high"};
        ChaseParams comets;
        comets.clock = 5; // Bass note.
        comets.rate = 1.0 / 16.0;
        comets.count = 4;
        comets.tail = 0.12;
        BurstsParams sparks;
        sparks.sparks = 0.5;
        sparks.keep = 2;
        StutterParams gate;
        gate.duty = 0.6;
        gate.depth = 0.5;
        p.scene.layers.push_back(make_layer({comets}, BlendMode::add));
        p.scene.layers.push_back(make_layer({sparks}, BlendMode::screen, 0.8f));
        p.scene.layers.push_back(make_layer({gate}, BlendMode::multiply));
        p.scene.routes.push_back(route(ModSource::random_bar, ModTarget::hue_shift, 0, 90.0,
                                       ModCurve::linear, "", ModQuantise::bar));
        looks.push_back(std::move(p));
    }
    { // Grooves, high energy: a ring from the centre on every kick, the rest ducking.
        ScenePreset p{"Kick pulse",
                      "A ring rolling out from the centre on every beat; the plasma ducks "
                      "after each kick like a sidechain.",
                      {},
                      palette_named("Ice")};
        p.scene.sections = {"groove", "high"};
        WaveParams rings;
        rings.mapping = 3; // Radial.
        rings.clock = 2;   // Beat: one ring out from the centre per beat (kick).
        rings.rate = 1.0;
        rings.shape = 4; // Bump.
        rings.width = 0.25;
        rings.level = 1.0;
        StutterParams duck;
        duck.mode = 2;
        duck.duty = 0.6;
        duck.depth = 0.8;
        p.scene.layers.push_back(make_layer({PlasmaParams{0.1, 1.0, 0.35, 0.5}}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({duck}, BlendMode::multiply));
        p.scene.layers.push_back(make_layer({rings}, BlendMode::add));
        p.scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add, 0.5f));
        looks.push_back(std::move(p));
    }
    { // Grooves: the bassline drawn as a pitch meter across the rig.
        ScenePreset p{"Pitch meter",
                      "The bassline as a bar across the rig, its length the note's pitch, over "
                      "a slow wave; kick rings on top.",
                      {},
                      palette_named("Aurora")};
        p.scene.sections = {"groove"};
        BassBarsParams meter;
        meter.mode = 1; // Bar.
        meter.colours = 1;
        WaveParams swell;
        swell.mapping = 2; // Across y.
        swell.clock = 4;   // Phrase.
        swell.level = 0.25;
        p.scene.layers.push_back(make_layer({swell}, BlendMode::add));
        p.scene.layers.push_back(make_layer({meter}, BlendMode::add));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen, 0.6f));
        looks.push_back(std::move(p));
    }
    { // Grooves, high energy: an acid line chopped by the bassline's own gate.
        ScenePreset p{"Acid gate",
                      "Ramps racing along the strips on the 16ths, opened and closed by the "
                      "bassline's gate; hue follows the acid's brightness.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove", "high"};
        WaveParams ramps;
        ramps.mapping = 0;
        ramps.wings = 2;
        ramps.clock = 1; // 16th.
        ramps.rate = 0.25;
        ramps.shape = 2; // Ramp.
        ramps.spread = 3.0;
        ramps.level = 0.9;
        StutterParams gate;
        gate.mode = 1;
        gate.release_s = 0.03;
        p.scene.layers.push_back(make_layer({ramps}, BlendMode::add));
        p.scene.layers.push_back(make_layer({gate}, BlendMode::multiply));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen, 0.7f));
        p.scene.routes.push_back(route(ModSource::residual_brightness, ModTarget::hue_shift, 0,
                                       90.0, ModCurve::linear, "", ModQuantise::off, 0.05, 0.3));
        looks.push_back(std::move(p));
    }
    { // Grooves, low energy: dim noise with the conveyor, for dark rooms.
        ScenePreset p{"Forest floor",
                      "Dim drifting noise in greens and teals with the bass conveyor and faint "
                      "kick rings.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove", "low", "mid"};
        NoiseParams moss;
        moss.scale_m = 0.5;
        moss.speed_hz = 0.15;
        moss.level = 0.3;
        p.scene.layers.push_back(make_layer({moss}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({ConveyorParams{}}, BlendMode::add, 0.6f));
        p.scene.layers.push_back(make_layer({ShockwaveParams{}}, BlendMode::screen, 0.35f));
        looks.push_back(std::move(p));
    }
    { // Breakdowns: a slow nebula whose colour follows the pads.
        ScenePreset p{"Nebula",
                      "A slow noise nebula shifting colour with the pads' brightness, shimmer in "
                      "the rests, hats as small fireworks.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"breakdown", "low"};
        NoiseParams nebula;
        nebula.scale_m = 1.2;
        nebula.speed_hz = 0.06;
        nebula.contrast = 2.0;
        nebula.level = 0.55;
        nebula.brightness_colour = 0.6;
        BurstsParams fireworks;
        fireworks.trigger = 1; // Hats.
        fireworks.sparks = 0.7;
        fireworks.width_m = 0.1;
        fireworks.decay_s = 0.2;
        fireworks.level = 0.5;
        p.scene.layers.push_back(make_layer({nebula}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({ShimmerParams{}}, BlendMode::add, 0.5f));
        p.scene.layers.push_back(make_layer({fireworks}, BlendMode::screen));
        p.scene.routes.push_back(
            route(ModSource::lfo_phrase, ModTarget::hue_shift, 0, 45.0, ModCurve::s_curve));
        looks.push_back(std::move(p));
    }
    { // Intros and outros: almost dark, one slow arm turning.
        ScenePreset p{"Deep space",
                      "Almost dark: one slow spiral arm turning once a phrase over faint noise.",
                      {},
                      palette_named("Mono violet")};
        p.scene.sections = {"intro", "outro"};
        SpiralParams arm;
        arm.arms = 1;
        arm.clock = 4; // Phrase.
        arm.rate = 1.0;
        arm.width = 0.25;
        arm.level = 0.5;
        NoiseParams dust;
        dust.level = 0.15;
        dust.speed_hz = 0.05;
        p.scene.layers.push_back(make_layer({dust}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({arm}, BlendMode::add));
        looks.push_back(std::move(p));
    }
    { // Builds: fire climbing with the riser, dark for the last beat.
        ScenePreset p{"Fire riser",
                      "Flames climbing with the riser and a spiral spinning up through the "
                      "build; dark for the last beat, so the drop lands out of nothing.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        FireParams flames;
        flames.height = 0.35;
        flames.riser_boost = 1.8;
        SpiralParams spin;
        spin.arms = 2;
        spin.rate = 0.25;
        spin.level = 0.5;
        StutterParams gap;
        gap.mode = 3; // Pre-drop gap.
        p.scene.layers.push_back(make_layer({flames}, BlendMode::add));
        p.scene.layers.push_back(make_layer({spin}, BlendMode::add, 0.3f));
        p.scene.layers.push_back(make_layer({gap}, BlendMode::multiply));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::setting, 1, 0.2, ModCurve::exponential, "rate"));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::opacity, 1, 0.6, ModCurve::exponential));
        looks.push_back(std::move(p));
    }
    { // Builds: a 16th strobe roll into the drop over a tightening spiral.
        ScenePreset p{"Strobe roll",
                      "A spiral tightening with the riser, then a 16th strobe roll in the "
                      "palette's colours over the build's last beat.",
                      {},
                      palette_named("Sunset")};
        p.scene.sections = {"build"};
        SpiralParams spin;
        spin.arms = 4;
        spin.twist = 2.0;
        spin.level = 0.6;
        StrobeParams roll;
        roll.trigger = 1; // Build end.
        roll.flashes = 4;
        roll.colour = 1; // Palette: white stays for the drop.
        roll.level = 0.8;
        p.scene.layers.push_back(make_layer({spin}, BlendMode::add));
        p.scene.layers.push_back(make_layer({roll}, BlendMode::screen));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::setting, 0, 0.3, ModCurve::exponential, "rate"));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::setting, 0, 0.2, ModCurve::linear, "twist"));
        looks.push_back(std::move(p));
    }
    { // Drops: white flashes, the burst, fireworks on every kick.
        ScenePreset p{"Drop bloom",
                      "Two white flashes and the burst as the drop lands, then fireworks on "
                      "every kick over 16th waves.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"drop"};
        WaveParams waves;
        waves.mapping = 3; // Radial.
        waves.clock = 1;   // 16th.
        waves.rate = 0.25;
        waves.shape = 3; // Square.
        waves.spread = 2.0;
        waves.level = 0.4;
        BurstsParams fireworks;
        fireworks.keep = 3;
        fireworks.sparks = 0.4;
        p.scene.layers.push_back(make_layer({waves}, BlendMode::add));
        p.scene.layers.push_back(make_layer({fireworks}, BlendMode::screen));
        p.scene.layers.push_back(make_layer({DropBurstParams{}}, BlendMode::screen));
        p.scene.layers.push_back(make_layer({StrobeParams{}}, BlendMode::screen));
        looks.push_back(std::move(p));
    }

    // The hexagon pack: looks for hexagonal rigs (the hexagon mandala, the Hive stage's hexagon),
    // built on hex distance, so each ring lights at once.
    { // Grooves: a hexagon out of the centre on every kick, the rolling bass round the slices.
        ScenePreset p{"Hex tunnel",
                      "A hexagon flies out of the centre on every kick, rings breathing inwards "
                      "each beat, the rolling bass chasing round the six slices.",
                      {},
                      palette_named("UV violet")};
        p.scene.sections = {"groove"};
        WaveParams rings;
        rings.mapping = 6; // Hex rings.
        rings.clock = 2;   // Beat.
        rings.rate = -1.0; // Inwards, a ring a beat.
        rings.shape = 4;   // Bump.
        rings.spread = 1.5;
        rings.width = 0.25;
        rings.level = 0.3;
        SectorsParams roll;
        roll.clock = 5; // Bass note.
        roll.tail = 3.0;
        roll.level = 0.5;
        p.scene.layers.push_back(make_layer({rings}, BlendMode::add));
        p.scene.layers.push_back(make_layer({roll}, BlendMode::add, 0.7f));
        p.scene.layers.push_back(make_layer({HexPulseParams{}}, BlendMode::screen));
        looks.push_back(std::move(p));
    }
    { // Grooves: a radar sweep stepping with the bassline, a ring closing in each bar.
        ScenePreset p{"Hex radar",
                      "A slice sweeping round one step per bass note with a long tail, a hexagon "
                      "closing in from the edge each bar, ducking on the kicks.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove"};
        SectorsParams sweep;
        sweep.count = 12;
        sweep.clock = 5; // Bass note.
        sweep.tail = 6.0;
        sweep.colour_spread = 0.3;
        HexPulseParams close;
        close.trigger = 2;   // Bar.
        close.direction = 1; // In.
        close.travel_beats = 4.0;
        close.width = 0.1;
        close.decay_s = 2.0;
        close.keep = 1;
        close.level = 0.7;
        StutterParams duck;
        duck.mode = 2; // Kick duck.
        duck.depth = 0.6;
        p.scene.layers.push_back(make_layer({sweep}, BlendMode::add));
        p.scene.layers.push_back(make_layer({close}, BlendMode::screen));
        p.scene.layers.push_back(make_layer({duck}, BlendMode::multiply));
        looks.push_back(std::move(p));
    }
    { // Builds: hexagons collapsing to the centre, faster with the riser; dark before the drop.
        ScenePreset p{"Hex implosion",
                      "Hexagons collapsing into the centre on every beat while the slices fill "
                      "round, brighter with the riser; dark for the last beat.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        HexPulseParams collapse;
        collapse.trigger = 1;   // Beat.
        collapse.direction = 1; // In.
        collapse.travel_beats = 1.0;
        collapse.colours = 1; // By ring.
        SectorsParams fill;
        fill.mode = 2;  // Fill.
        fill.clock = 2; // Beat.
        fill.level = 0.4;
        StutterParams gap;
        gap.mode = 3; // Pre-drop gap.
        p.scene.layers.push_back(make_layer({fill}, BlendMode::add, 0.5f));
        p.scene.layers.push_back(make_layer({collapse}, BlendMode::screen));
        p.scene.layers.push_back(make_layer({gap}, BlendMode::multiply));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::opacity, 0, 0.5, ModCurve::exponential));
        p.scene.routes.push_back(route(ModSource::riser, ModTarget::brightness, 0, 0.4));
        looks.push_back(std::move(p));
    }
    { // Builds: Tension rise's closing rings and growing light, the Build scene's hats and kicks,
      // in hexagons. Pressure: everything falls inwards, faster, while each kick pushes back out.
        ScenePreset p{"Hex pressure",
                      "Rings closing on the centre ever faster and hexagons collapsing into it, "
                      "each kick pushing a hexagon back out, hats sparking; brighter and denser as "
                      "the riser rises, then dark for the last beat.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        HexPulseParams collapse;
        collapse.trigger = 1;   // Beat.
        collapse.direction = 1; // In.
        collapse.travel_beats = 1.0;
        collapse.colours = 1; // By ring.
        HexPulseParams push;
        push.travel_beats = 0.5; // Out on each kick, against the fall.
        push.width = 0.08;
        push.decay_s = 0.25;
        StutterParams gap;
        gap.mode = 3; // Pre-drop gap.
        p.scene.layers.push_back(make_layer({PlasmaParams{}}, BlendMode::alpha, 0.4f));
        p.scene.layers.push_back(
            make_layer({BuildParams{0.4, 0.6, 4.0, 0.1, 0.8}}, BlendMode::add, 0.6f));
        p.scene.layers.push_back(make_layer({collapse}, BlendMode::screen, 0.3f));
        p.scene.layers.push_back(make_layer({push}, BlendMode::screen));
        p.scene.layers.push_back(make_layer({SparkleParams{}}, BlendMode::add, 0.4f));
        p.scene.layers.push_back(make_layer({gap}, BlendMode::multiply));
        p.scene.routes.push_back(route(ModSource::riser, ModTarget::brightness, 0, 0.5));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::opacity, 2, 0.7, ModCurve::exponential));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::opacity, 4, 0.6, ModCurve::exponential));
        p.scene.routes.push_back(
            route(ModSource::riser, ModTarget::setting, 4, 0.4, ModCurve::exponential, "density"));
        looks.push_back(std::move(p));
    }
    { // Intros without a kick: the bassline as hexagons, each hat (and its echoes) as sparks.
        ScenePreset p{"Hex echoes",
                      "For kickless intros: a dim nebula; each bass note sends a hexagon out "
                      "from the centre, each hat breaks into sparks that fade like an echo, "
                      "repeats overlapping; the nebula lifts on the hats.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"intro"};
        NoiseParams nebula;
        nebula.scale_m = 1.0;
        nebula.speed_hz = 0.05;
        nebula.level = 0.25;
        HexPulseParams bass;
        bass.trigger = 3; // Bass note.
        bass.travel_beats = 1.0;
        bass.width = 0.08;
        bass.decay_s = 0.6;
        bass.keep = 4;
        bass.level = 0.6;
        BurstsParams echoes;
        echoes.trigger = 1; // Hats.
        echoes.keep = 4;
        echoes.sparks = 0.7;
        echoes.width_m = 0.12;
        echoes.decay_s = 0.35;
        echoes.level = 0.7;
        p.scene.layers.push_back(make_layer({nebula}, BlendMode::alpha));
        p.scene.layers.push_back(make_layer({bass}, BlendMode::screen));
        p.scene.layers.push_back(make_layer({echoes}, BlendMode::screen));
        p.scene.routes.push_back(route(ModSource::hat, ModTarget::opacity, 0, 0.4, ModCurve::linear,
                                       "", ModQuantise::off, 0.0, 0.15));
        looks.push_back(std::move(p));
    }
    // The hexagon-visuals looks (docs/PLAN_HEX_VISUALS.md sections 11 and 11b.4), for the
    // Hexagon 2 pack: each sound gets its own strips (groups the hexagon templates create:
    // inner, middle, outer, spokes). Starting points to tune on the piece.
    const auto on = [](const EffectSpec &spec, BlendMode blend, float opacity, const char *group) {
        Layer l = make_layer(spec, blend, opacity);
        l.group = group;
        return l;
    };
    { // Grooves: the bass note as a clock hand, kicks along the spokes, hats at the inner corners.
        ScenePreset p{"Pitch tunnel",
                      "The bass note round the rings like a clock, its octave choosing the ring; "
                      "each kick runs out along the spokes; hats spark at the inner corners.",
                      {},
                      palette_named("UV violet")};
        p.scene.sections = {"groove", "low", "mid"};
        p.scene.layers.push_back(on({PitchClockParams{}}, BlendMode::add, 1.0f, ""));
        p.scene.layers.push_back(on({SpokeKickParams{}}, BlendMode::screen, 1.0f, "spokes"));
        p.scene.layers.push_back(on({CornerSparksParams{}}, BlendMode::add, 0.6f, "inner"));
        looks.push_back(std::move(p));
    }
    { // Grooves: every note an arc as long as it is held, each note on the next ring.
        ScenePreset p{"Note rings",
                      "Each bass note draws an arc from a corner as long as it is held, the next "
                      "note on the next ring; kicks run out along the spokes.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"groove", "low", "mid"};
        p.scene.layers.push_back(on({NoteArcsParams{}}, BlendMode::add, 1.0f, ""));
        p.scene.layers.push_back(on({SpokeKickParams{}}, BlendMode::screen, 0.8f, "spokes"));
        looks.push_back(std::move(p));
    }
    { // Grooves, high: twelve sectors stepping with the bass round the outside.
        ScenePreset p{"Radar 2",
                      "Twelve slices stepping round the outer ring with the bass notes; the "
                      "pitch clock on the inner rings; kicks along the spokes.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove", "high"};
        SectorsParams radar;
        radar.count = 12;
        radar.clock = 5; // Bass note.
        p.scene.layers.push_back(on({radar}, BlendMode::add, 1.0f, "outer"));
        p.scene.layers.push_back(on({PitchClockParams{}}, BlendMode::add, 1.0f, "inner"));
        p.scene.layers.push_back(on({PitchClockParams{}}, BlendMode::add, 1.0f, "middle"));
        p.scene.layers.push_back(on({SpokeKickParams{}}, BlendMode::screen, 1.0f, "spokes"));
        looks.push_back(std::move(p));
    }
    { // Breakdowns: a cellular automaton growing round the rings through the phrase.
        ScenePreset p{"Automaton",
                      "Each ring a row of cells running rule 90 from its corners, a step each "
                      "16th of the phrase; hats spark at the corners.",
                      {},
                      palette_named("Mono violet")};
        p.scene.sections = {"breakdown", "low"};
        RingAutomatonParams cells;
        cells.generations = 32;
        p.scene.layers.push_back(on({cells}, BlendMode::add, 1.0f, ""));
        p.scene.layers.push_back(on({CornerSparksParams{}}, BlendMode::add, 0.4f, ""));
        looks.push_back(std::move(p));
    }
    { // Intros and breakdowns: smoke flowing in, bass notes as hexagons from the inner ring.
        ScenePreset p{"Smoke",
                      "Polar noise drifting in towards the centre; each bass note sends a "
                      "hexagon out from the inner ring.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"intro", "breakdown"};
        PolarNoiseParams smoke;
        smoke.flow = -0.125;
        HexPulseParams notes;
        notes.trigger = 3; // Bass note.
        notes.start = 1;
        p.scene.layers.push_back(on({smoke}, BlendMode::alpha, 1.0f, ""));
        p.scene.layers.push_back(on({notes}, BlendMode::screen, 0.5f, ""));
        looks.push_back(std::move(p));
    }
    { // Builds: the rig fills from the inside through the phrase, dark before the drop.
        ScenePreset p{"Pressure 2",
                      "The rig fills from the centre out through the phrase, hexagons falling "
                      "in on the beats, kicks along the spokes; brighter as the build climbs, "
                      "dark for the last beat of the whole build.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        PhraseFillParams fill;
        fill.order = 1; // Inner to outer.
        HexPulseParams falling;
        falling.trigger = 1;   // Beat.
        falling.direction = 1; // In.
        StutterParams gap;
        gap.mode = 4; // Pre-drop gap, joined builds.
        p.scene.layers.push_back(on({fill}, BlendMode::add, 0.7f, ""));
        p.scene.layers.push_back(on({falling}, BlendMode::screen, 0.5f, ""));
        p.scene.layers.push_back(on({SpokeKickParams{}}, BlendMode::screen, 1.0f, "spokes"));
        p.scene.layers.push_back(on({gap}, BlendMode::multiply, 1.0f, ""));
        p.scene.routes.push_back(route(ModSource::build_progress, ModTarget::brightness, 0, 0.5));
        looks.push_back(std::move(p));
    }
    { // Drops: white kicks with ring flashes, the burst and the strobe, bass arcs outside.
        ScenePreset p{"Bloom 2",
                      "The drop's burst and white flashes, then white kicks running out along "
                      "the spokes and flashing each ring; bass arcs round the outer ring.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"drop"};
        NoteArcsParams outer;
        outer.ring_mode = 2; // Outer only.
        SpokeKickParams white;
        white.colours = 1;
        white.rings = 1;
        p.scene.layers.push_back(on({outer}, BlendMode::add, 1.0f, "outer"));
        p.scene.layers.push_back(on({white}, BlendMode::screen, 1.0f, ""));
        p.scene.layers.push_back(on({DropBurstParams{}}, BlendMode::screen, 1.0f, ""));
        p.scene.layers.push_back(on({StrobeParams{}}, BlendMode::screen, 1.0f, ""));
        looks.push_back(std::move(p));
    }
    { // Outros: a slow automaton glowing out.
        ScenePreset p{"Embers",
                      "Rule 150 stepping slowly round the rings, leaving an afterglow.",
                      {},
                      palette_named("Mono violet")};
        p.scene.sections = {"outro"};
        RingAutomatonParams cells;
        cells.rule = 150;
        cells.generations = 16;
        p.scene.layers.push_back(on({cells}, BlendMode::add, 0.6f, ""));
        p.scene.post.push_back({AfterglowParams{2.0, 1.0}, true});
        looks.push_back(std::move(p));
    }
    // The Psy club and Dark forest looks for the hexagon (section 11b.4).
    { // From Scanner: one comet round the outer ring each bar over the conveyor.
        ScenePreset p{"Scanner 2",
                      "A comet round the outer ring each bar over faint smoke, the bass "
                      "conveyor on the middle ring, kicks along the spokes.",
                      {},
                      palette_named("UV violet")};
        p.scene.sections = {"groove", "mid"};
        PolarNoiseParams smoke;
        smoke.level = 0.2;
        ChaseParams scan;
        scan.mapping = static_cast<int>(Mapping::around);
        scan.clock = 3; // Bar.
        scan.count = 1;
        ConveyorParams belt;
        belt.segment_units = 1;
        p.scene.layers.push_back(on({smoke}, BlendMode::alpha, 1.0f, ""));
        p.scene.layers.push_back(on({scan}, BlendMode::add, 1.0f, "outer"));
        p.scene.layers.push_back(on({belt}, BlendMode::add, 0.7f, "middle"));
        p.scene.layers.push_back(on({SpokeKickParams{}}, BlendMode::screen, 1.0f, "spokes"));
        looks.push_back(std::move(p));
    }
    { // From Kick pulse: kicks out along the spokes, flashing each ring.
        ScenePreset p{"Kick pulse 2",
                      "Kicks running out along the spokes and flashing each ring over smoke; "
                      "the conveyor on the outer ring.",
                      {},
                      palette_named("Ice")};
        p.scene.sections = {"groove", "high"};
        SpokeKickParams kick;
        kick.rings = 1;
        ConveyorParams belt;
        belt.segment_units = 1;
        p.scene.layers.push_back(on({PolarNoiseParams{}}, BlendMode::alpha, 0.4f, ""));
        p.scene.layers.push_back(on({kick}, BlendMode::screen, 1.0f, ""));
        p.scene.layers.push_back(on({belt}, BlendMode::add, 0.5f, "outer"));
        looks.push_back(std::move(p));
    }
    { // From Forest floor: slow organic smoke, bass arcs outside, hats inside.
        ScenePreset p{"Forest floor 2",
                      "Slow, finer smoke drifting out; bass arcs round the outer ring; hats "
                      "spark at the inner corners.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove", "low", "mid"};
        PolarNoiseParams smoke;
        smoke.rings_scale = 3.0;
        smoke.lobes = 5.0;
        smoke.flow = 0.06;
        p.scene.layers.push_back(on({smoke}, BlendMode::alpha, 1.0f, ""));
        p.scene.layers.push_back(on({NoteArcsParams{}}, BlendMode::add, 0.6f, "outer"));
        p.scene.layers.push_back(on({CornerSparksParams{}}, BlendMode::add, 0.4f, "inner"));
        looks.push_back(std::move(p));
    }
    { // From Pitch meter: the phrase filling, the pitch clock with a trail.
        ScenePreset p{"Pitch meter 2",
                      "The rig filling from the centre through each phrase; the bass note on "
                      "the pitch clock with its last three notes trailing; kicks along the "
                      "spokes.",
                      {},
                      palette_named("Aurora")};
        p.scene.sections = {"groove"};
        PhraseFillParams fill;
        fill.order = 1;
        fill.hold_level = 0.15;
        PitchClockParams clock;
        clock.trail = 3;
        p.scene.layers.push_back(on({fill}, BlendMode::add, 0.5f, ""));
        p.scene.layers.push_back(on({clock}, BlendMode::add, 1.0f, ""));
        p.scene.layers.push_back(on({SpokeKickParams{}}, BlendMode::screen, 0.6f, "spokes"));
        looks.push_back(std::move(p));
    }
    { // From Acid gate: the 16th ramps confined to the outer ring.
        ScenePreset p{"Acid gate 2",
                      "Ramps racing round the outer ring on the 16ths, opened and closed by the "
                      "bassline's gate; hue follows the acid's brightness; kicks along the "
                      "spokes.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove", "high"};
        WaveParams ramps;
        ramps.mapping = 0;
        ramps.wings = 2;
        ramps.clock = 1; // 16th.
        ramps.rate = 0.25;
        ramps.shape = 2; // Ramp.
        ramps.spread = 3.0;
        ramps.level = 0.9;
        StutterParams gate;
        gate.mode = 1;
        gate.release_s = 0.03;
        p.scene.layers.push_back(on({ramps}, BlendMode::add, 1.0f, "outer"));
        p.scene.layers.push_back(on({gate}, BlendMode::multiply, 1.0f, "outer"));
        p.scene.layers.push_back(on({SpokeKickParams{}}, BlendMode::screen, 0.7f, "spokes"));
        p.scene.routes.push_back(route(ModSource::residual_brightness, ModTarget::hue_shift, 0,
                                       90.0, ModCurve::linear, "", ModQuantise::off, 0.05, 0.3));
        looks.push_back(std::move(p));
    }
    { // From Fire riser and Strobe roll: smoke flowing out faster with the build.
        ScenePreset p{"Riser 2",
                      "Smoke flowing outwards faster as the whole build climbs, automata on the "
                      "inner rings, a strobe roll on the spokes at the build's end, then dark "
                      "for its last beat.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        RingAutomatonParams cells;
        cells.generations = 128;
        StrobeParams roll;
        roll.trigger = 1; // Build end.
        roll.flashes = 4;
        roll.colour = 1;
        roll.level = 0.8;
        StutterParams gap;
        gap.mode = 4;
        p.scene.layers.push_back(on({PolarNoiseParams{}}, BlendMode::alpha, 1.0f, ""));
        p.scene.layers.push_back(on({cells}, BlendMode::add, 1.0f, "inner"));
        p.scene.layers.push_back(on({cells}, BlendMode::add, 1.0f, "middle"));
        p.scene.layers.push_back(on({roll}, BlendMode::screen, 1.0f, "spokes"));
        p.scene.layers.push_back(on({gap}, BlendMode::multiply, 1.0f, ""));
        p.scene.routes.push_back(route(ModSource::build_progress, ModTarget::setting, 0, 0.15,
                                       ModCurve::linear, "flow"));
        looks.push_back(std::move(p));
    }
    // BEGIN Hexagon 3 scenes (generated from hex3_spec.py)
    {
        ScenePreset p{"Tunnel 3",
                      "Hexagons drawn inwards on the beat, the kick's hexagon pushing out "
                      "from the inner ring, sectors stepping with the bass, the bass note "
                      "round the outer ring.",
                      {},
                      palette_named("UV violet")};
        p.scene.sections = {"groove", "low", "mid"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 6.0},
                                              {"clock", 2.0},
                                              {"rate", -1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.5},
                                              {"width", 0.25},
                                              {"level", 0.35},
                                              {"colour_spread", 0.5}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "sectors",
            {{"clock", 5.0}, {"count", 6.0}, {"tail", 3.0}, {"level", 0.5}, {"colour_spread", 1.0}},
            "add", 0.7f, ""));
        p.scene.layers.push_back(layer_by_id("hex_pulse",
                                             {{"trigger", 0.0},
                                              {"travel_beats", 1.0},
                                              {"width", 0.12},
                                              {"decay_s", 0.5},
                                              {"keep", 3.0}},
                                             "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "pitch_clock",
            {{"width", 0.07}, {"confidence", 0.2}, {"release_s", 0.15}, {"spokes", 0.0}}, "add",
            1.0f, "outer"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Radar 3",
                      "Twelve slices stepping round with the bass, a hexagon closing in each "
                      "bar, ducking after each kick; the kick itself runs out along the "
                      "spokes.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove", "mid"};
        p.scene.layers.push_back(layer_by_id("sectors",
                                             {{"clock", 5.0},
                                              {"count", 12.0},
                                              {"tail", 6.0},
                                              {"level", 0.9},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("hex_pulse",
                                             {{"trigger", 2.0},
                                              {"direction", 1.0},
                                              {"travel_beats", 4.0},
                                              {"decay_s", 2.0},
                                              {"keep", 1.0},
                                              {"level", 0.7},
                                              {"width", 0.1}},
                                             "screen", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("stutter", {{"mode", 2.0}, {"depth", 0.6}}, "multiply", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("spoke_kick", {{"tail", 0.8}, {"travel_beats", 1.0}},
                                             "screen", 1.0f, "spokes"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Ring rain",
                      "Every bass note a hexagon going out, kicks as wide bright hexagons, "
                      "hats sparking at the corners.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"groove", "high"};
        p.scene.layers.push_back(layer_by_id("hex_pulse",
                                             {{"trigger", 3.0},
                                              {"keep", 4.0},
                                              {"travel_beats", 1.0},
                                              {"width", 0.08},
                                              {"decay_s", 0.4},
                                              {"level", 0.7}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse", {{"trigger", 0.0}, {"width", 0.15}, {"decay_s", 0.4}, {"keep", 2.0}},
            "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "corner_sparks", {{"span", 2.0}, {"count", 2.0}, {"level", 0.9}}, "add", 0.6f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Echo field",
                      "Polar smoke drifting in, each bass note a hexagon, hats at the "
                      "corners: dim but never dark.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"breakdown", "low"};
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.7}, {"floor", 0.25}, {"flow", -0.1}, {"clock", 3.0}},
            "alpha", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("hex_pulse",
                                             {{"trigger", 3.0},
                                              {"keep", 4.0},
                                              {"travel_beats", 1.0},
                                              {"width", 0.08},
                                              {"decay_s", 0.6},
                                              {"level", 0.6}},
                                             "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "corner_sparks", {{"span", 2.0}, {"count", 2.0}, {"level", 0.9}}, "add", 0.6f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Pressure 3",
                      "The rings fill from the inside through the phrase, hexagons falling in "
                      "on the beats, kicks pushing out; brighter as the whole build climbs, "
                      "dark only for its last beat.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 6.0},
                                              {"clock", 2.0},
                                              {"rate", -1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.3},
                                              {"level", 0.45},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "phrase_fill", {{"order", 1.0}, {"hold_level", 0.45}, {"steps", 32.0}, {"level", 0.7}},
            "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse", {{"trigger", 1.0}, {"direction", 1.0}, {"colours", 1.0}, {"width", 0.12}},
            "screen", 0.8f, ""));
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse",
            {{"trigger", 0.0}, {"width", 0.08}, {"travel_beats", 0.5}, {"decay_s", 0.25}}, "screen",
            1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("stutter", {{"mode", 4.0}, {"depth", 1.0}}, "multiply", 1.0f, ""));
        p.scene.routes.push_back(route_by_id("build_progress", "brightness", 0, 0.25, "", false));
        p.scene.routes.push_back(route_by_id("riser", "opacity", 2, 0.4, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Bloom 3",
                      "The drop's burst and white flashes, then hexagon rings pumping on the "
                      "16ths and a wide hexagon on every kick.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"drop"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 6.0},
                                              {"clock", 1.0},
                                              {"rate", 0.25},
                                              {"shape", 3.0},
                                              {"spread", 2.0},
                                              {"width", 0.3},
                                              {"level", 0.4},
                                              {"colour_spread", 0.5}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse", {{"trigger", 0.0}, {"width", 0.15}, {"decay_s", 0.4}, {"keep", 2.0}},
            "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("drop_burst", {}, "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("strobe", {}, "screen", 1.0f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Echo intro",
                      "Faint smoke; each bass note sends a hexagon out from the inner ring; "
                      "hats at the corners.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"intro"};
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.6}, {"floor", 0.2}, {"flow", 0.05}, {"clock", 3.0}},
            "alpha", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("hex_pulse",
                                             {{"trigger", 3.0},
                                              {"keep", 4.0},
                                              {"travel_beats", 1.0},
                                              {"width", 0.08},
                                              {"decay_s", 0.6},
                                              {"level", 0.6}},
                                             "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "corner_sparks", {{"span", 2.0}, {"count", 2.0}, {"level", 0.9}}, "add", 0.6f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Deep tunnel",
                      "Slow hexagons drawn inwards each bar over a cellular automaton, with "
                      "an afterglow.",
                      {},
                      palette_named("Mono violet")};
        p.scene.sections = {"outro"};
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.5}, {"floor", 0.2}, {"flow", -0.05}, {"clock", 3.0}},
            "alpha", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 6.0},
                                              {"clock", 3.0},
                                              {"rate", -0.5},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.3},
                                              {"level", 0.9}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("ring_automaton", {{"rule", 150.0}, {"generations", 16.0}, {"level", 0.8}},
                        "add", 0.8f, ""));
        p.scene.post.push_back({AfterglowParams{1.5, 0.7}, true});
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Cube 3",
                      "The hexagon as a cube flipping every bar, a hexagon on every kick, "
                      "each bass note an arc round the outer ring.",
                      {},
                      palette_named("UV violet")};
        p.scene.sections = {"groove", "low", "mid"};
        p.scene.layers.push_back(
            layer_by_id("cube", {{"flip", 1.0}, {"level", 0.4}, {"edges", 0.9}, {"fade_s", 0.06}},
                        "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("hex_pulse",
                                             {{"trigger", 0.0},
                                              {"travel_beats", 1.0},
                                              {"width", 0.12},
                                              {"decay_s", 0.5},
                                              {"keep", 3.0}},
                                             "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "note_arcs", {{"ring_mode", 2.0}, {"arc_per_16th", 0.25}, {"confidence", 0.2}}, "add",
            1.0f, "outer"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Lattice",
                      "A hexagonal lattice drifting and pumping on each kick, sectors "
                      "stepping with the bass, kicks along the spokes.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"groove", "mid"};
        p.scene.layers.push_back(layer_by_id(
            "interference",
            {{"level", 0.6}, {"kick_pump", 1.0}, {"rate", 0.25}, {"clock", 2.0}, {"scale_m", 0.6}},
            "add", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("sectors", {{"clock", 5.0}, {"count", 6.0}, {"tail", 2.0}, {"level", 0.6}},
                        "add", 0.6f, ""));
        p.scene.layers.push_back(layer_by_id("spoke_kick", {{"tail", 0.8}, {"travel_beats", 1.0}},
                                             "screen", 1.0f, "spokes"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Star radar",
                      "Twelve slices stepping with the bass, the cube's Y flashing on the "
                      "kick, the bass note round the middle ring.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove", "high"};
        p.scene.layers.push_back(layer_by_id("sectors",
                                             {{"clock", 5.0},
                                              {"count", 12.0},
                                              {"tail", 4.0},
                                              {"level", 0.8},
                                              {"colour_spread", 0.4}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("cube", {{"flip", 4.0}, {"level", 0.0}, {"edges", 1.0}, {"fade_s", 0.0}},
                        "screen", 0.8f, ""));
        p.scene.layers.push_back(
            layer_by_id("stutter", {{"mode", 2.0}, {"depth", 0.5}}, "multiply", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("pitch_clock", {{"width", 0.08}, {"confidence", 0.2}, {"spokes", 0.0}},
                        "add", 1.0f, "middle"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Facets",
                      "The cube turning once a phrase, slowly, rule 90 growing round the "
                      "rings, hats at the corners.",
                      {},
                      palette_named("Mono violet")};
        p.scene.sections = {"breakdown", "low"};
        p.scene.layers.push_back(
            layer_by_id("cube", {{"flip", 3.0}, {"level", 0.5}, {"edges", 0.9}, {"fade_s", 1.0}},
                        "add", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("ring_automaton", {{"rule", 90.0}, {"generations", 32.0}, {"level", 0.6}},
                        "add", 0.8f, ""));
        p.scene.layers.push_back(layer_by_id(
            "corner_sparks", {{"span", 2.0}, {"count", 2.0}, {"level", 0.9}}, "add", 0.6f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Fold",
                      "Slices filling round on the beats, hexagons falling inwards, the "
                      "phrase filling side by side; dark for the build's last beat.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 6.0},
                                              {"clock", 2.0},
                                              {"rate", -1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.3},
                                              {"level", 0.45},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("phrase_fill",
                                             {{"order", 0.0}, {"hold_level", 0.45}, {"level", 0.7}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("sectors", {{"mode", 2.0}, {"clock", 2.0}, {"count", 6.0}, {"level", 0.6}},
                        "add", 0.6f, ""));
        p.scene.layers.push_back(
            layer_by_id("hex_pulse", {{"trigger", 1.0}, {"direction", 1.0}, {"width", 0.12}},
                        "screen", 0.7f, ""));
        p.scene.layers.push_back(
            layer_by_id("stutter", {{"mode", 4.0}, {"depth", 1.0}}, "multiply", 1.0f, ""));
        p.scene.routes.push_back(route_by_id("build_progress", "brightness", 0, 0.25, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Shatter",
                      "The drop's burst and flashes, the cube flipping on every kick, white "
                      "kicks flashing each ring.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"drop"};
        p.scene.layers.push_back(
            layer_by_id("cube", {{"flip", 4.0}, {"level", 0.45}, {"edges", 1.0}, {"fade_s", 0.03}},
                        "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "spoke_kick", {{"colours", 1.0}, {"rings", 1.0}, {"tail", 0.8}}, "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("drop_burst", {}, "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("strobe", {}, "screen", 1.0f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Facet intro",
                      "A faint lattice drifting; each bass note a hexagon from the inner "
                      "ring.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"intro"};
        p.scene.layers.push_back(layer_by_id(
            "interference",
            {{"level", 0.75}, {"kick_pump", 0.0}, {"rate", 0.1}, {"clock", 3.0}, {"scale_m", 0.6}},
            "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse",
            {{"trigger", 3.0}, {"keep", 4.0}, {"width", 0.08}, {"decay_s", 0.6}, {"level", 0.6}},
            "screen", 1.0f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Fading cube",
                      "The cube turning once a phrase, glowing out.",
                      {},
                      palette_named("Mono violet")};
        p.scene.sections = {"outro"};
        p.scene.layers.push_back(
            layer_by_id("cube", {{"flip", 3.0}, {"level", 0.55}, {"edges", 0.9}, {"fade_s", 2.0}},
                        "add", 1.0f, ""));
        p.scene.post.push_back({AfterglowParams{2.0, 0.6}, true});
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Pulse 3",
                      "Kick hexagons from the inner ring out, the bass note as a clock hand "
                      "on the middle ring and its spoke, hats sparking at the inner corners.",
                      {},
                      palette_named("UV violet")};
        p.scene.sections = {"groove", "low", "mid"};
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse", {{"trigger", 0.0}, {"keep", 2.0}, {"width", 0.14}, {"decay_s", 0.45}},
            "add", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("pitch_clock", {{"width", 0.1}, {"confidence", 0.2}, {"release_s", 0.2}},
                        "screen", 1.0f, "middle"));
        p.scene.layers.push_back(layer_by_id("corner_sparks",
                                             {{"span", 2.0}, {"count", 3.0}, {"level", 0.9}}, "add",
                                             0.7f, "inner"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Arc pulse",
                      "Kick hexagons, each bass note an arc on the outer ring with the last "
                      "three still showing.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"groove", "mid"};
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse", {{"trigger", 0.0}, {"keep", 2.0}, {"width", 0.14}, {"decay_s", 0.45}},
            "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("note_arcs",
                                             {{"ring_mode", 2.0},
                                              {"history", 3.0},
                                              {"arc_per_16th", 0.2},
                                              {"confidence", 0.2},
                                              {"release_s", 0.3}},
                                             "screen", 1.0f, "outer"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Hammer",
                      "Wide kick hexagons, ramps racing round the outer ring on the 16ths "
                      "gated by the bassline, kicks flashing along the spokes.",
                      {},
                      palette_named("Toxic")};
        p.scene.sections = {"groove", "high"};
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse", {{"trigger", 0.0}, {"keep", 2.0}, {"width", 0.2}, {"decay_s", 0.35}},
            "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 7.0},
                                              {"wings", 2.0},
                                              {"clock", 1.0},
                                              {"rate", 0.25},
                                              {"shape", 2.0},
                                              {"spread", 3.0},
                                              {"level", 0.9}},
                                             "add", 1.0f, "outer"));
        p.scene.layers.push_back(layer_by_id("stutter", {{"mode", 1.0}, {"release_s", 0.03}},
                                             "multiply", 1.0f, "outer"));
        p.scene.layers.push_back(
            layer_by_id("spoke_kick", {{"tail", 0.8}}, "screen", 1.0f, "spokes"));
        p.scene.routes.push_back(
            route_by_id("residual_brightness", "hue_shift", 1, 90.0, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Breath",
                      "Smoke breathing, the bass note on the middle ring with its last three "
                      "notes trailing, a hexagon per bass note.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"breakdown", "low"};
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.7}, {"floor", 0.25}, {"flow", -0.08}, {"clock", 3.0}},
            "alpha", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("pitch_clock",
                        {{"width", 0.1}, {"trail", 3.0}, {"confidence", 0.2}, {"release_s", 0.3}},
                        "screen", 1.0f, "middle"));
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse", {{"trigger", 3.0}, {"keep", 3.0}, {"width", 0.08}, {"decay_s", 0.5}},
            "screen", 0.6f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Climb",
                      "The rings filling from the inside, hexagons falling in on the beats, "
                      "kicks along the spokes, a strobe roll on the spokes at the end, then "
                      "dark.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 6.0},
                                              {"clock", 2.0},
                                              {"rate", -1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.3},
                                              {"level", 0.45},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("phrase_fill",
                                             {{"order", 1.0}, {"hold_level", 0.45}, {"level", 0.7}},
                                             "add", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("hex_pulse", {{"trigger", 1.0}, {"direction", 1.0}, {"width", 0.12}},
                        "screen", 0.8f, ""));
        p.scene.layers.push_back(
            layer_by_id("spoke_kick", {{"tail", 0.8}}, "screen", 1.0f, "spokes"));
        p.scene.layers.push_back(layer_by_id(
            "strobe", {{"trigger", 1.0}, {"flashes", 4.0}, {"colour", 1.0}, {"level", 0.8}},
            "screen", 1.0f, "spokes"));
        p.scene.layers.push_back(
            layer_by_id("stutter", {{"mode", 4.0}, {"depth", 1.0}}, "multiply", 1.0f, ""));
        p.scene.routes.push_back(route_by_id("build_progress", "brightness", 0, 0.25, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Impact 3",
                      "The drop's burst and flashes, white kick hexagons flashing each ring "
                      "as they pass, bass arcs on the outer ring.",
                      {},
                      palette_named("Magenta storm")};
        p.scene.sections = {"drop"};
        p.scene.layers.push_back(layer_by_id(
            "note_arcs", {{"ring_mode", 2.0}, {"arc_per_16th", 0.25}, {"confidence", 0.2}}, "add",
            1.0f, "outer"));
        p.scene.layers.push_back(layer_by_id(
            "spoke_kick", {{"colours", 1.0}, {"rings", 1.0}, {"tail", 0.8}}, "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse", {{"trigger", 0.0}, {"width", 0.15}, {"decay_s", 0.35}, {"keep", 2.0}},
            "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("drop_burst", {}, "screen", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("strobe", {}, "screen", 1.0f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Heartbeat",
                      "Faint smoke, each bass note a hexagon going out.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"intro"};
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.6}, {"floor", 0.2}, {"flow", 0.05}, {"clock", 3.0}},
            "alpha", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id(
            "hex_pulse",
            {{"trigger", 3.0}, {"keep", 4.0}, {"width", 0.1}, {"decay_s", 0.6}, {"level", 0.7}},
            "screen", 1.0f, ""));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Afterglow",
                      "A hexagon each bar, slowly, over a glowing automaton.",
                      {},
                      palette_named("Mono violet")};
        p.scene.sections = {"outro"};
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.5}, {"floor", 0.2}, {"flow", -0.05}, {"clock", 3.0}},
            "alpha", 1.0f, ""));
        p.scene.layers.push_back(layer_by_id("hex_pulse",
                                             {{"trigger", 2.0},
                                              {"travel_beats", 4.0},
                                              {"width", 0.2},
                                              {"decay_s", 3.0},
                                              {"keep", 2.0},
                                              {"level", 0.9}},
                                             "screen", 1.0f, ""));
        p.scene.layers.push_back(
            layer_by_id("ring_automaton", {{"rule", 150.0}, {"generations", 16.0}, {"level", 0.8}},
                        "add", 0.8f, ""));
        p.scene.post.push_back({AfterglowParams{2.0, 0.8}, true});
        looks.push_back(std::move(p));
    }
    // END Hexagon 3 scenes
    // BEGIN Cagan scenes (generated from cagan_packs.py)
    {
        ScenePreset p{"Dormant",
                      "The halo breathing once a phrase over a dim glow, a faint drift in the "
                      "panels, hats as single sparks on the arm tips.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"intro", "outro"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.12},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, "halo"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 4.0},
                                              {"rate", 1.0},
                                              {"shape", 0.0},
                                              {"spread", 0.0},
                                              {"level", 0.35},
                                              {"colour_spread", 0.3}},
                                             "screen", 1.0f, "halo"));
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.3}, {"floor", 0.12}, {"flow", 0.05}, {"clock", 3.0}},
            "screen", 1.0f, "panels"));
        p.scene.layers.push_back(layer_by_id(
            "corner_sparks",
            {{"sites", 1.0}, {"count", 6.0}, {"span", 1.0}, {"decay_s", 0.1}, {"level", 0.9}},
            "add", 1.0f, "arms"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"First light",
                      "The arms lighting one after another round the piece, twice a phrase, "
                      "the light flowing out along each; domes dark, the halo and panels a "
                      "dim glow.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"intro", "outro"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.1},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, "halo"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.05},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, "panels"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"clock", 4.0},
                                              {"rate", 2.0},
                                              {"shape", 4.0},
                                              {"spread", 0.5},
                                              {"width", 0.25},
                                              {"level", 0.8},
                                              {"colour_spread", 0.5},
                                              {"turn_offset", 1.0}},
                                             "screen", 1.0f, "arms"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Nerve",
                      "The bassline on the arms, each note on the next arm five round "
                      "(jumping across the piece whatever its pitch), coloured by pitch; the "
                      "domes thumping on the kick; everything else dark.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"groove", "low", "mid", "high"};
        p.scene.layers.push_back(layer_by_id("arm_notes",
                                             {{"arm_mode", 3.0},
                                              {"step", 5.0},
                                              {"colours", 2.0},
                                              {"per_16th", 1.5},
                                              {"confidence", 0.3}},
                                             "add", 1.0f, "arms"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"radial_from", 1.0},
                                              {"clock", 2.0},
                                              {"rate", 1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.5},
                                              {"level", 1.0},
                                              {"colour_spread", 0.4}},
                                             "screen", 0.15f, "domes"));
        p.scene.routes.push_back(route_by_id("kick", "opacity", 1, 0.85, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Tide",
                      "Every bass note on all twelve arms, a slow radial wave in the panels a "
                      "bar long, the kick on the domes; the rim dark.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"groove", "mid", "high"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"clock", 3.0},
                                              {"rate", -1.0},
                                              {"shape", 0.0},
                                              {"spread", 1.0},
                                              {"level", 0.35},
                                              {"colour_spread", 0.5}},
                                             "add", 1.0f, "panels"));
        p.scene.layers.push_back(
            layer_by_id("arm_notes", {{"arm_mode", 2.0}, {"colours", 1.0}, {"confidence", 0.3}},
                        "screen", 0.8f, "arms"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"radial_from", 1.0},
                                              {"clock", 2.0},
                                              {"rate", 1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.5},
                                              {"level", 1.0},
                                              {"colour_spread", 0.4}},
                                             "screen", 0.15f, "domes"));
        p.scene.routes.push_back(route_by_id("kick", "opacity", 2, 0.85, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Spine",
                      "The bassline crawling round the arms a note at a time, coloured by "
                      "pitch, its last four notes trailing; the kick stepping round the three "
                      "domes one at a time, the halo dim.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"groove", "low", "mid"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.1},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, "halo"));
        p.scene.layers.push_back(layer_by_id("arm_notes",
                                             {{"arm_mode", 3.0},
                                              {"step", 1.0},
                                              {"colours", 2.0},
                                              {"trail", 4.0},
                                              {"confidence", 0.3}},
                                             "screen", 1.0f, "arms"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 4.0},
                                              {"clock", 6.0},
                                              {"rate", 0.3333333333333333},
                                              {"shape", 4.0},
                                              {"spread", 0.0},
                                              {"width", 0.3},
                                              {"level", 1.0},
                                              {"colour_spread", 0.4},
                                              {"turn_offset", 1.0}},
                                             "screen", 0.15f, "domes"));
        p.scene.routes.push_back(route_by_id("kick", "opacity", 2, 0.85, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Lure",
                      "The glow-led alternative: arms dark but for hats at their tips, the "
                      "bass colouring the panels by pitch and lifting them on each note, the "
                      "kick on the domes.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"groove", "low"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"clock", 3.0},
                                              {"rate", -0.5},
                                              {"shape", 0.0},
                                              {"spread", 0.5},
                                              {"level", 0.6},
                                              {"colour_spread", 0.2}},
                                             "add", 1.0f, "panels"));
        p.scene.layers.push_back(layer_by_id(
            "corner_sparks",
            {{"sites", 1.0}, {"count", 6.0}, {"span", 1.0}, {"decay_s", 0.1}, {"level", 0.9}},
            "add", 1.0f, "arms"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"radial_from", 1.0},
                                              {"clock", 2.0},
                                              {"rate", 1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.5},
                                              {"level", 1.0},
                                              {"colour_spread", 0.4}},
                                             "screen", 0.15f, "domes"));
        p.scene.routes.push_back(route_by_id("bass_pitch", "hue_shift", 0, 360.0, "", false));
        p.scene.routes.push_back(route_by_id("bass_active", "opacity", 0, 0.5, "", false));
        p.scene.routes.push_back(route_by_id("kick", "opacity", 2, 0.85, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Inhale",
                      "Light drawn from the rim into the centre on every beat, faster and "
                      "brighter as the tension climbs; the arms filling from the centre "
                      "outwards through the build; dark over the pre-drop.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"clock", 2.0},
                                              {"rate", -1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.5},
                                              {"level", 0.6},
                                              {"colour_spread", 0.4}},
                                             "add", 1.0f, "glow"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 1.8},
                                              {"width", 0.01},
                                              {"level", 0.7},
                                              {"colour_spread", 0.3}},
                                             "screen", 1.0f, "arms"));
        p.scene.layers.push_back(
            layer_by_id("stutter", {{"mode", 5.0}, {"depth", 1.0}}, "multiply", 1.0f, ""));
        p.scene.routes.push_back(route_by_id("tension", "brightness", 0, 0.4, "", false));
        p.scene.routes.push_back(route_by_id("tension", "setting", 0, -0.0625, "rate", false));
        p.scene.routes.push_back(route_by_id("build_progress", "setting", 1, 1.0, "width", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Swarm",
                      "Sparks on the arms growing denser with the tension, the domes "
                      "brightening and the halo rising; dark over the pre-drop.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"build"};
        p.scene.layers.push_back(
            layer_by_id("sparkle", {{"density", 0.02}, {"decay_s", 0.12}}, "add", 1.0f, "arms"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.6},
                                              {"colour_spread", 0.3}},
                                             "add", 0.2f, "domes"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.5},
                                              {"colour_spread", 0.3}},
                                             "add", 0.3f, "halo"));
        p.scene.layers.push_back(
            layer_by_id("stutter", {{"mode", 5.0}, {"depth", 1.0}}, "multiply", 1.0f, ""));
        p.scene.routes.push_back(route_by_id("tension", "setting", 0, 0.75, "density", false));
        p.scene.routes.push_back(route_by_id("tension", "opacity", 1, 0.8, "", false));
        p.scene.routes.push_back(route_by_id("tension", "opacity", 2, 0.7, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Bloom",
                      "The kick as a coloured shockwave from the centre across the whole "
                      "piece, each bass note on three arms evenly round, moving on a note and "
                      "coloured by pitch, panels and rim in full colour.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"drop"};
        p.scene.layers.push_back(layer_by_id("polar_noise",
                                             {{"level", 0.55},
                                              {"floor", 0.3},
                                              {"flow", 0.25},
                                              {"clock", 2.0},
                                              {"colour_spread", 1.0}},
                                             "add", 1.0f, "glow"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"clock", 2.0},
                                              {"rate", 1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.3},
                                              {"level", 1.0},
                                              {"colour_spread", 0.6}},
                                             "screen", 0.2f, ""));
        p.scene.layers.push_back(layer_by_id("arm_notes",
                                             {{"arm_mode", 3.0},
                                              {"step", 1.0},
                                              {"arms_per_note", 3.0},
                                              {"colours", 2.0},
                                              {"confidence", 0.2}},
                                             "screen", 1.0f, "arms"));
        p.scene.routes.push_back(route_by_id("kick", "opacity", 1, 0.8, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Strike",
                      "The kick on the domes and the rim together, all twelve arms firing "
                      "outwards on each bass note, the panels dark for contrast.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"drop"};
        p.scene.layers.push_back(layer_by_id(
            "arm_notes",
            {{"arm_mode", 2.0}, {"colours", 1.0}, {"per_16th", 2.0}, {"confidence", 0.2}}, "add",
            1.0f, "arms"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"radial_from", 1.0},
                                              {"clock", 2.0},
                                              {"rate", 1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.5},
                                              {"level", 1.0},
                                              {"colour_spread", 0.4}},
                                             "screen", 0.15f, "domes"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"clock", 2.0},
                                              {"rate", 1.0},
                                              {"shape", 4.0},
                                              {"spread", 1.0},
                                              {"width", 0.4},
                                              {"level", 0.9},
                                              {"colour_spread", 0.5}},
                                             "screen", 0.15f, "rim"));
        p.scene.routes.push_back(route_by_id("kick", "opacity", 1, 0.85, "", false));
        p.scene.routes.push_back(route_by_id("kick", "opacity", 2, 0.85, "", false));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Drift",
                      "Polar smoke in the panels and rim, the three domes breathing in turn "
                      "once a bar, the arms as a slow outward ripple.",
                      {},
                      palette_named("Ocean UV")};
        p.scene.sections = {"breakdown"};
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.45}, {"floor", 0.2}, {"flow", 0.05}, {"clock", 3.0}}, "add",
            1.0f, "panels"));
        p.scene.layers.push_back(layer_by_id(
            "polar_noise", {{"level", 0.35}, {"floor", 0.15}, {"flow", -0.04}, {"clock", 3.0}},
            "add", 1.0f, "rim"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 4.0},
                                              {"clock", 3.0},
                                              {"rate", 1.0},
                                              {"shape", 4.0},
                                              {"spread", 0.0},
                                              {"width", 0.5},
                                              {"level", 0.5},
                                              {"colour_spread", 0.4},
                                              {"turn_offset", 1.0}},
                                             "screen", 1.0f, "domes"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 3.0},
                                              {"clock", 3.0},
                                              {"rate", 1.0},
                                              {"shape", 0.0},
                                              {"spread", 2.0},
                                              {"level", 0.3},
                                              {"colour_spread", 0.4}},
                                             "screen", 1.0f, "arms"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Held breath",
                      "The domes hold a dim glow, the arms dark, each bass note a short flick "
                      "at the arm roots.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"break"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.2},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, "domes"));
        p.scene.layers.push_back(layer_by_id("arm_notes",
                                             {{"arm_mode", 3.0},
                                              {"step", 5.0},
                                              {"per_16th", 0.4},
                                              {"release", 0.5},
                                              {"colours", 2.0},
                                              {"confidence", 0.3},
                                              {"level", 1.0}},
                                             "screen", 1.0f, "arms"));
        looks.push_back(std::move(p));
    }
    {
        ScenePreset p{"Afterimage",
                      "The halo and panels held dim under a long afterglow, hats sparking on "
                      "the arm tips and leaving trails.",
                      {},
                      palette_named("Ember")};
        p.scene.sections = {"break"};
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.08},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, "halo"));
        p.scene.layers.push_back(layer_by_id("wave",
                                             {{"mapping", 0.0},
                                              {"clock", 0.0},
                                              {"rate", 0.0},
                                              {"shape", 3.0},
                                              {"spread", 0.0},
                                              {"width", 1.0},
                                              {"level", 0.03},
                                              {"colour_spread", 0.3}},
                                             "add", 1.0f, "panels"));
        p.scene.layers.push_back(layer_by_id("corner_sparks",
                                             {{"sites", 1.0},
                                              {"count", 6.0},
                                              {"span", 2.0},
                                              {"keep", 4.0},
                                              {"decay_s", 0.2},
                                              {"level", 1.0}},
                                             "add", 1.0f, "arms"));
        p.scene.post.push_back({AfterglowParams{4.0, 0.85}, true});
        looks.push_back(std::move(p));
    }
    // END Cagan scenes
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

    // Choreographed packs: a scene for every section, grooves by energy, builds that go dark
    // before the drop.
    ShowPack club = pack_of(
        "Psy club",
        "A full arc for a club: scanner grooves stepping up to comet rain and kick pulses at high "
        "energy, a nebula in breakdowns, fire climbing with the riser, a blooming drop.",
        {find(looks, "Scanner"), find(looks, "Comet rain"), find(looks, "Kick pulse"),
         find(looks, "Nebula"), find(looks, "Fire riser"), find(looks, "Drop bloom"),
         find(looks, "Deep space")});
    club.automation.palette_every_phrases = 2;
    packs.push_back(std::move(club));

    ShowPack forest = pack_of(
        "Dark forest",
        "Darker and organic: forest-floor noise and the pitch meter, acid gates when the energy "
        "rises, strobe rolls into the drops; the palette moves every 4 phrases.",
        {find(looks, "Forest floor"), find(looks, "Pitch meter"), find(looks, "Acid gate"),
         find(looks, "Nebula"), find(looks, "Strobe roll"), find(looks, "Drop impact"),
         find(looks, "Deep space")});
    forest.automation.palette_every_phrases = 4;
    forest.automation.crossfade_beats = 8.0;
    packs.push_back(std::move(forest));
    // BEGIN Cagan packs (generated from cagan_packs.py)
    {
        ShowPack pack = rotating_pack(
            "Cagan ember",
            "For the Cagan piece in warm reds and ambers with cool "
            "accents: lush glow in the intros and breakdowns, the "
            "bassline on the arms and the kick on the domes in "
            "grooves, everything lit in the drops.",
            {find(looks, "Dormant"), find(looks, "First light"), find(looks, "Nerve"),
             find(looks, "Tide"), find(looks, "Spine"), find(looks, "Lure"), find(looks, "Inhale"),
             find(looks, "Swarm"), find(looks, "Bloom"), find(looks, "Strike"),
             find(looks, "Drift"), find(looks, "Held breath"), find(looks, "Afterimage")},
            {{"Dormant", {"Ember", "Ember tide"}},
             {"First light", {"Ember", "Ember tide"}},
             {"Nerve", {"Ember", "Amber"}},
             {"Tide", {"Ember", "Amber"}},
             {"Spine", {"Ember", "Amber"}},
             {"Lure", {"Ember", "Amber"}},
             {"Inhale", {"Amber", "Ember"}},
             {"Swarm", {"Amber", "Ember"}},
             {"Bloom", {"Ember", "Ember tide"}},
             {"Strike", {"Ember", "Ember tide"}},
             {"Drift", {"Ember", "Ember tide"}},
             {"Held breath", {"Ember", "Ember tide"}},
             {"Afterimage", {"Ember", "Ember tide"}}});
        for (Scene &s : pack.scenes) {
            s.palette = s.palette_list.front(); // Its own list, not the look's.
        }
        packs.push_back(std::move(pack));
    }
    {
        ShowPack pack = rotating_pack(
            "Cagan forest",
            "For the Cagan piece in deep greens, teals and violets "
            "with rare hot accents: the glow zones held low "
            "throughout, the arms carry the show.",
            {find(looks, "Dormant"), find(looks, "First light"), find(looks, "Nerve"),
             find(looks, "Tide"), find(looks, "Spine"), find(looks, "Lure"), find(looks, "Inhale"),
             find(looks, "Swarm"), find(looks, "Bloom"), find(looks, "Strike"),
             find(looks, "Drift"), find(looks, "Held breath"), find(looks, "Afterimage")},
            {{"Dormant", {"Moss", "Mono violet"}},
             {"First light", {"Moss", "Mono violet"}},
             {"Nerve", {"Moss", "Fern glow"}},
             {"Tide", {"Moss", "Fern glow"}},
             {"Spine", {"Moss", "Fern glow"}},
             {"Lure", {"Moss", "Fern glow"}},
             {"Inhale", {"Fern glow", "Toxic"}},
             {"Swarm", {"Fern glow", "Toxic"}},
             {"Bloom", {"Fern glow", "Moss"}},
             {"Strike", {"Fern glow", "Moss"}},
             {"Drift", {"Moss", "Mono violet"}},
             {"Held breath", {"Moss", "Mono violet"}},
             {"Afterimage", {"Moss", "Mono violet"}}});
        for (Scene &s : pack.scenes) {
            s.palette = s.palette_list.front(); // Its own list, not the look's.
            if (s.name == "Dormant") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.12},
                                                {"colour_spread", 0.3}},
                                               "add", 0.4f, "halo"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 4.0},
                                                {"rate", 1.0},
                                                {"shape", 0.0},
                                                {"spread", 0.0},
                                                {"level", 0.35},
                                                {"colour_spread", 0.3}},
                                               "screen", 0.4f, "halo"));
                s.layers.push_back(
                    layer_by_id("polar_noise",
                                {{"level", 0.3}, {"floor", 0.12}, {"flow", 0.05}, {"clock", 3.0}},
                                "screen", 0.4f, "panels"));
                s.layers.push_back(layer_by_id("corner_sparks",
                                               {{"sites", 1.0},
                                                {"count", 6.0},
                                                {"span", 1.0},
                                                {"decay_s", 0.1},
                                                {"level", 0.9}},
                                               "add", 1.0f, "arms"));
            }
            if (s.name == "First light") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.1},
                                                {"colour_spread", 0.3}},
                                               "add", 0.4f, "halo"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.05},
                                                {"colour_spread", 0.3}},
                                               "add", 0.4f, "panels"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 4.0},
                                                {"rate", 2.0},
                                                {"shape", 4.0},
                                                {"spread", 0.5},
                                                {"width", 0.25},
                                                {"level", 0.8},
                                                {"colour_spread", 0.5},
                                                {"turn_offset", 1.0}},
                                               "screen", 1.0f, "arms"));
            }
            if (s.name == "Nerve") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 3.0},
                                                {"step", 5.0},
                                                {"colours", 2.0},
                                                {"per_16th", 1.5},
                                                {"confidence", 0.3}},
                                               "add", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"radial_from", 1.0},
                                                {"clock", 2.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4}},
                                               "screen", 0.06f, "domes"));
                s.routes.push_back(route_by_id("kick", "opacity", 1, 0.34, "", false));
            }
            if (s.name == "Tide") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 3.0},
                                                {"rate", -1.0},
                                                {"shape", 0.0},
                                                {"spread", 1.0},
                                                {"level", 0.35},
                                                {"colour_spread", 0.5}},
                                               "add", 0.4f, "panels"));
                s.layers.push_back(layer_by_id(
                    "arm_notes", {{"arm_mode", 2.0}, {"colours", 1.0}, {"confidence", 0.3}},
                    "screen", 0.8f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"radial_from", 1.0},
                                                {"clock", 2.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4}},
                                               "screen", 0.06f, "domes"));
                s.routes.push_back(route_by_id("kick", "opacity", 2, 0.34, "", false));
            }
            if (s.name == "Spine") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.1},
                                                {"colour_spread", 0.3}},
                                               "add", 0.4f, "halo"));
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 3.0},
                                                {"step", 1.0},
                                                {"colours", 2.0},
                                                {"trail", 4.0},
                                                {"confidence", 0.3}},
                                               "screen", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 4.0},
                                                {"clock", 6.0},
                                                {"rate", 0.3333333333333333},
                                                {"shape", 4.0},
                                                {"spread", 0.0},
                                                {"width", 0.3},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4},
                                                {"turn_offset", 1.0}},
                                               "screen", 0.06f, "domes"));
                s.routes.push_back(route_by_id("kick", "opacity", 2, 0.34, "", false));
            }
            if (s.name == "Lure") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 3.0},
                                                {"rate", -0.5},
                                                {"shape", 0.0},
                                                {"spread", 0.5},
                                                {"level", 0.6},
                                                {"colour_spread", 0.2}},
                                               "add", 0.4f, "panels"));
                s.layers.push_back(layer_by_id("corner_sparks",
                                               {{"sites", 1.0},
                                                {"count", 6.0},
                                                {"span", 1.0},
                                                {"decay_s", 0.1},
                                                {"level", 0.9}},
                                               "add", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"radial_from", 1.0},
                                                {"clock", 2.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4}},
                                               "screen", 0.06f, "domes"));
                s.routes.push_back(route_by_id("bass_pitch", "hue_shift", 0, 360.0, "", false));
                s.routes.push_back(route_by_id("bass_active", "opacity", 0, 0.2, "", false));
                s.routes.push_back(route_by_id("kick", "opacity", 2, 0.34, "", false));
            }
            if (s.name == "Inhale") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 2.0},
                                                {"rate", -1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 0.6},
                                                {"colour_spread", 0.4}},
                                               "add", 0.4f, "glow"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 1.8},
                                                {"width", 0.01},
                                                {"level", 0.7},
                                                {"colour_spread", 0.3}},
                                               "screen", 1.0f, "arms"));
                s.layers.push_back(
                    layer_by_id("stutter", {{"mode", 5.0}, {"depth", 1.0}}, "multiply", 1.0f, ""));
                s.routes.push_back(route_by_id("tension", "brightness", 0, 0.4, "", false));
                s.routes.push_back(route_by_id("tension", "setting", 0, -0.0625, "rate", false));
                s.routes.push_back(
                    route_by_id("build_progress", "setting", 1, 1.0, "width", false));
            }
            if (s.name == "Swarm") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("sparkle", {{"density", 0.02}, {"decay_s", 0.12}},
                                               "add", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.6},
                                                {"colour_spread", 0.3}},
                                               "add", 0.08f, "domes"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.5},
                                                {"colour_spread", 0.3}},
                                               "add", 0.12f, "halo"));
                s.layers.push_back(
                    layer_by_id("stutter", {{"mode", 5.0}, {"depth", 1.0}}, "multiply", 1.0f, ""));
                s.routes.push_back(route_by_id("tension", "setting", 0, 0.75, "density", false));
                s.routes.push_back(route_by_id("tension", "opacity", 1, 0.32, "", false));
                s.routes.push_back(route_by_id("tension", "opacity", 2, 0.28, "", false));
            }
            if (s.name == "Bloom") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("polar_noise",
                                               {{"level", 0.55},
                                                {"floor", 0.3},
                                                {"flow", 0.25},
                                                {"clock", 2.0},
                                                {"colour_spread", 1.0}},
                                               "add", 0.4f, "glow"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 2.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.3},
                                                {"level", 1.0},
                                                {"colour_spread", 0.6}},
                                               "screen", 0.2f, ""));
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 3.0},
                                                {"step", 1.0},
                                                {"arms_per_note", 3.0},
                                                {"colours", 2.0},
                                                {"confidence", 0.2}},
                                               "screen", 1.0f, "arms"));
                s.routes.push_back(route_by_id("kick", "opacity", 1, 0.8, "", false));
            }
            if (s.name == "Strike") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id(
                    "arm_notes",
                    {{"arm_mode", 2.0}, {"colours", 1.0}, {"per_16th", 2.0}, {"confidence", 0.2}},
                    "add", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"radial_from", 1.0},
                                                {"clock", 2.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4}},
                                               "screen", 0.06f, "domes"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 2.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.4},
                                                {"level", 0.9},
                                                {"colour_spread", 0.5}},
                                               "screen", 0.06f, "rim"));
                s.routes.push_back(route_by_id("kick", "opacity", 1, 0.34, "", false));
                s.routes.push_back(route_by_id("kick", "opacity", 2, 0.34, "", false));
            }
            if (s.name == "Drift") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(
                    layer_by_id("polar_noise",
                                {{"level", 0.45}, {"floor", 0.2}, {"flow", 0.05}, {"clock", 3.0}},
                                "add", 0.4f, "panels"));
                s.layers.push_back(
                    layer_by_id("polar_noise",
                                {{"level", 0.35}, {"floor", 0.15}, {"flow", -0.04}, {"clock", 3.0}},
                                "add", 0.4f, "rim"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 4.0},
                                                {"clock", 3.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 0.0},
                                                {"width", 0.5},
                                                {"level", 0.5},
                                                {"colour_spread", 0.4},
                                                {"turn_offset", 1.0}},
                                               "screen", 0.4f, "domes"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 3.0},
                                                {"rate", 1.0},
                                                {"shape", 0.0},
                                                {"spread", 2.0},
                                                {"level", 0.3},
                                                {"colour_spread", 0.4}},
                                               "screen", 1.0f, "arms"));
            }
            if (s.name == "Held breath") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.2},
                                                {"colour_spread", 0.3}},
                                               "add", 0.4f, "domes"));
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 3.0},
                                                {"step", 5.0},
                                                {"per_16th", 0.4},
                                                {"release", 0.5},
                                                {"colours", 2.0},
                                                {"confidence", 0.3},
                                                {"level", 1.0}},
                                               "screen", 1.0f, "arms"));
            }
            if (s.name == "Afterimage") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.08},
                                                {"colour_spread", 0.3}},
                                               "add", 0.4f, "halo"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.03},
                                                {"colour_spread", 0.3}},
                                               "add", 0.4f, "panels"));
                s.layers.push_back(layer_by_id("corner_sparks",
                                               {{"sites", 1.0},
                                                {"count", 6.0},
                                                {"span", 2.0},
                                                {"keep", 4.0},
                                                {"decay_s", 0.2},
                                                {"level", 1.0}},
                                               "add", 1.0f, "arms"));
                s.post.push_back({AfterglowParams{4.0, 0.85}, true});
            }
        }
        packs.push_back(std::move(pack));
    }
    {
        ShowPack pack = rotating_pack(
            "Cagan abyss",
            "For the Cagan piece in cyan, magenta and UV blue on "
            "black: the arms always tight, the glow always slow, in "
            "every section.",
            {find(looks, "Dormant"), find(looks, "First light"), find(looks, "Nerve"),
             find(looks, "Tide"), find(looks, "Spine"), find(looks, "Lure"), find(looks, "Inhale"),
             find(looks, "Swarm"), find(looks, "Bloom"), find(looks, "Strike"),
             find(looks, "Drift"), find(looks, "Held breath"), find(looks, "Afterimage")},
            {{"Dormant", {"Ocean UV", "Abyss"}},
             {"First light", {"Ocean UV", "Abyss"}},
             {"Nerve", {"Abyss", "UV violet"}},
             {"Tide", {"Abyss", "UV violet"}},
             {"Spine", {"Abyss", "UV violet"}},
             {"Lure", {"Abyss", "UV violet"}},
             {"Inhale", {"UV violet", "Magenta storm"}},
             {"Swarm", {"UV violet", "Magenta storm"}},
             {"Bloom", {"Abyss", "Magenta storm"}},
             {"Strike", {"Abyss", "Magenta storm"}},
             {"Drift", {"Ocean UV", "Abyss"}},
             {"Held breath", {"Ocean UV", "Abyss"}},
             {"Afterimage", {"Ocean UV", "Abyss"}}});
        for (Scene &s : pack.scenes) {
            s.palette = s.palette_list.front(); // Its own list, not the look's.
            if (s.name == "Nerve") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 3.0},
                                                {"step", 5.0},
                                                {"colours", 2.0},
                                                {"per_16th", 1.5},
                                                {"confidence", 0.3},
                                                {"release", 0.5}},
                                               "add", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"radial_from", 1.0},
                                                {"clock", 3.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4}},
                                               "screen", 0.15f, "domes"));
                s.routes.push_back(route_by_id("kick", "opacity", 1, 0.85, "", false));
            }
            if (s.name == "Tide") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 3.0},
                                                {"rate", -1.0},
                                                {"shape", 0.0},
                                                {"spread", 1.0},
                                                {"level", 0.35},
                                                {"colour_spread", 0.5}},
                                               "add", 1.0f, "panels"));
                s.layers.push_back(layer_by_id(
                    "arm_notes",
                    {{"arm_mode", 2.0}, {"colours", 1.0}, {"confidence", 0.3}, {"release", 0.5}},
                    "screen", 0.8f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"radial_from", 1.0},
                                                {"clock", 3.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4}},
                                               "screen", 0.15f, "domes"));
                s.routes.push_back(route_by_id("kick", "opacity", 2, 0.85, "", false));
            }
            if (s.name == "Spine") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.1},
                                                {"colour_spread", 0.3}},
                                               "add", 1.0f, "halo"));
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 3.0},
                                                {"step", 1.0},
                                                {"colours", 2.0},
                                                {"trail", 4.0},
                                                {"confidence", 0.3},
                                                {"release", 0.5}},
                                               "screen", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 4.0},
                                                {"clock", 6.0},
                                                {"rate", 0.3333333333333333},
                                                {"shape", 4.0},
                                                {"spread", 0.0},
                                                {"width", 0.3},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4},
                                                {"turn_offset", 1.0}},
                                               "screen", 0.15f, "domes"));
                s.routes.push_back(route_by_id("kick", "opacity", 2, 0.85, "", false));
            }
            if (s.name == "Lure") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 3.0},
                                                {"rate", -0.5},
                                                {"shape", 0.0},
                                                {"spread", 0.5},
                                                {"level", 0.6},
                                                {"colour_spread", 0.2}},
                                               "add", 1.0f, "panels"));
                s.layers.push_back(layer_by_id("corner_sparks",
                                               {{"sites", 1.0},
                                                {"count", 6.0},
                                                {"span", 1.0},
                                                {"decay_s", 0.1},
                                                {"level", 0.9}},
                                               "add", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"radial_from", 1.0},
                                                {"clock", 3.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4}},
                                               "screen", 0.15f, "domes"));
                s.routes.push_back(route_by_id("bass_pitch", "hue_shift", 0, 360.0, "", false));
                s.routes.push_back(route_by_id("bass_active", "opacity", 0, 0.5, "", false));
                s.routes.push_back(route_by_id("kick", "opacity", 2, 0.85, "", false));
            }
            if (s.name == "Inhale") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 3.0},
                                                {"rate", -1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 0.6},
                                                {"colour_spread", 0.4}},
                                               "add", 1.0f, "glow"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 1.8},
                                                {"width", 0.01},
                                                {"level", 0.7},
                                                {"colour_spread", 0.3}},
                                               "screen", 1.0f, "arms"));
                s.layers.push_back(
                    layer_by_id("stutter", {{"mode", 5.0}, {"depth", 1.0}}, "multiply", 1.0f, ""));
                s.routes.push_back(route_by_id("tension", "brightness", 0, 0.4, "", false));
                s.routes.push_back(route_by_id("tension", "setting", 0, -0.0625, "rate", false));
                s.routes.push_back(
                    route_by_id("build_progress", "setting", 1, 1.0, "width", false));
            }
            if (s.name == "Bloom") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("polar_noise",
                                               {{"level", 0.55},
                                                {"floor", 0.3},
                                                {"flow", 0.25},
                                                {"clock", 3.0},
                                                {"colour_spread", 1.0}},
                                               "add", 1.0f, "glow"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 2.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.3},
                                                {"level", 1.0},
                                                {"colour_spread", 0.6}},
                                               "screen", 0.2f, ""));
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 3.0},
                                                {"step", 1.0},
                                                {"arms_per_note", 3.0},
                                                {"colours", 2.0},
                                                {"confidence", 0.2},
                                                {"release", 0.5}},
                                               "screen", 1.0f, "arms"));
                s.routes.push_back(route_by_id("kick", "opacity", 1, 0.8, "", false));
            }
            if (s.name == "Strike") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 2.0},
                                                {"colours", 1.0},
                                                {"per_16th", 2.0},
                                                {"confidence", 0.2},
                                                {"release", 0.5}},
                                               "add", 1.0f, "arms"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"radial_from", 1.0},
                                                {"clock", 3.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.5},
                                                {"level", 1.0},
                                                {"colour_spread", 0.4}},
                                               "screen", 0.15f, "domes"));
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 3.0},
                                                {"clock", 3.0},
                                                {"rate", 1.0},
                                                {"shape", 4.0},
                                                {"spread", 1.0},
                                                {"width", 0.4},
                                                {"level", 0.9},
                                                {"colour_spread", 0.5}},
                                               "screen", 0.15f, "rim"));
                s.routes.push_back(route_by_id("kick", "opacity", 1, 0.85, "", false));
                s.routes.push_back(route_by_id("kick", "opacity", 2, 0.85, "", false));
            }
            if (s.name == "Held breath") {
                s.layers.clear();
                s.routes.clear();
                s.post.clear();
                s.layers.push_back(layer_by_id("wave",
                                               {{"mapping", 0.0},
                                                {"clock", 0.0},
                                                {"rate", 0.0},
                                                {"shape", 3.0},
                                                {"spread", 0.0},
                                                {"width", 1.0},
                                                {"level", 0.2},
                                                {"colour_spread", 0.3}},
                                               "add", 1.0f, "domes"));
                s.layers.push_back(layer_by_id("arm_notes",
                                               {{"arm_mode", 3.0},
                                                {"step", 5.0},
                                                {"per_16th", 0.4},
                                                {"release", 0.25},
                                                {"colours", 2.0},
                                                {"confidence", 0.3},
                                                {"level", 1.0}},
                                               "screen", 1.0f, "arms"));
            }
        }
        packs.push_back(std::move(pack));
    }
    // END Cagan packs

    ShowPack hexagon = pack_of(
        "Hexagon",
        "For hexagonal rigs: hex tunnels and a radar sweep in grooves, comet rain at high energy, "
        "pressure building through builds as hexagons fall inwards and kicks push back, a "
        "blooming drop, a nebula in breakdowns, hex echoes of the bass and hats in intros and "
        "deep space at the end.",
        {find(looks, "Hex tunnel"), find(looks, "Hex radar"), find(looks, "Comet rain"),
         find(looks, "Nebula"), find(looks, "Hex pressure"), find(looks, "Drop bloom"),
         find(looks, "Hex echoes"), find(looks, "Deep space")});
    hexagon.automation.palette_every_phrases = 2;
    // Here Hex echoes takes the intros; Deep space keeps the outros.
    for (Scene &s : hexagon.scenes) {
        if (s.name == "Deep space") {
            s.sections = {"outro"};
        }
    }
    packs.push_back(std::move(hexagon));

    // Hexagon 2 (docs/PLAN_HEX_VISUALS.md section 11): the hexagon-visuals effects, each sound on
    // its own strips, each scene rotating through its own palettes.
    ShowPack hex2 = pack_of(
        "Hexagon 2",
        "For hexagonal rigs with rings and spokes: the bass note as a clock and as arcs, kicks "
        "running out along the spokes, hats at the corners, automata and a filling phrase in "
        "breakdowns and builds, smoke in intros; each scene keeps its own colours.",
        {find(looks, "Pitch tunnel"), find(looks, "Note rings"), find(looks, "Scanner 2"),
         find(looks, "Forest floor 2"), find(looks, "Pitch meter 2"), find(looks, "Radar 2"),
         find(looks, "Kick pulse 2"), find(looks, "Acid gate 2"), find(looks, "Automaton"),
         find(looks, "Smoke"), find(looks, "Pressure 2"), find(looks, "Riser 2"),
         find(looks, "Bloom 2"), find(looks, "Embers")});
    hex2.automation.palette_every_phrases = 2;
    hex2.automation.palette_rotation = PaletteRotation::scene_list;
    hex2.automation.hold_rule = HoldRule::tolerant;
    // Each scene's palettes (its own first), added to the pack as needed.
    const auto palette_in = [&](const std::string &name) {
        for (size_t i = 0; i < hex2.palettes.size(); ++i) {
            if (hex2.palettes[i].name == name) {
                return static_cast<int>(i);
            }
        }
        hex2.palettes.push_back(palette_named(name));
        return static_cast<int>(hex2.palettes.size()) - 1;
    };
    const std::vector<std::pair<std::string, std::vector<std::string>>> lists = {
        {"Pitch tunnel", {"UV violet", "Magenta storm"}},
        {"Note rings", {"Ocean UV", "UV violet"}},
        {"Automaton", {"Mono violet", "Ocean UV"}}};
    for (Scene &s : hex2.scenes) {
        s.palette_list = {s.palette};
        for (const auto &[name, palettes] : lists) {
            if (s.name == name) {
                s.palette_list.clear();
                for (const std::string &pal : palettes) {
                    s.palette_list.push_back(palette_in(pal));
                }
            }
        }
    }
    // Macros in every scene where the target exists: 1 darkness (inverted, so 0 leaves the scene
    // at full brightness), 2 kick punch, 3 bass size, 4 hue.
    hex2.macro_names = {"Darkness", "Kick punch", "Bass size", "Hue", "", "", "", ""};
    for (Scene &s : hex2.scenes) {
        ModRoute darkness = route(ModSource::macro_1, ModTarget::brightness, 0, 1.0,
                                  ModCurve::linear, "", ModQuantise::off, 0.05, 0.05);
        darkness.invert = true;
        s.routes.push_back(darkness);
        for (size_t k = 0; k < s.layers.size(); ++k) {
            const auto layer = static_cast<int>(k);
            const EffectType type = s.layers[k].spec.type();
            if (type == EffectType::spoke_kick) {
                s.routes.push_back(route(ModSource::macro_2, ModTarget::opacity, layer, 0.5));
            } else if (type == EffectType::note_arcs) {
                s.routes.push_back(route(ModSource::macro_3, ModTarget::setting, layer, 0.5,
                                         ModCurve::linear, "arc_per_16th"));
            } else if (type == EffectType::pitch_clock) {
                s.routes.push_back(route(ModSource::macro_3, ModTarget::setting, layer, 0.3,
                                         ModCurve::linear, "width"));
            }
            if (s.layers[k].blend != BlendMode::multiply) {
                s.routes.push_back(route(ModSource::macro_4, ModTarget::hue_shift, layer, 180.0));
            }
        }
    }
    packs.push_back(std::move(hex2));
    // BEGIN Hexagon 3 packs (generated from hex3_spec.py)
    packs.push_back(
        rotating_pack("Hexagon 3: Tunnel",
                      "The Hexagon pack's tunnel kept and repaired: hexagon rings "
                      "drawn on every beat and kick, the bass on the outer ring "
                      "as a pitch clock, hats at the corners; breakdowns, builds "
                      "and the end never go dark.",
                      {find(looks, "Tunnel 3"), find(looks, "Radar 3"), find(looks, "Ring rain"),
                       find(looks, "Echo field"), find(looks, "Pressure 3"), find(looks, "Bloom 3"),
                       find(looks, "Echo intro"), find(looks, "Deep tunnel")},
                      {{"Tunnel 3", {"UV violet", "Magenta storm"}},
                       {"Radar 3", {"Toxic", "Ocean UV"}},
                       {"Ring rain", {"Magenta storm", "UV violet"}},
                       {"Echo field", {"Ocean UV", "Mono violet"}},
                       {"Pressure 3", {"Ember"}},
                       {"Bloom 3", {"Magenta storm"}},
                       {"Echo intro", {"Ocean UV"}},
                       {"Deep tunnel", {"Mono violet", "Ocean UV"}}}));
    packs.push_back(
        rotating_pack("Hexagon 3: Geometry",
                      "The hexagon as figures: the Necker cube flipping, a "
                      "hexagonal lattice pumping on the kick, slices folding "
                      "round; bass arcs on the outer ring.",
                      {find(looks, "Cube 3"), find(looks, "Lattice"), find(looks, "Star radar"),
                       find(looks, "Facets"), find(looks, "Fold"), find(looks, "Shatter"),
                       find(looks, "Facet intro"), find(looks, "Fading cube")},
                      {{"Cube 3", {"UV violet", "Ocean UV"}},
                       {"Lattice", {"Ocean UV", "Toxic"}},
                       {"Star radar", {"Toxic", "Magenta storm"}},
                       {"Facets", {"Mono violet", "Ocean UV"}},
                       {"Fold", {"Ember"}},
                       {"Shatter", {"Magenta storm"}},
                       {"Facet intro", {"Ocean UV"}},
                       {"Fading cube", {"Mono violet"}}}));
    packs.push_back(
        rotating_pack("Hexagon 3: Pulse",
                      "Rhythm first, each sound in its own place: the kick as "
                      "hexagon rings, the bass note as a clock on the middle "
                      "ring, hats at the corners, the acid line round the "
                      "outside.",
                      {find(looks, "Pulse 3"), find(looks, "Arc pulse"), find(looks, "Hammer"),
                       find(looks, "Breath"), find(looks, "Climb"), find(looks, "Impact 3"),
                       find(looks, "Heartbeat"), find(looks, "Afterglow")},
                      {{"Pulse 3", {"UV violet", "Magenta storm"}},
                       {"Arc pulse", {"Ocean UV", "UV violet"}},
                       {"Hammer", {"Toxic", "Ember"}},
                       {"Breath", {"Ocean UV", "Mono violet"}},
                       {"Climb", {"Ember", "Magenta storm"}},
                       {"Impact 3", {"Magenta storm"}},
                       {"Heartbeat", {"Ocean UV"}},
                       {"Afterglow", {"Mono violet"}}}));
    // END Hexagon 3 packs

    // Hexagon 4: the Hexagon pack's eight scenes as they are, with the owner's picks from Hexagon
    // 2 and 3 alongside, several scenes for each section so long sets rotate among them. Copies
    // in this pack get energy tags (the scenes and the other packs are untouched): an untagged
    // scene only plays when no scene carries the section's energy, so the original Hex tunnel
    // and Hex radar would never play among tagged ones, and Comet rain alone would take every
    // high drop.
    ShowPack hex4 = rotating_pack(
        "Hexagon 4",
        "The Hexagon pack with more to rotate through: hex tunnels, radar sweeps, the cube, the "
        "lattice, the pitch tunnel and note rings in grooves; comet and ring rain, star radar and "
        "the hammer when the energy rises; two blooms for drops, two intros, two outros.",
        {find(looks, "Hex tunnel"),   find(looks, "Hex radar"),    find(looks, "Comet rain"),
         find(looks, "Nebula"),       find(looks, "Hex pressure"), find(looks, "Drop bloom"),
         find(looks, "Hex echoes"),   find(looks, "Deep space"),   find(looks, "Tunnel 3"),
         find(looks, "Pitch tunnel"), find(looks, "Note rings"),   find(looks, "Cube 3"),
         find(looks, "Lattice"),      find(looks, "Radar 3"),      find(looks, "Ring rain"),
         find(looks, "Star radar"),   find(looks, "Hammer"),       find(looks, "Bloom 3"),
         find(looks, "Facet intro"),  find(looks, "Deep tunnel")},
        {{"Tunnel 3", {"UV violet", "Magenta storm"}},
         {"Pitch tunnel", {"UV violet", "Magenta storm"}},
         {"Note rings", {"Ocean UV", "UV violet"}},
         {"Cube 3", {"UV violet", "Ocean UV"}},
         {"Lattice", {"Ocean UV", "Toxic"}},
         {"Radar 3", {"Toxic", "Ocean UV"}},
         {"Ring rain", {"Magenta storm", "UV violet"}},
         {"Star radar", {"Toxic", "Magenta storm"}},
         {"Hammer", {"Toxic", "Ember"}},
         {"Deep tunnel", {"Mono violet", "Ocean UV"}}});
    // The next scene for a section every phrase (the Hexagon pack: every two), so even a
    // two-phrase intro or outro shows both of its scenes and long sections change more often.
    hex4.automation.rotate_every_phrases = 1;
    for (Scene &s : hex4.scenes) {
        if (s.palette_list.empty()) { // The Hexagon pack's scenes keep their own palette.
            s.palette_list = {s.palette};
        }
        if (s.name == "Hex tunnel") {
            s.sections = {"groove", "low", "mid"};
        } else if (s.name == "Hex radar") {
            s.sections = {"groove", "mid", "high"};
        } else if (s.name == "Drop bloom" || s.name == "Bloom 3") {
            s.sections = {"drop", "low", "mid", "high"};
        } else if (s.name == "Deep space") {
            s.sections = {"outro"}; // As in the Hexagon pack: Hex echoes takes the intros.
        }
    }
    packs.push_back(std::move(hex4));
    return packs;
}

} // namespace psyight
