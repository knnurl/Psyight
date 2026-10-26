"""Fusion script: Psyight's build kit assembly DXF as a stacked 3D model.

Reads the one-DXF build kit (Rig > Export drawing > Build kit, one DXF) and builds it from the
layer names, which carry their heights in mm (2_spacer_z6-15mm_cut; 4p5 means 4.5):
- each part's cut layer becomes a component with a sketch at its lower height and a body
  extruded through its sheet, holes left open (regions nested an odd number of times);
- each part's engrave layer becomes a sketch on the part's top face;
- the rig layers (0_centre_lines_z6mm, 0_pixels_z6mm and so on) become sketches at their height,
  wiring labels as sketch text.
Bodies get a wood or a translucent plastic appearance when Fusion's library has one.

Reads Psyight's own R12 output only (POLYLINE with VERTEX, CIRCLE, TEXT), not DXF in general.
Install: Utilities > Add-Ins > Scripts and Add-Ins > + (My Scripts), pick this folder, then Run.
"""

import math
import re

LAYER = re.compile(r"^(\d+)_([a-z_]+?)_z([0-9p]+)(?:-([0-9p]+))?mm(?:_(cut|engrave))?$")


def millimetres(tag):
    return float(tag.replace("p", "."))


def parse_layer_name(name):
    """(index, part, z_low_mm, z_high_mm, kind) for a build kit layer, or None.

    kind is "cut", "engrave" or "rig" (the 0_ layers, at one height: z_low == z_high)."""
    m = LAYER.match(name)
    if not m:
        return None
    index, part, low, high, kind = m.groups()
    z_low = millimetres(low)
    z_high = millimetres(high) if high else z_low
    return int(index), part, z_low, z_high, kind or "rig"


def read_dxf(path):
    """Each layer's shapes: {"lines": [(points, closed)], "circles": [(x, y, r)],
    "texts": [(x, y, height, text)]}, in mm, from Psyight's R12 DXF."""
    with open(path, encoding="utf-8", errors="replace") as f:
        raw = f.read().splitlines()
    pairs = [(raw[i].strip(), raw[i + 1].rstrip("\r")) for i in range(0, len(raw) - 1, 2)]
    layers = {}

    def layer(name):
        return layers.setdefault(name, {"lines": [], "circles": [], "texts": []})

    i = 0
    in_entities = False
    while i < len(pairs):
        code, value = pairs[i]
        if code == "2" and value == "ENTITIES":
            in_entities = True
        if not in_entities or code != "0":
            i += 1
            continue
        if value == "POLYLINE":
            fields = {}
            i += 1
            while pairs[i][0] != "0":
                fields[pairs[i][0]] = pairs[i][1]
                i += 1
            points = []
            while pairs[i] == ("0", "VERTEX"):
                i += 1
                x = y = 0.0
                while pairs[i][0] != "0":
                    if pairs[i][0] == "10":
                        x = float(pairs[i][1])
                    elif pairs[i][0] == "20":
                        y = float(pairs[i][1])
                    i += 1
                points.append((x, y))
            closed = int(fields.get("70", "0")) & 1 == 1
            if len(points) >= 2:
                layer(fields.get("8", "0"))["lines"].append((points, closed))
        elif value in ("CIRCLE", "TEXT"):
            fields = {}
            i += 1
            while pairs[i][0] != "0":
                fields[pairs[i][0]] = pairs[i][1]
                i += 1
            x, y = float(fields.get("10", 0)), float(fields.get("20", 0))
            if value == "CIRCLE":
                layer(fields.get("8", "0"))["circles"].append((x, y, float(fields.get("40", 0))))
            else:
                layer(fields.get("8", "0"))["texts"].append(
                    (x, y, float(fields.get("40", 4)), fields.get("1", "")))
        else:
            i += 1
    return layers


# Fusion from here on (its API works in centimetres).


def find_appearance(app, words):
    """The first appearance in Fusion's libraries whose name holds all of `words`, or None."""
    for library in app.materialLibraries:
        try:
            for appearance in library.appearances:
                name = appearance.name.lower()
                if all(w in name for w in words):
                    return appearance
        except Exception:
            continue
    return None


def draw(sketch, shapes, adsk):
    """The layer's polylines, circles and texts into the sketch (mm to cm)."""
    P = adsk.core.Point3D.create
    lines = sketch.sketchCurves.sketchLines
    for points, closed in shapes["lines"]:
        if closed and points[0] != points[-1]:
            points = points + [points[0]]
        first = None
        previous = None
        for k in range(1, len(points)):
            a, b = points[k - 1], points[k]
            if math.hypot(b[0] - a[0], b[1] - a[1]) < 1e-6:
                continue
            start = previous.endSketchPoint if previous else P(a[0] / 10, a[1] / 10, 0)
            if closed and k == len(points) - 1 and first is not None:
                previous = lines.addByTwoPoints(start, first.startSketchPoint)
            else:
                previous = lines.addByTwoPoints(start, P(b[0] / 10, b[1] / 10, 0))
            if first is None:
                first = previous
    for x, y, r in shapes["circles"]:
        sketch.sketchCurves.sketchCircles.addByCenterRadius(P(x / 10, y / 10, 0), r / 10)
    for x, y, height, text in shapes["texts"]:
        try:  # [Verify] the text API differs between Fusion versions; labels are optional.
            made = sketch.sketchTexts.createInput2(text, height / 10)
            made.setAsMultiLine(P(x / 10, y / 10, 0),
                                P((x + 0.7 * height * len(text)) / 10, (y + height) / 10, 0),
                                adsk.core.HorizontalAlignments.LeftHorizontalAlignment,
                                adsk.core.VerticalAlignments.BottomVerticalAlignment, 0)
            sketch.sketchTexts.add(made)
        except Exception:
            pass


def material_profiles(sketch, adsk):
    """The sketch's regions that are material: nested an even number of times (the outside of a
    part, not its holes). A region's parent is the region whose inner loop is its outer loop."""

    def key(loop):
        return frozenset(c.sketchEntity.entityToken for c in loop.profileCurves)

    profiles = [sketch.profiles.item(i) for i in range(sketch.profiles.count)]
    outer_of = {}
    parent_by_loop = {}
    for i, profile in enumerate(profiles):
        for loop in profile.profileLoops:
            if loop.isOuter:
                outer_of[i] = key(loop)
            else:
                parent_by_loop[key(loop)] = i
    depth = {}

    def depth_of(i):
        if i not in depth:
            parent = parent_by_loop.get(outer_of.get(i))
            depth[i] = 0 if parent is None or parent == i else depth_of(parent) + 1
        return depth[i]

    chosen = adsk.core.ObjectCollection.create()
    for i, profile in enumerate(profiles):
        if depth_of(i) % 2 == 0:
            chosen.add(profile)
    return chosen


def build(path, adsk):
    app = adsk.core.Application.get()
    design = adsk.fusion.Design.cast(app.activeProduct)
    if design is None:
        raise RuntimeError("Open a design first.")
    layers = read_dxf(path)
    root = design.rootComponent
    wood = find_appearance(app, ["oak"]) or find_appearance(app, ["wood"])
    plastic = (find_appearance(app, ["plastic", "translucent", "white"])
               or find_appearance(app, ["acrylic"]))
    components = {}
    made = []

    def component(index, part):
        # A component per part in an Assembly document; a Part Design document holds only one,
        # so there everything goes in the root, the bodies and sketches named by part.
        if (index, part) not in components:
            comp = root
            if components.get("single") is None:
                try:
                    occurrence = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
                    comp = occurrence.component
                    comp.name = f"{index} {part}" if index else "rig"
                except RuntimeError:
                    components["single"] = True
            components[(index, part)] = comp
        return components[(index, part)]

    def sketch_at(comp, z_mm, name):
        planes = comp.constructionPlanes
        plane_input = planes.createInput()
        plane_input.setByOffset(comp.xYConstructionPlane,
                                adsk.core.ValueInput.createByReal(z_mm / 10))
        plane = planes.add(plane_input)
        plane.name = name  # Unique even when every part shares the root.
        sketch = comp.sketches.add(plane)
        sketch.name = name
        return sketch

    for name in sorted(layers, key=lambda n: (parse_layer_name(n) or (99,))[0]):
        parsed = parse_layer_name(name)
        if parsed is None:
            continue
        index, part, z_low, z_high, kind = parsed
        comp = component(index, part if index else "rig")
        z = z_high if kind == "engrave" else z_low
        sketch = sketch_at(comp, z, name)
        sketch.isComputeDeferred = True
        draw(sketch, layers[name], adsk)
        sketch.isComputeDeferred = False
        if kind != "cut" or z_high <= z_low:
            continue
        profiles = material_profiles(sketch, adsk)
        if profiles.count == 0:
            continue
        extrudes = comp.features.extrudeFeatures
        extrude_input = extrudes.createInput(
            profiles, adsk.fusion.FeatureOperations.NewBodyFeatureOperation)
        extrude_input.setDistanceExtent(False,
                                        adsk.core.ValueInput.createByReal((z_high - z_low) / 10))
        extrude = extrudes.add(extrude_input)
        look = plastic if part == "acrylic" else wood
        for body in extrude.bodies:
            body.name = f"{index} {part}"
            if look is not None:
                body.appearance = look
        made.append(f"{part}: {z_low:g} to {z_high:g} mm, {profiles.count} regions")
    return made


def run(context):
    import adsk.core
    import adsk.fusion

    ui = adsk.core.Application.get().userInterface
    try:
        dialog = ui.createFileDialog()
        dialog.title = "Psyight build kit, one DXF"
        dialog.filter = "DXF (*.dxf)"
        if dialog.showOpen() != adsk.core.DialogResults.DialogOK:
            return
        made = build(dialog.filename, adsk)
        ui.messageBox("Built:\n" + "\n".join(made) if made else
                      "No build kit layers found (names like 2_spacer_z6-15mm_cut).")
    except Exception:
        import traceback
        ui.messageBox("Failed:\n" + traceback.format_exc())
