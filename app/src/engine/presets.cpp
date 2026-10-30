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

    ShowPack hexagon = pack_of(
        "Hexagon",
        "For hexagonal rigs: hex tunnels and a radar sweep in grooves, comet rain at high energy, "
        "pressure building through builds as hexagons fall inwards and kicks push back, a "
        "blooming drop, a nebula in breakdowns and deep space at the ends.",
        {find(looks, "Hex tunnel"), find(looks, "Hex radar"), find(looks, "Comet rain"),
         find(looks, "Nebula"), find(looks, "Hex pressure"), find(looks, "Drop bloom"),
         find(looks, "Deep space")});
    hexagon.automation.palette_every_phrases = 2;
    packs.push_back(std::move(hexagon));
    return packs;
}

} // namespace psyight
