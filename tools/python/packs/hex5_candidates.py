# Batch F candidates (docs/PLAN_SECTIONS_BATCHES.md section 7): four builds, four breakdowns and
# two breaks for the owner to pick from, as data in hex3_spec.py's form. Prototypes only: nothing
# here is in a built-in pack until the owner picks (then Hexagon 5: Hexagon 4 unchanged plus the
# picks). Rendered offline by captures/review/sections/batch_f/candidates.py (not in the repo).
# Rules from the Hexagon 3 round: the rings and outlines carry every scene; detail sits on one ring
# or group; every build has a ring base from its first bar and climbs to its end; quiet scenes
# stay visible; a breakdown never lights over a quarter of the rig above byte 20; no white.
from hex3_spec import L, R, scene

RING_BASE = L("wave", mapping=6, clock=2, rate=-1.0, shape=4, spread=1.0, width=0.3, level=0.6,
              colour_spread=0.3)
PREDROP_GAP = L("stutter", "multiply", mode=5, depth=1.0)  # Pre-drop gap, detected (Batch D).
HATS = L("corner_sparks", opacity=0.6, span=2, count=2, level=0.9)

BUILDS = [
    scene("Tension rings", "The hexagon rings drawn inwards on every beat from the first bar, "
          "brighter as the tension climbs; the phrase fills from the inside; dark over the "
          "pre-drop.", ["build"], ["Ember", "Magenta storm"],
          [RING_BASE,
           L("phrase_fill", order=1, hold_level=0.45, steps=32, level=0.7),
           L("hex_pulse", "screen", 0.8, trigger=1, direction=1, colours=1, width=0.12),
           PREDROP_GAP],
          [R("tension", "brightness", 0, 0.3),
           R("tension", "opacity", 1, 0.6)]),
    scene("Spoke charge", "The ring base on every beat, the spokes charging outwards on each "
          "kick, hexagons falling in; the spokes and rings brighten with the tension.",
          ["build"], ["Ember"],
          [RING_BASE,
           L("spoke_kick", "screen", group="spokes", tail=0.8, travel_beats=1.0),
           L("hex_pulse", "screen", 0.7, trigger=1, direction=1, width=0.1),
           PREDROP_GAP],
          [R("tension", "brightness", 0, 0.3),
           R("tension", "opacity", 1, 0.7)]),
    scene("Rising fire", "Fire climbing the rig with the riser over the ring base, kicks as "
          "hexagons going in; tension lifts both.", ["build"], ["Ember", "Magenta storm"],
          [RING_BASE,
           L("fire", "screen", 0.8, height=0.5, riser_boost=0.6),
           L("hex_pulse", "screen", 0.6, trigger=0, direction=1, width=0.1),
           PREDROP_GAP],
          [R("tension", "brightness", 0, 0.3),
           R("tension", "opacity", 1, 0.5)]),
    scene("Closing sectors", "The ring base, six slices turning on the beats round the outer "
          "ring, the phrase filling side by side; brighter with the tension.", ["build"],
          ["Ember"],
          [RING_BASE,
           L("sectors", "screen", 0.8, group="outer", mode=2, clock=2, count=6, level=0.7),
           L("phrase_fill", order=0, hold_level=0.45, level=0.6),
           PREDROP_GAP],
          [R("tension", "brightness", 0, 0.3),
           R("tension", "opacity", 2, 0.6)]),
]

BREAKDOWNS = [
    scene("Slow orbit", "Smoke drifting round, the bass note as a slow clock hand on the middle "
          "ring with its last notes trailing, a soft glow behind.", ["breakdown", "low"],
          ["Ocean UV", "Mono violet"],
          [L("wave", mapping=6, clock=3, rate=-0.25, shape=4, spread=1.0, width=1.0, level=0.3,
             colour_spread=0.3),
           L("polar_noise", "screen", level=0.8, floor=0.35, flow=-0.06, clock=3),
           L("pitch_clock", "screen", group="middle", width=0.1, trail=3, confidence=0.2,
             release_s=0.4)],
          post=[("afterglow", {"decay_beats": 1.0, "amount": 0.5})]),
    scene("Ring tide", "The three rings breathing in turn once a bar, hats sparking at the "
          "corners.", ["breakdown", "low"], ["Ocean UV", "UV violet"],
          [L("wave", mapping=6, clock=3, rate=-0.5, shape=4, spread=1.0, width=0.4, level=0.5,
             colour_spread=0.4),
           HATS]),
    scene("Slow facets", "The cube turning once a phrase over a glowing automaton on the inner "
          "ring.", ["breakdown", "low"], ["Mono violet", "Ocean UV"],
          [L("cube", flip=3, level=0.4, edges=0.8, fade_s=1.5),
           L("ring_automaton", opacity=0.7, group="inner", rule=150, generations=16, level=0.6)],
          post=[("afterglow", {"decay_beats": 2.0, "amount": 0.5})]),
    scene("Ember drift", "Low smoke, a hexagon drawn slowly inwards each bar, each bass note a "
          "small arc on the outer ring.", ["breakdown", "low"], ["Ember", "Mono violet"],
          [L("polar_noise", "alpha", level=0.5, floor=0.2, flow=0.04, clock=3),
           L("hex_pulse", "screen", trigger=2, direction=1, travel_beats=4.0, width=0.15,
             decay_s=2.0, keep=1, level=0.7),
           L("note_arcs", "screen", group="outer", ring_mode=2, arc_per_16th=0.15,
             confidence=0.2, release_s=0.4)]),
]

BREAKS = [
    scene("Held outline", "For a short break between grooves: the rings held as a dim outline, "
          "each bass note a hexagon from the inner ring, the spokes dark.", ["break"],
          ["UV violet", "Ocean UV"],
          [L("wave", mapping=6, clock=1, rate=0.0, shape=0, spread=0.0, width=1.0, level=0.25,
             colour_spread=0.3),
           L("hex_pulse", "screen", trigger=3, keep=3, width=0.08, decay_s=0.5, level=0.7)]),
    scene("Stop sparks", "For a short break: dark but for hats sparking at the corners and the "
          "outer ring glowing out from the last kick.", ["break"], ["Magenta storm"],
          [L("wave", group="outer", mapping=6, clock=3, rate=-0.25, shape=4, spread=1.0,
             width=1.0, level=0.4),
           L("corner_sparks", span=2, count=3, level=1.0)],
          post=[("afterglow", {"decay_beats": 2.0, "amount": 0.7})]),
]

CANDIDATES = BUILDS + BREAKDOWNS + BREAKS
