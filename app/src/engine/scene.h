// Effects, layers and scenes (SPEC sections 7.3 and 7.6). An effect is a deterministic function of
// (pixel position, engine inputs, palette, seed) returning colours; a scene is a stack of layers,
// each an effect with a blend mode and opacity. Rendering is pure: the same rig, inputs, palette
// and seed give the same frame (SPEC 4.2 determinism).
//
// Effect library v0, after the browser prototypes:
// - plasma: two sine fields at different speeds (ambient; brighter outside grooves);
// - shockwave: a white ring from the rig's centre at each kick (white is for kicks, SPEC 7.5);
// - conveyor: one lit segment in every period_segments, stepping one segment per bass note along
//   each strip, hue from pitch (28 degrees per semitone), lit length from gate ratio, brightness
//   from velocity and decay. At most 1 / period_segments of a strip is lit, so neither a step nor
//   a note's attack brightens a quarter of the field at the 16th rate (SPEC 7.2: motion steps,
//   never full-field brightness);
// - sparkle: random pixels flashing on each hat;
// - shimmer: a sparse seeded subset of pixels twinkling smoothly in the rests, from the residual
//   (non-bass) level, hue from its centroid; it fades in after each note rather than switching;
// - build compression: rings contracting toward the centre during builds, speeding up and
//   brightening through the section (the speed ramp is integrated, so the motion never jumps);
// - drop burst: a white disc released from the centre at the start of a drop (white is for
//   kicks and drops, SPEC 7.5; the flash limiter still applies).
#pragma once

#include "engine/colour.h"
#include "engine/inputs.h"
#include "engine/modulation.h"
#include "engine/rig.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace psyight {

struct EffectContext {
    const Rig &rig;
    const EngineInputs &in;
    const Palette &palette;
    std::uint64_t seed = 0;
};

class Effect {
  public:
    virtual ~Effect() = default;
    virtual const char *name() const = 0;
    // Writes one colour per rig pixel into out (out.size() == rig.pixels.size()).
    virtual void render(const EffectContext &context, std::span<Rgb> out) const = 0;
};

struct PlasmaParams {
    double speed_hz = 0.08;      // Field drift.
    double scale_m = 1.4;        // Spatial period.
    double groove_level = 0.15;  // Brightness during grooves and drops.
    double ambient_level = 0.45; // Brightness in intros, breakdowns, builds and outros.
};

struct ShockwaveParams {
    double speed_m_per_s = 6.0;
    double width_m = 0.25;
    double decay_s = 0.25; // Time constant of the ring's brightness.
};

struct ConveyorParams {
    int segment_pixels = 12; // One step along the strip per note.
    int period_segments = 5; // One lit segment in this many.
    double hue_per_semitone_deg = 28.0;
    double hue_offset_deg = 270.0; // Hue of MIDI 0 (C-1); E1 (28) lands at 334, magenta.
    double release_s = 0.03;       // Fade after the note ends.
};

struct SparkleParams {
    double density = 0.04; // Share of pixels lit per hat.
    double decay_s = 0.06;
};

struct ShimmerParams {
    double level = 0.6;
    double rate_hz = 3.0;  // Twinkle rate of each pixel.
    double density = 0.15; // Share of pixels that shimmer.
    double fade_s = 0.03;  // Fade-in time constant after a note ends.
};

struct BuildParams {
    double wavelength_m = 0.5;
    double start_speed_hz = 0.5; // Rings per second passing a point at the build's start.
    double speed_ramp = 3.0;     // Extra speed at the end, as a multiple of the start speed.
    double start_level = 0.15;
    double end_level = 0.75;
};

struct DropBurstParams {
    double speed_m_per_s = 4.0;
    double decay_s = 0.35;
    double duration_s = 1.5; // After the drop starts.
};

// An effect as data: its type (the variant's alternative) and parameters (engine/effect_spec.h
// makes the effect and describes the parameters).
enum class EffectType { plasma, shockwave, conveyor, sparkle, shimmer, build, drop_burst };
using EffectParams = std::variant<PlasmaParams, ShockwaveParams, ConveyorParams, SparkleParams,
                                  ShimmerParams, BuildParams, DropBurstParams>;
struct EffectSpec {
    EffectParams params;
    EffectType type() const { return static_cast<EffectType>(params.index()); }
};

std::unique_ptr<Effect> make_plasma(const PlasmaParams &params = {});
std::unique_ptr<Effect> make_shockwave(const ShockwaveParams &params = {});
std::unique_ptr<Effect> make_conveyor(const ConveyorParams &params = {});
std::unique_ptr<Effect> make_sparkle(const SparkleParams &params = {});
std::unique_ptr<Effect> make_shimmer(const ShimmerParams &params = {});
std::unique_ptr<Effect> make_build(const BuildParams &params = {});
std::unique_ptr<Effect> make_drop_burst(const DropBurstParams &params = {});

// Effects are immutable once made, so copies of a scene (for the show thread) share them. `spec`
// describes `effect` for saving and editing; make_layer (engine/effect_spec.h) keeps them in step,
// and an edit of the spec remakes the effect.
struct Layer {
    std::shared_ptr<const Effect> effect;
    BlendMode blend = BlendMode::add;
    float opacity = 1.0f;
    bool enabled = true;
    EffectSpec spec;
};

struct Scene {
    std::string name;
    std::vector<Layer> layers;         // Bottom first.
    int palette = 0;                   // Into the palette list.
    std::vector<std::string> sections; // Section labels this scene suits (automation, SPEC 7.6).
    std::vector<ModRoute> routes;      // Modulation (SPEC 7.4).
};

// Renders the layers bottom to top onto black, with the scene's modulation applied.
Frame render_scene(const Scene &scene, const Rig &rig, const EngineInputs &inputs,
                   const std::vector<Palette> &palettes, std::uint64_t seed);
// The same with an explicit palette instead of the scene's.
Frame render_scene(const Scene &scene, const Rig &rig, const EngineInputs &inputs,
                   const Palette &palette, std::uint64_t seed);

// Layer edits that keep each route on the same layer: removing a layer removes its routes and
// renumbers the rest; swapping two layers swaps their routes' indices.
void remove_layer(Scene &scene, size_t index);
void swap_layers(Scene &scene, size_t a, size_t b);

// Plasma (alpha), conveyor (add), shimmer (add), sparkle (add), shockwave (screen).
Scene default_scene();

// The default library: Groove and "Groove, staccato" (shorter conveyor segments, the second
// palette) for grooves, Drop (groove plus the burst), Ambient (intro, breakdown, outro), Build.
std::vector<Scene> default_scenes();

} // namespace psyight
