"""Writes the rig preset "Cagan flat" from "Cagan 3D" (docs/PLAN_CAGAN_PACK.md section 2).

    python cagan_rig.py "<rigs folder>/Cagan 3D.json"

Reads the given file, never writes it, and writes "Cagan flat.json" beside it:
- z scaled by 0.2, so the 25 cm cone becomes 5 cm deep with the layer order kept [Verify: real
  layer depth];
- three domes appended, one strip each: an inner ring of 12 pixels and an outer ring of 24 at
  60 pixels a metre, centred on the three glow wedges, 1 cm behind the back layer, joined by a
  short zigzag the rig's joint rule cuts out (a wire) [Verify: counts, radii, wiring, lobes];
- groups by voice and light path added to the existing ones (arms, arms_inner, arms_outer, domes,
  dome_1 to dome_3, panels, rim, halo, centre, sharp, glow).
Every strip's count is kept as given ("fills": false), and a strip's pitch shrinks with its path
(pixels a metre scaled by its 3D length over its flat length), so each pixel keeps its place seen
from the front; the pistil, standing straight out, becomes a dense 6 cm stub.
"""
import json
import math
import os
import sys

Z_SCALE = 0.2
DOME_PPM = 60.0
DOME_INNER = 12
DOME_OUTER = 24
DOME_Z_M = -0.01
SEGMENTS = 36  # Polygon sides of a ring: 10 degree turns, under the joint rule's 20.
CLEARANCE_M = 0.005  # kJointClearanceM: cut each side of a sharp corner.


def ring_radius(pixels, ends_cut):
    """Radius whose ring holds `pixels` exactly at DOME_PPM, half a pitch spare, given how many of
    its ends lose CLEARANCE_M to a corner."""
    perimeter = (pixels + 0.5) / DOME_PPM + ends_cut * CLEARANCE_M
    return perimeter / (SEGMENTS * 2.0 * math.sin(math.pi / SEGMENTS))


def ring(cx, cy, r, a0):
    return [[cx + r * math.sin(a0 + 2 * math.pi * k / SEGMENTS),
             cy + r * math.cos(a0 + 2 * math.pi * k / SEGMENTS), DOME_Z_M]
            for k in range(SEGMENTS + 1)]


def dome(name, groups, cx, cy, a0):
    r_in = ring_radius(DOME_INNER, 1)
    r_out = ring_radius(DOME_OUTER, 1)
    inner = ring(cx, cy, r_in, a0)
    outer = ring(cx, cy, r_out, a0)
    # Zigzag out of the plane, corners under a centimetre apart: all of it within the clearance
    # of a corner, so it holds no pixel.
    steps = max(2, math.ceil((r_out - r_in) / 0.008))
    hop = []
    for k in range(1, steps):
        r = r_in + (r_out - r_in) * k / steps
        hop.append([cx + r * math.sin(a0), cy + r * math.cos(a0),
                    DOME_Z_M + (0.004 if k % 2 else -0.004)])
    return {"name": name, "polyline": inner + hop + outer, "pixels_per_m": DOME_PPM,
            "count": DOME_INNER + DOME_OUTER, "fills": False, "groups": groups}


def length(polyline):
    return sum(math.dist(polyline[i], polyline[i + 1]) for i in range(len(polyline) - 1))


def added_groups(strip):
    name = strip["name"]
    if name.startswith("OMURGALAR"):
        # The six leaving the centre start there; the six to the rim end there.
        x0, y0, _ = strip["polyline"][0]
        x1, y1, _ = strip["polyline"][-1]
        from_centre = math.hypot(x0, y0 - CENTRE[1]) < math.hypot(x1, y1 - CENTRE[1])
        return ["arms", "arms_inner" if from_centre else "arms_outer", "sharp"]
    if name.startswith("KENAR_CIZGILER"):
        return ["rim" if strip["count"] == 106 else "panels", "glow"]
    if name == "Base ring":
        return ["halo", "glow"]
    if name == "Pistil":
        return ["centre", "glow"]
    raise SystemExit(f"unexpected strip {name}: the generator does not know it")


CENTRE = (0.0, 1.602)


def main():
    src = sys.argv[1]
    rig = json.load(open(src, encoding="utf-8"))
    strips = []
    for s in rig["strips"]:
        t = dict(s)
        t["polyline"] = [[x, y, z * Z_SCALE] for x, y, z in s["polyline"]]
        # A strip that climbed the cone is shorter flat; the pitch shrinks with it so each pixel
        # keeps its place along the strip, and so its place seen from the front.
        t["pixels_per_m"] = s["pixels_per_m"] * length(s["polyline"]) / length(t["polyline"])
        t["groups"] = list(s["groups"]) + [g for g in added_groups(s) if g not in s["groups"]]
        t["fills"] = False
        strips.append(t)
    # Domes on the wedges: KENAR_CIZGILER 16 to 18 at the top, 10 to 12 lower right, 13 to 15
    # lower left; each dome at its wedge's centroid, numbered clockwise from the top.
    by_name = {s["name"]: s for s in rig["strips"]}
    for k, (first, side) in enumerate([(16, ["left", "right"]), (10, ["right"]), (13, ["left"])]):
        corners = [by_name[f"KENAR_CIZGILER {first + j}"]["polyline"][0] for j in range(3)]
        cx = sum(c[0] for c in corners) / 3
        cy = sum(c[1] for c in corners) / 3
        a0 = math.atan2(cx - CENTRE[0], cy - CENTRE[1])
        strips.append(dome(f"Dome {k + 1}", ["domes", f"dome_{k + 1}", "glow"] + side, cx, cy, a0))
    total = sum(s["count"] for s in strips)
    out = {"description": f"{len(strips)} strips, {total} pixels: Cagan flat, the cone pressed to "
                          "5 cm, three domes on the wedges (placeholders), groups by voice and "
                          "light path. Generated from Cagan 3D by tools/python/packs/cagan_rig.py.",
           "format": "psyight rig preset", "name": "Cagan flat", "schema": rig["schema"],
           "strips": strips}
    dst = os.path.join(os.path.dirname(os.path.abspath(src)), "Cagan flat.json")
    if os.path.abspath(dst) == os.path.abspath(src):
        raise SystemExit("refusing to overwrite the source")
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    print(f"wrote {dst}: {len(strips)} strips, {total} pixels")


if __name__ == "__main__":
    main()
