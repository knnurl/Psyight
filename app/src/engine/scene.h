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
// Pattern effects (engine/pattern_effects.cpp), on a mapping of the pixels to 0 to 1 with mirrored
// wings and a clock free or locked to the music (engine/space.h):
// - chase: comets with fading tails running along the mapping, looping or bouncing (a scanner),
//   one palette colour each;
// - wave: a waveform (sine, triangle, ramp, square, bump) across the mapping, `spread` cycles
//   wide, moving with the clock; colour from the palette along the mapping;
// - strobe: full-field flashes on a trigger (the drop's start, a build's last beat, kicks, beats
//   or bars), rolling on 16ths for `flashes`; white or palette. The flash limiter still applies;
// - stutter: a gate for the layers below (multiply blend): open and closed on the 16th, 8th or
//   beat grid, with the bassline's gate, ducking after each kick like a sidechain, or dark for
//   a build's last gap_beats so the drop lands out of darkness;
// - noise: a drifting smooth noise field across the rig (ambient, breakdowns), coloured from the
//   palette and shifted by the residual's brightness;
// - fire: flames rising from the base of the mapping, flickering with noise; they reach higher
//   with the riser through a build;
// - bursts: rings expanding from seeded pixels on each kick, hat, beat, bar or bass note, up to
//   `keep` overlapping (grid triggers and kicks), optionally broken into sparks (fireworks);
// - spiral: arms rotating around the rig's centre with the clock (a pinwheel on a frame, a
//   spiral on a matrix);
// - bass bars: a pitch meter, a marker or a bar at the sounding bass note's pitch along the
//   mapping (low_midi to high_midi), brightness from its velocity and decay like the conveyor.
// Hexagon effects (engine/hex_effects.cpp), on the rig seen as a hexagon (engine/space.h: hex
// distance, so each hexagon ring about the centre is lit at once):
// - hex pulse: hexagons travelling out from the centre (or in from the edge) on each kick, beat,
//   bar, bass note or hat, crossing from centre to edge in travel_beats;
// - cube: the hexagon as a cube seen corner-on: three faces in three shades of one colour and
//   the Y of edges between them, flipping on the beat, bar, two bars, phrase or kick between the
//   cube seen from above and from below (the Necker cube);
// - sectors: six (or 3 to 12) slices round the centre, a chase, alternating halves, a fill or a
//   seeded slice stepping on a clock (the 16ths of a rolling bassline);
// - interference: three waves at 120 degrees summed into a hexagonal lattice of light drifting
//   with a clock, its contrast pumped by each kick.
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

// Mapping and clock values are engine/space.h's Mapping and Clock.
struct ChaseParams {
    int mapping = 0; // Along the strip.
    int wings = 1;
    int clock = 2;      // Beat.
    double rate = 0.25; // Passes per clock division: one a bar.
    int count = 2;      // Comets, evenly spaced.
    double tail = 0.2;  // Of the mapping's length.
    int bounce = 0;     // 1: there and back.
    double level = 1.0;
};

struct WaveParams {
    int mapping = 1; // Across x.
    int wings = 1;
    int clock = 3;       // Bar.
    double rate = 1.0;   // Cycles per clock division.
    int shape = 0;       // Sine, triangle, ramp, square, bump.
    double spread = 1.0; // Cycles across the mapping.
    double width = 0.3;  // Square duty, bump width.
    double level = 0.8;
    double colour_spread = 0.5; // Palette span across the mapping.
};

struct StrobeParams {
    int trigger = 0; // Drop start, build end, kick, beat, bar.
    int flashes = 2; // On successive 16ths after the trigger.
    int every = 1;   // Kicks, beats or bars: every nth.
    double decay_s = 0.05;
    double level = 1.0;
    int colour = 0; // White, palette.
};

struct StutterParams {
    int mode = 0;      // Grid, bass gate, kick duck, pre-drop gap.
    int division = 0;  // 16th, 8th, beat (grid mode).
    double duty = 0.5; // Grid: share open; kick duck: share of a beat to reopen.
    double depth = 1.0;
    double release_s = 0.02; // Closing fade.
    double gap_beats = 1.0;  // Pre-drop gap: dark for the build's last beats.
};

struct NoiseParams {
    double scale_m = 0.7;
    double speed_hz = 0.1; // How fast the field evolves.
    int octaves = 2;
    double contrast = 1.5;
    double level = 0.5;
    double colour_spread = 0.8;
    double brightness_colour = 0.3; // Palette shift with the residual's brightness.
};

struct FireParams {
    int mapping = 2;          // Across y: flames rise from the bottom.
    double height = 0.5;      // Of the mapping the flames reach.
    double riser_boost = 0.6; // Extra height at the riser's top, as a share of `height`.
    double speed_m_per_s = 0.8;
    double scale_m = 0.25;
    double flicker = 0.6;
    double level = 1.0;
    int colours = 0; // Fire, palette.
};

struct BurstsParams {
    int trigger = 0; // Kick, hat, beat, bar, bass note.
    int every = 1;
    int keep = 3; // Overlapping bursts (grid triggers and kicks; the others keep one).
    double speed_m_per_s = 3.0;
    double width_m = 0.25;
    double decay_s = 0.3;
    double sparks = 0.0; // 0: whole rings; towards 1: broken into sparks.
    double level = 0.9;
};

struct SpiralParams {
    int arms = 3;
    double twist = 1.0; // Turns across the radius.
    int clock = 2;      // Beat.
    double rate = 0.25; // Turns per clock division.
    double width = 0.35;
    double level = 0.8;
    double colour_spread = 1.0;
};

struct BassBarsParams {
    int mapping = 1;    // Across x.
    int low_midi = 24;  // C1 at u = 0.
    int high_midi = 48; // C3 at u = 1.
    int mode = 0;       // Marker, bar.
    double width = 0.1; // Marker width, of the mapping.
    double release_s = 0.08;
    double level = 1.0;
    int colours = 0; // Pitch hue (as the conveyor), palette.
};

struct HexPulseParams {
    int trigger = 0; // Kick, beat, bar, bass note, hat.
    int every = 1;
    int direction = 0;         // Out from the centre, in from the edge.
    double travel_beats = 1.0; // From centre to edge.
    double width = 0.12;       // Of the centre-to-edge distance.
    double decay_s = 0.5;
    int keep = 3; // Pulses alive at once (grid triggers and kicks; the others keep one).
    double level = 1.0;
    int colours = 0; // A palette step per pulse, or the palette from centre to edge.
    int corners = 0; // Auto, up and down, sideways (engine/space.h hex_frame).
};

struct CubeParams {
    int flip = 1;               // Beat, bar, two bars, phrase, kick.
    double fade_s = 0.08;       // The crossfade at a flip.
    double shade = 0.85;        // The darkest face: 0 all alike, 1 black.
    double edges = 1.0;         // The Y's brightness (on its own, not times the faces' level).
    double edge_width = 0.04;   // Of the centre-to-edge distance.
    double level = 0.55;        // The faces' brightness: below the edges, so the Y stands out.
    double colour_spread = 0.0; // Palette span over the faces; 0: one colour, shaded.
    int corners = 0;
};

struct SectorsParams {
    int mode = 0;  // Chase, alternate, fill, random.
    int count = 6; // Slices.
    int align = 0; // Centred on the corners, or on the flat sides.
    int clock = 1; // 16th.
    double rate = 1.0;
    double tail = 2.0; // Steps a slice takes to fade.
    double level = 0.9;
    double colour_spread = 1.0; // Palette span round the slices.
    int corners = 0;
};

struct InterferenceParams {
    double scale_m = 0.25; // The lattice's spacing.
    int clock = 0;         // Free.
    double rate = 0.15;    // Drift: lattice spacings per clock division.
    double contrast = 2.0;
    double kick_pump = 0.5; // Extra contrast on each kick, fading over half a beat.
    double level = 0.6;
    double colour_spread = 0.6;
    int corners = 0;
};

// An effect as data: its type (the variant's alternative) and parameters (engine/effect_spec.h
// makes the effect and describes the parameters).
enum class EffectType {
    plasma,
    shockwave,
    conveyor,
    sparkle,
    shimmer,
    build,
    drop_burst,
    chase,
    wave,
    strobe,
    stutter,
    noise,
    fire,
    bursts,
    spiral,
    bass_bars,
    hex_pulse,
    cube,
    sectors,
    interference
};
using EffectParams =
    std::variant<PlasmaParams, ShockwaveParams, ConveyorParams, SparkleParams, ShimmerParams,
                 BuildParams, DropBurstParams, ChaseParams, WaveParams, StrobeParams, StutterParams,
                 NoiseParams, FireParams, BurstsParams, SpiralParams, BassBarsParams,
                 HexPulseParams, CubeParams, SectorsParams, InterferenceParams>;
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
std::unique_ptr<Effect> make_chase(const ChaseParams &params = {});
std::unique_ptr<Effect> make_wave(const WaveParams &params = {});
std::unique_ptr<Effect> make_strobe(const StrobeParams &params = {});
std::unique_ptr<Effect> make_stutter(const StutterParams &params = {});
std::unique_ptr<Effect> make_noise(const NoiseParams &params = {});
std::unique_ptr<Effect> make_fire(const FireParams &params = {});
std::unique_ptr<Effect> make_bursts(const BurstsParams &params = {});
std::unique_ptr<Effect> make_spiral(const SpiralParams &params = {});
std::unique_ptr<Effect> make_bass_bars(const BassBarsParams &params = {});
std::unique_ptr<Effect> make_hex_pulse(const HexPulseParams &params = {});
std::unique_ptr<Effect> make_cube(const CubeParams &params = {});
std::unique_ptr<Effect> make_sectors(const SectorsParams &params = {});
std::unique_ptr<Effect> make_interference(const InterferenceParams &params = {});

// Effects are immutable once made, so copies of a scene (for the show thread) share them. `spec`
// describes `effect` for saving and editing; make_layer (engine/effect_spec.h) keeps them in step,
// and an edit of the spec remakes the effect.
struct Layer {
    std::shared_ptr<const Effect> effect;
    BlendMode blend = BlendMode::add;
    float opacity = 1.0f;
    bool enabled = true;
    EffectSpec spec;
    std::string group; // Group mask (SPEC 7.3): only strips with this tag; empty for all.

    // The effect with this frame's modulated settings (routes with ModTarget::setting on layer
    // `index`, values in route order): `effect` itself when none moves a setting.
    std::shared_ptr<const Effect> effect_for(const std::vector<ModRoute> &routes,
                                             const std::vector<double> &values, size_t index) const;
};

struct Scene {
    std::string name;
    std::vector<Layer> layers;         // Bottom first.
    int palette = 0;                   // Into the palette list.
    std::vector<std::string> sections; // Section labels this scene suits (automation, SPEC 7.6).
    std::vector<ModRoute> routes;      // Modulation (SPEC 7.4).
};

// Renders the layers bottom to top onto black, with the scene's modulation applied. memory: the
// routes' memory (one per route, resized to fit) for holding and smoothing; null renders each
// route's instantaneous value, a pure function of the inputs.
Frame render_scene(const Scene &scene, const Rig &rig, const EngineInputs &inputs,
                   const std::vector<Palette> &palettes, std::uint64_t seed,
                   std::vector<ModRouteState> *memory = nullptr);
// The same with an explicit palette instead of the scene's.
Frame render_scene(const Scene &scene, const Rig &rig, const EngineInputs &inputs,
                   const Palette &palette, std::uint64_t seed,
                   std::vector<ModRouteState> *memory = nullptr);

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
