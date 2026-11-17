# The Cagan packs (docs/PLAN_CAGAN_PACK.md section 6, batch CD) as data: one scene set, the
# owner's picks from cagan_candidates.py (all but Abyss), and three packs that differ in palette
# lists and arc, not in mapping. `python gen_cpp.py cagan` writes them into presets.cpp (then
# clang-format it).
#
# The owner's rule (2026-11-17): lights must not stay on one part of the rig for long; pitch may
# drive colour, not movement. The picks follow it (the arm scenes move by note order, Tide and
# Strike use every arm, the glow zones drift); the arcs below change levels, release and clocks
# only, so they keep it.
#
# Arcs:
# - Cagan ember: the scenes as picked (lush intros and breakdowns, sparse grooves, full drops).
# - Cagan forest: dark and sparse throughout: every layer on a glow zone at 40% (its opacity and
#   the routes that lift it), so the arms carry the show.
# - Cagan abyss: by layer: the arms tight (arm notes released at half the time), the glow slow
#   (glow-zone layers on the beat clock move on the bar instead; the kick still lifts them).
import copy

from cagan_candidates import CANDIDATES

PICKS = ["Dormant", "First light", "Nerve", "Tide", "Spine", "Lure", "Inhale", "Swarm", "Bloom",
         "Strike", "Drift", "Held breath", "Afterimage"]

# Section tags for the packs (energy tags steer grooves; untagged kinds take every scene).
SECTIONS = {
    "Dormant": ["intro", "outro"],
    "First light": ["intro", "outro"],
    "Nerve": ["groove", "low", "mid", "high"],
    "Tide": ["groove", "mid", "high"],
    "Spine": ["groove", "low", "mid"],
    "Lure": ["groove", "low"],
    "Inhale": ["build"],
    "Swarm": ["build"],
    "Bloom": ["drop"],
    "Strike": ["drop"],
    "Drift": ["breakdown"],
    "Held breath": ["break"],
    "Afterimage": ["break"],
}

# New palettes (additions to builtin_palettes).
PALETTES = {
    "Amber": [[1.0, 0.45, 0.0], [1.0, 0.2, 0.0], [0.9, 0.6, 0.1]],
    "Ember tide": [[0.9, 0.1, 0.0], [1.0, 0.4, 0.0], [0.0, 0.45, 0.8]],
    "Moss": [[0.0, 0.5, 0.1], [0.0, 0.6, 0.4], [0.3, 0.0, 0.6]],
    "Fern glow": [[0.1, 0.8, 0.2], [0.0, 0.5, 0.5], [1.0, 0.3, 0.0]],
    "Abyss": [[0.0, 0.8, 1.0], [1.0, 0.0, 0.6], [0.2, 0.0, 1.0]],
}

GLOW_ZONES = {"panels", "rim", "halo", "glow", "domes", "centre"}

# Palette lists per pack, by the scene's role: quiet (intros, outros, breakdowns, breaks),
# groove, build, drop.
ROLE = {"Dormant": "quiet", "First light": "quiet", "Drift": "quiet", "Held breath": "quiet",
        "Afterimage": "quiet", "Nerve": "groove", "Tide": "groove", "Spine": "groove",
        "Lure": "groove", "Inhale": "build", "Swarm": "build", "Bloom": "drop",
        "Strike": "drop"}
PACK_PALETTES = {
    "Cagan ember": {"quiet": ["Ember", "Ember tide"], "groove": ["Ember", "Amber"],
                    "build": ["Amber", "Ember"], "drop": ["Ember", "Ember tide"]},
    "Cagan forest": {"quiet": ["Moss", "Mono violet"], "groove": ["Moss", "Fern glow"],
                     "build": ["Fern glow", "Toxic"], "drop": ["Fern glow", "Moss"]},
    "Cagan abyss": {"quiet": ["Ocean UV", "Abyss"], "groove": ["Abyss", "UV violet"],
                    "build": ["UV violet", "Magenta storm"], "drop": ["Abyss", "Magenta storm"]},
}
DESCRIPTIONS = {
    "Cagan ember": "For the Cagan piece in warm reds and ambers with cool accents: lush glow in "
                   "the intros and breakdowns, the bassline on the arms and the kick on the domes "
                   "in grooves, everything lit in the drops.",
    "Cagan forest": "For the Cagan piece in deep greens, teals and violets with rare hot "
                    "accents: the glow zones held low throughout, the arms carry the show.",
    "Cagan abyss": "For the Cagan piece in cyan, magenta and UV blue on black: the arms always "
                   "tight, the glow always slow, in every section.",
}


# Descriptions for the operator where the candidate's carried review notes.
DESCRIPTION = {
    "Inhale": "Light drawn from the rim into the centre on every beat, faster and brighter as "
              "the tension climbs; the arms filling from the centre outwards through the build; "
              "dark over the pre-drop.",
    "Afterimage": "The halo and panels held dim under a long afterglow, hats sparking on the arm "
                  "tips and leaving trails.",
}


def base_scenes():
    by_name = {s["name"]: s for s in CANDIDATES}
    out = []
    for name in PICKS:
        s = copy.deepcopy(by_name[name])
        s["sections"] = SECTIONS[name]
        s["description"] = DESCRIPTION.get(name, s["description"])
        out.append(s)
    return out


def with_layer(layer, **changes):
    effect, blend, opacity, group, params = layer
    params = dict(params)
    opacity = changes.pop("opacity", opacity)
    params.update(changes)
    return (effect, blend, opacity, group, params)


def forest(s):
    s = copy.deepcopy(s)
    glow = [k for k, l in enumerate(s["layers"]) if l[3] in GLOW_ZONES]
    s["layers"] = [with_layer(l, opacity=round(l[2] * 0.4, 4)) if k in glow else l
                   for k, l in enumerate(s["layers"])]
    s["routes"] = [(src, t, layer, round(a * 0.4, 4) if t == "opacity" and layer in glow else a,
                    prm, inv) for src, t, layer, a, prm, inv in s["routes"]]
    return s


def abyss(s):
    s = copy.deepcopy(s)
    layers = []
    for l in s["layers"]:
        effect, _, _, group, params = l
        if effect == "arm_notes":
            l = with_layer(l, release=params.get("release", 1.0) * 0.5)
        elif group in GLOW_ZONES and effect in ("wave", "polar_noise") and \
                params.get("clock") == 2:
            l = with_layer(l, clock=3)
        layers.append(l)
    s["layers"] = layers
    return s


ARCS = {"Cagan ember": lambda s: s, "Cagan forest": forest, "Cagan abyss": abyss}


def packs():
    """{pack name: {"description", "scenes": [scene with this pack's arc], "lists": {scene:
    [palettes]}}}."""
    out = {}
    for name, arc in ARCS.items():
        scenes = [arc(s) for s in base_scenes()]
        out[name] = {"description": DESCRIPTIONS[name], "scenes": scenes,
                     "lists": {s["name"]: PACK_PALETTES[name][ROLE[s["name"]]] for s in scenes}}
    return out
