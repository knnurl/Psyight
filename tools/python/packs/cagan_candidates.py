# Batch CC candidates (docs/PLAN_CAGAN_PACK.md section 5): scenes for the Cagan piece on the rig
# "Cagan flat" (tools/python/packs/cagan_rig.py), as data in hex3_spec.py's form, for the owner to
# pick from. Prototypes only: no pack holds them until the picks (batch CD). Rendered offline by
# cagan_sheets.py.
# Rules: bass on the arms, kick on the domes unless the scene says otherwise; glow zones move on
# bar or phrase clocks, arms move fast; no white; a quiet scene never goes fully dark; grooves stay
# under a quarter of the rig lit above byte 20.
# Wave settings used here: mapping 0 along strip, 3 radial, 4 by strip; clock 0 free, 2 beat,
# 3 bar, 4 phrase, 6 kick; shape 0 sine, 2 ramp, 3 square, 4 bump; turn_offset steps strips round
# the centre; radial_from 1 measures the radial mapping from each strip's own centre (a dome).
from hex3_spec import L, R, scene

# Extra palettes appended to the review project (none yet: the project's own are used).
PALETTES = {}


def glow(group, level, colour_spread=0.3, opacity=1.0):
    """A still, even glow on a group: never fully dark."""
    return L("wave", "add", opacity, group=group, mapping=0, clock=0, rate=0.0, shape=3, spread=0.0, width=1.0,
             level=level, colour_spread=colour_spread)


def dome_kick(opacity=0.15, level=1.0):
    """Each dome thumps from its middle to its edge on the beat (the kick in a groove); the
    layer's opacity rides the kick (route it with kick_route)."""
    return L("wave", "screen", opacity, group="domes", mapping=3, radial_from=1, clock=2, rate=1.0,
             shape=4, spread=1.0, width=0.5, level=level, colour_spread=0.4)


def kick_route(layer, amount=0.85):
    return R("kick", "opacity", layer, amount)


HAT_TIPS = L("corner_sparks", group="arms", sites=1, count=6, span=1, decay_s=0.1, level=0.9)
PREDROP_GAP = L("stutter", "multiply", mode=5, depth=1.0)

INTROS = [
    scene("Dormant", "The halo breathing once a phrase over a dim glow, a faint drift in the "
          "panels, hats as single sparks on the arm tips.", ["intro", "outro"], ["Ember"],
          [glow("halo", 0.12),
           L("wave", "screen", group="halo", mapping=0, clock=4, rate=1.0, shape=0, spread=0.0,
             level=0.35, colour_spread=0.3),
           L("polar_noise", "screen", group="panels", level=0.3, floor=0.12, flow=0.05, clock=3),
           HAT_TIPS]),
    scene("First light", "The arms lighting one after another round the piece, twice a phrase, "
          "the light flowing out along each; domes dark, the halo and panels a dim glow.",
          ["intro", "outro"], ["Ember", "Ocean UV"],
          [glow("halo", 0.1), glow("panels", 0.05),
           L("wave", "screen", group="arms", mapping=3, clock=4, rate=2.0, shape=4, spread=0.5,
             width=0.25, level=0.8, colour_spread=0.5, turn_offset=1.0)]),
]

GROOVES = [
    scene("Nerve", "The bassline on the arms, each note on the next arm five round (jumping "
          "across the piece whatever its pitch), coloured by pitch; the domes thumping on the "
          "kick; everything else dark.", ["groove", "low", "mid"], ["Ember"],
          [L("arm_notes", group="arms", arm_mode=3, step=5, colours=2, per_16th=1.5,
             confidence=0.3),
           dome_kick()],
          [kick_route(1)]),
    scene("Tide", "Every bass note on all twelve arms, a slow radial wave in the panels a bar "
          "long, the kick on the domes; the rim dark.", ["groove", "mid"], ["Ember", "Ocean UV"],
          [L("wave", group="panels", mapping=3, clock=3, rate=-1.0, shape=0, spread=1.0,
             level=0.35, colour_spread=0.5),
           L("arm_notes", "screen", 0.8, group="arms", arm_mode=2, colours=1, confidence=0.3),
           dome_kick()],
          [kick_route(2)]),
    scene("Spine", "The bassline crawling round the arms a note at a time, coloured by pitch, "
          "its last four notes trailing; the kick stepping round the three domes one at a time, "
          "the halo dim.", ["groove", "mid"],
          ["Ember"],
          [glow("halo", 0.1),
           L("arm_notes", "screen", group="arms", arm_mode=3, step=1, colours=2, trail=4,
             confidence=0.3),
           L("wave", "screen", 0.15, group="domes", mapping=4, clock=6, rate=1.0 / 3.0, shape=4,
             spread=0.0, width=0.3, level=1.0, colour_spread=0.4, turn_offset=1.0)],
          [kick_route(2)]),
    scene("Lure", "The glow-led alternative: arms dark but for hats at their tips, the bass "
          "colouring the panels by pitch and lifting them on each note, the kick on the domes.",
          ["groove", "low"], ["Ember", "Ocean UV"],
          [L("wave", group="panels", mapping=3, clock=3, rate=-0.5, shape=0, spread=0.5,
             level=0.6, colour_spread=0.2),
           HAT_TIPS,
           dome_kick()],
          [R("bass_pitch", "hue_shift", 0, 360.0),
           R("bass_active", "opacity", 0, 0.5),
           kick_route(2)]),
]

BUILDS = [
    scene("Inhale", "Light drawn from the rim into the centre on every beat, faster and brighter "
          "as the tension climbs; the arms filling from the centre outwards through the build "
          "(the plan's tips inwards needs a negative spread, which Wave does not take); dark "
          "over the pre-drop.", ["build"], ["Ember", "Magenta storm"],
          [L("wave", group="glow", mapping=3, clock=2, rate=-1.0, shape=4, spread=1.0,
             width=0.5, level=0.6, colour_spread=0.4),
           L("wave", "screen", group="arms", mapping=3, clock=0, rate=0.0, shape=3, spread=1.8,
             width=0.01, level=0.7, colour_spread=0.3),
           PREDROP_GAP],
          [R("tension", "brightness", 0, 0.4),
           R("tension", "setting", 0, -1.0 / 16.0, "rate"),
           R("build_progress", "setting", 1, 1.0, "width")]),
    scene("Swarm", "Sparks on the arms growing denser with the tension, the domes brightening "
          "and the halo rising; dark over the pre-drop.", ["build"], ["Ember", "Toxic"],
          [L("sparkle", group="arms", density=0.02, decay_s=0.12),
           glow("domes", 0.6, opacity=0.2), glow("halo", 0.5, opacity=0.3),
           PREDROP_GAP],
          [R("tension", "setting", 0, 0.75, "density"),
           R("tension", "opacity", 1, 0.8),
           R("tension", "opacity", 2, 0.7)]),
]

DROPS = [
    scene("Bloom", "The kick as a coloured shockwave from the centre across the whole piece, "
          "each bass note on three arms evenly round, moving on a note and coloured by pitch, "
          "panels and rim in full colour.", ["drop"],
          ["Ember", "Magenta storm"],
          [L("polar_noise", group="glow", level=0.55, floor=0.3, flow=0.25, clock=2,
             colour_spread=1.0),
           L("wave", "screen", 0.2, mapping=3, clock=2, rate=1.0, shape=4, spread=1.0,
             width=0.3, level=1.0, colour_spread=0.6),
           L("arm_notes", "screen", group="arms", arm_mode=3, step=1, arms_per_note=3, colours=2,
             confidence=0.2)],
          [kick_route(1, 0.8)]),
    scene("Strike", "The kick on the domes and the rim together, all twelve arms firing "
          "outwards on each bass note, the panels dark for contrast.", ["drop"], ["Ember"],
          [L("arm_notes", group="arms", arm_mode=2, colours=1, per_16th=2.0, confidence=0.2),
           dome_kick(),
           L("wave", "screen", 0.15, group="rim", mapping=3, clock=2, rate=1.0,
             shape=4, spread=1.0, width=0.4, level=0.9, colour_spread=0.5)],
          [kick_route(1), kick_route(2)]),
]

BREAKDOWNS = [
    scene("Drift", "Polar smoke in the panels and rim, the three domes breathing in turn once "
          "a bar, the arms as a slow outward ripple.", ["breakdown", "low"],
          ["Ocean UV", "Ember"],
          [L("polar_noise", group="panels", level=0.45, floor=0.2, flow=0.05, clock=3),
           L("polar_noise", group="rim", level=0.35, floor=0.15, flow=-0.04, clock=3),
           L("wave", "screen", group="domes", mapping=4, clock=3, rate=1.0, shape=4, spread=0.0,
             width=0.5, level=0.5, colour_spread=0.4, turn_offset=1.0),
           L("wave", "screen", group="arms", mapping=3, clock=3, rate=1.0, shape=0, spread=2.0,
             level=0.3, colour_spread=0.4)]),
    scene("Abyss", "Near dark: the halo alone, and single slow pulses travelling out along one "
          "arm at a time, a bar each, with an afterglow.", ["breakdown", "low"],
          ["Ocean UV", "Mono violet"],
          [glow("halo", 0.1),
           L("wave", "screen", group="halo", mapping=0, clock=4, rate=1.0, shape=0, spread=0.0,
             level=0.15),
           L("wave", "screen", group="arms", mapping=3, clock=3, rate=1.0 / 12.0, shape=4,
             spread=0.13, width=0.02, level=0.8, colour_spread=0.3, turn_offset=1.0)],
          post=[("afterglow", {"decay_beats": 2.0, "amount": 0.6})]),
]

BREAKS = [
    scene("Held breath", "The domes hold a dim glow, the arms dark, each bass note a short "
          "flick at the arm roots.", ["break"], ["Ember"],
          [glow("domes", 0.2),
           L("arm_notes", "screen", group="arms", arm_mode=3, step=5, per_16th=0.4, release=0.5,
             colours=2, confidence=0.3, level=1.0)]),
    scene("Afterimage", "The halo and panels held dim under a long afterglow, hats sparking on "
          "the arm tips and leaving trails (a scene's afterglow holds its own frames, not the "
          "last scene's).", ["break"], ["Ember", "Ocean UV"],
          [glow("halo", 0.08), glow("panels", 0.03),
           L("corner_sparks", group="arms", sites=1, count=6, span=2, keep=4, decay_s=0.2,
             level=1.0)],
          post=[("afterglow", {"decay_beats": 4.0, "amount": 0.85})]),
]

CANDIDATES = INTROS + GROOVES + BUILDS + DROPS + BREAKDOWNS + BREAKS
