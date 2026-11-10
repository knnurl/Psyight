# The Hexagon 3 packs as data: one source for the prototype projects (rendered by proto.py) and
# the C++ built-in presets (gen_cpp.py). A layer: (effect id, blend, opacity, group, {param: value}).
# A route: (source, target, layer, amount, param, invert). Missing params take the effect's default.

L = lambda effect, blend="add", opacity=1.0, group="", **params: (effect, blend, opacity, group, params)
R = lambda source, target, layer, amount, param="", invert=False: (source, target, layer, amount, param, invert)

# Recurring parts.
KICK_RINGS = L("hex_pulse", "screen", trigger=0, travel_beats=1.0, width=0.12, decay_s=0.5, keep=3)
TUNNEL_WAVE = L("wave", mapping=6, clock=2, rate=-1.0, shape=4, spread=1.5, width=0.25, level=0.35,
                colour_spread=0.5)
BASS_SECTORS = L("sectors", opacity=0.7, clock=5, count=6, tail=3.0, level=0.5, colour_spread=1.0)
HATS = L("corner_sparks", opacity=0.6, span=2, count=2, level=0.9)
GAP = L("stutter", "multiply", mode=4, depth=1.0)
BUILD_BASE = L("wave", mapping=6, clock=2, rate=-1.0, shape=4, spread=1.0, width=0.3, level=0.45,
               colour_spread=0.3)


def scene(name, desc, sections, palettes, layers, routes=(), post=()):
    return {"name": name, "description": desc, "sections": sections, "palettes": palettes,
            "layers": list(layers), "routes": list(routes), "post": list(post)}


TUNNEL = scene  # Readability below.

PACKS = {
    "Hexagon 3: Tunnel": {
        "description": "The Hexagon pack's tunnel kept and repaired: hexagon rings drawn on every "
                       "beat and kick, the bass on the outer ring as a pitch clock, hats at the "
                       "corners; breakdowns, builds and the end never go dark.",
        "scenes": [
            scene("Tunnel 3", "Hexagons drawn inwards on the beat, the kick's hexagon pushing out "
                  "from the inner ring, sectors stepping with the bass, the bass note round the "
                  "outer ring.", ["groove", "low", "mid"], ["UV violet", "Magenta storm"],
                  [TUNNEL_WAVE, BASS_SECTORS, KICK_RINGS,
                   L("pitch_clock", group="outer", width=0.07, confidence=0.2, release_s=0.15,
                     spokes=0)]),
            scene("Radar 3", "Twelve slices stepping round with the bass, a hexagon closing in "
                  "each bar, ducking after each kick; the kick itself runs out along the spokes.",
                  ["groove", "mid"], ["Toxic", "Ocean UV"],
                  [L("sectors", clock=5, count=12, tail=6.0, level=0.9, colour_spread=0.3),
                   L("hex_pulse", "screen", trigger=2, direction=1, travel_beats=4.0, decay_s=2.0,
                     keep=1, level=0.7, width=0.1),
                   L("stutter", "multiply", mode=2, depth=0.6),
                   L("spoke_kick", "screen", group="spokes", tail=0.8, travel_beats=1.0)]),
            scene("Ring rain", "Every bass note a hexagon going out, kicks as wide bright "
                  "hexagons, hats sparking at the corners.", ["groove", "high"],
                  ["Magenta storm", "UV violet"],
                  [L("hex_pulse", trigger=3, keep=4, travel_beats=1.0, width=0.08, decay_s=0.4,
                     level=0.7),
                   L("hex_pulse", "screen", trigger=0, width=0.15, decay_s=0.4, keep=2),
                   HATS]),
            scene("Echo field", "Polar smoke drifting in, each bass note a hexagon, hats at the "
                  "corners: dim but never dark.", ["breakdown", "low"], ["Ocean UV", "Mono violet"],
                  [L("polar_noise", "alpha", level=0.7, floor=0.25, flow=-0.1, clock=3),
                   L("hex_pulse", "screen", trigger=3, keep=4, travel_beats=1.0, width=0.08,
                     decay_s=0.6, level=0.6),
                   HATS]),
            scene("Pressure 3", "The rings fill from the inside through the phrase, hexagons "
                  "falling in on the beats, kicks pushing out; brighter as the whole build "
                  "climbs, dark only for its last beat.", ["build"], ["Ember"],
                  [BUILD_BASE,
                   L("phrase_fill", order=1, hold_level=0.45, steps=32, level=0.7),
                   L("hex_pulse", "screen", 0.8, trigger=1, direction=1, colours=1, width=0.12),
                   L("hex_pulse", "screen", trigger=0, width=0.08, travel_beats=0.5, decay_s=0.25),
                   GAP],
                  [R("build_progress", "brightness", 0, 0.25),
                   R("riser", "opacity", 2, 0.4)]),
            scene("Bloom 3", "The drop's burst and white flashes, then hexagon rings pumping on "
                  "the 16ths and a wide hexagon on every kick.", ["drop"], ["Magenta storm"],
                  [L("wave", mapping=6, clock=1, rate=0.25, shape=3, spread=2.0, width=0.3,
                     level=0.4, colour_spread=0.5),
                   L("hex_pulse", "screen", trigger=0, width=0.15, decay_s=0.4, keep=2),
                   L("drop_burst", "screen"), L("strobe", "screen")]),
            scene("Echo intro", "Faint smoke; each bass note sends a hexagon out from the inner "
                  "ring; hats at the corners.", ["intro"], ["Ocean UV"],
                  [L("polar_noise", "alpha", level=0.6, floor=0.2, flow=0.05, clock=3),
                   L("hex_pulse", "screen", trigger=3, keep=4, travel_beats=1.0, width=0.08,
                     decay_s=0.6, level=0.6),
                   HATS]),
            scene("Deep tunnel", "Slow hexagons drawn inwards each bar over a cellular "
                  "automaton, with an afterglow.", ["outro"], ["Mono violet", "Ocean UV"],
                  [L("polar_noise", "alpha", level=0.5, floor=0.2, flow=-0.05, clock=3),
                   L("wave", mapping=6, clock=3, rate=-0.5, shape=4, spread=1.0, width=0.3,
                     level=0.9),
                   L("ring_automaton", opacity=0.8, rule=150, generations=16, level=0.8)],
                  post=[("afterglow", {"decay_beats": 1.5, "amount": 0.7})]),
        ],
    },
    "Hexagon 3: Geometry": {
        "description": "The hexagon as figures: the Necker cube flipping, a hexagonal lattice "
                       "pumping on the kick, slices folding round; bass arcs on the outer ring.",
        "scenes": [
            scene("Cube 3", "The hexagon as a cube flipping every bar, a hexagon on every kick, "
                  "each bass note an arc round the outer ring.", ["groove", "low", "mid"],
                  ["UV violet", "Ocean UV"],
                  [L("cube", flip=1, level=0.4, edges=0.9, fade_s=0.06),
                   KICK_RINGS,
                   L("note_arcs", group="outer", ring_mode=2, arc_per_16th=0.25, confidence=0.2)]),
            scene("Lattice", "A hexagonal lattice drifting and pumping on each kick, sectors "
                  "stepping with the bass, kicks along the spokes.", ["groove", "mid"],
                  ["Ocean UV", "Toxic"],
                  [L("interference", level=0.6, kick_pump=1.0, rate=0.25, clock=2, scale_m=0.6),
                   L("sectors", opacity=0.6, clock=5, count=6, tail=2.0, level=0.6),
                   L("spoke_kick", "screen", group="spokes", tail=0.8, travel_beats=1.0)]),
            scene("Star radar", "Twelve slices stepping with the bass, the cube's Y flashing on "
                  "the kick, the bass note round the middle ring.", ["groove", "high"],
                  ["Toxic", "Magenta storm"],
                  [L("sectors", clock=5, count=12, tail=4.0, level=0.8, colour_spread=0.4),
                   L("cube", "screen", 0.8, flip=4, level=0.0, edges=1.0, fade_s=0.0),
                   L("stutter", "multiply", mode=2, depth=0.5),
                   L("pitch_clock", group="middle", width=0.08, confidence=0.2, spokes=0)]),
            scene("Facets", "The cube turning once a phrase, slowly, rule 90 growing round the "
                  "rings, hats at the corners.", ["breakdown", "low"], ["Mono violet", "Ocean UV"],
                  [L("cube", flip=3, level=0.5, edges=0.9, fade_s=1.0),
                   L("ring_automaton", opacity=0.8, rule=90, generations=32, level=0.6),
                   HATS]),
            scene("Fold", "Slices filling round on the beats, hexagons falling inwards, the "
                  "phrase filling side by side; dark for the build's last beat.", ["build"],
                  ["Ember"],
                  [BUILD_BASE,
                   L("phrase_fill", order=0, hold_level=0.45, level=0.7),
                   L("sectors", "add", 0.6, mode=2, clock=2, count=6, level=0.6),
                   L("hex_pulse", "screen", 0.7, trigger=1, direction=1, width=0.12),
                   GAP],
                  [R("build_progress", "brightness", 0, 0.25)]),
            scene("Shatter", "The drop's burst and flashes, the cube flipping on every kick, "
                  "white kicks flashing each ring.", ["drop"], ["Magenta storm"],
                  [L("cube", flip=4, level=0.45, edges=1.0, fade_s=0.03),
                   L("spoke_kick", "screen", colours=1, rings=1, tail=0.8),
                   L("drop_burst", "screen"), L("strobe", "screen")]),
            scene("Facet intro", "A faint lattice drifting; each bass note a hexagon from the "
                  "inner ring.", ["intro"], ["Ocean UV"],
                  [L("interference", level=0.75, kick_pump=0.0, rate=0.1, clock=3, scale_m=0.6),
                   L("hex_pulse", "screen", trigger=3, keep=4, width=0.08, decay_s=0.6,
                     level=0.6)]),
            scene("Fading cube", "The cube turning once a phrase, glowing out.", ["outro"],
                  ["Mono violet"],
                  [L("cube", flip=3, level=0.55, edges=0.9, fade_s=2.0)],
                  post=[("afterglow", {"decay_beats": 2.0, "amount": 0.6})]),
        ],
    },
    "Hexagon 3: Pulse": {
        "description": "Rhythm first, each sound in its own place: the kick as hexagon rings, "
                       "the bass note as a clock on the middle ring, hats at the corners, the "
                       "acid line round the outside.",
        "scenes": [
            scene("Pulse 3", "Kick hexagons from the inner ring out, the bass note as a clock "
                  "hand on the middle ring and its spoke, hats sparking at the inner corners.",
                  ["groove", "low", "mid"], ["UV violet", "Magenta storm"],
                  [L("hex_pulse", trigger=0, keep=2, width=0.14, decay_s=0.45),
                   L("pitch_clock", "screen", group="middle", width=0.1, confidence=0.2,
                     release_s=0.2),
                   L("corner_sparks", opacity=0.7, group="inner", span=2, count=3, level=0.9)]),
            scene("Arc pulse", "Kick hexagons, each bass note an arc on the outer ring with the "
                  "last three still showing.", ["groove", "mid"], ["Ocean UV", "UV violet"],
                  [L("hex_pulse", trigger=0, keep=2, width=0.14, decay_s=0.45),
                   L("note_arcs", "screen", group="outer", ring_mode=2, history=3,
                     arc_per_16th=0.2, confidence=0.2, release_s=0.3)]),
            scene("Hammer", "Wide kick hexagons, ramps racing round the outer ring on the 16ths "
                  "gated by the bassline, kicks flashing along the spokes.", ["groove", "high"],
                  ["Toxic", "Ember"],
                  [L("hex_pulse", trigger=0, keep=2, width=0.2, decay_s=0.35),
                   L("wave", group="outer", mapping=7, wings=2, clock=1, rate=0.25, shape=2,
                     spread=3.0, level=0.9),
                   L("stutter", "multiply", group="outer", mode=1, release_s=0.03),
                   L("spoke_kick", "screen", group="spokes", tail=0.8)],
                  [R("residual_brightness", "hue_shift", 1, 90.0)]),
            scene("Breath", "Smoke breathing, the bass note on the middle ring with its last "
                  "three notes trailing, a hexagon per bass note.", ["breakdown", "low"],
                  ["Ocean UV", "Mono violet"],
                  [L("polar_noise", "alpha", level=0.7, floor=0.25, flow=-0.08, clock=3),
                   L("pitch_clock", "screen", group="middle", width=0.1, trail=3,
                     confidence=0.2, release_s=0.3),
                   L("hex_pulse", "screen", 0.6, trigger=3, keep=3, width=0.08, decay_s=0.5)]),
            scene("Climb", "The rings filling from the inside, hexagons falling in on the beats, "
                  "kicks along the spokes, a strobe roll on the spokes at the end, then dark.",
                  ["build"], ["Ember", "Magenta storm"],
                  [BUILD_BASE,
                   L("phrase_fill", order=1, hold_level=0.45, level=0.7),
                   L("hex_pulse", "screen", 0.8, trigger=1, direction=1, width=0.12),
                   L("spoke_kick", "screen", group="spokes", tail=0.8),
                   L("strobe", "screen", group="spokes", trigger=1, flashes=4, colour=1,
                     level=0.8),
                   GAP],
                  [R("build_progress", "brightness", 0, 0.25)]),
            scene("Impact 3", "The drop's burst and flashes, white kick hexagons flashing each "
                  "ring as they pass, bass arcs on the outer ring.", ["drop"], ["Magenta storm"],
                  [L("note_arcs", group="outer", ring_mode=2, arc_per_16th=0.25, confidence=0.2),
                   L("spoke_kick", "screen", colours=1, rings=1, tail=0.8),
                   L("hex_pulse", "screen", trigger=0, width=0.15, decay_s=0.35, keep=2),
                   L("drop_burst", "screen"), L("strobe", "screen")]),
            scene("Heartbeat", "Faint smoke, each bass note a hexagon going out.", ["intro"],
                  ["Ocean UV"],
                  [L("polar_noise", "alpha", level=0.6, floor=0.2, flow=0.05, clock=3),
                   L("hex_pulse", "screen", trigger=3, keep=4, width=0.1, decay_s=0.6,
                     level=0.7)]),
            scene("Afterglow", "A hexagon each bar, slowly, over a glowing automaton.",
                  ["outro"], ["Mono violet"],
                  [L("polar_noise", "alpha", level=0.5, floor=0.2, flow=-0.05, clock=3),
                   L("hex_pulse", "screen", trigger=2, travel_beats=4.0, width=0.2, decay_s=3.0,
                     keep=2, level=0.9),
                   L("ring_automaton", opacity=0.8, rule=150, generations=16, level=0.8)],
                  post=[("afterglow", {"decay_beats": 2.0, "amount": 0.8})]),
        ],
    },
}
