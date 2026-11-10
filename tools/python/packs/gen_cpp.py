# Writes the Hexagon 3 packs (python gen_cpp.py, then clang-format presets.cpp) into app/src/engine/presets.cpp from hex3_spec.py: the scene presets
# (before the closing loop of builtin_scene_presets) and the packs (after Hexagon 2), between
# marker comments so it can be rerun.
import os
import re
from hex3_spec import PACKS

PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "app", "src",
                    "engine", "presets.cpp")
BEGIN_S, END_S = "    // BEGIN Hexagon 3 scenes (generated from hex3_spec.py)\n", "    // END Hexagon 3 scenes\n"
BEGIN_P, END_P = "    // BEGIN Hexagon 3 packs (generated from hex3_spec.py)\n", "    // END Hexagon 3 packs\n"


def q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def num(v):
    return repr(float(v))


def wrap(text, indent, width=96):
    # A C++ string literal split over lines.
    words, lines, line = text.split(" "), [], ""
    for w in words:
        if len(line) + len(w) + 1 > width - indent - 4 and line:
            lines.append(line + " ")
            line = w
        else:
            line = (line + " " + w) if line else w
    lines.append(line)
    return ("\n" + " " * indent).join(q(l) for l in lines)


scenes_cpp = [BEGIN_S]
for pack in PACKS.values():
    for s in pack["scenes"]:
        out = []
        out.append("    {\n")
        out.append(f"        ScenePreset p{{{q(s['name'])},\n")
        out.append(f"                      {wrap(s['description'], 22)},\n")
        out.append(f"                      {{}},\n")
        out.append(f"                      palette_named({q(s['palettes'][0])})}};\n")
        out.append("        p.scene.sections = {" + ", ".join(q(x) for x in s["sections"]) + "};\n")
        for effect, blend, opacity, group, params in s["layers"]:
            ps = ", ".join(f"{{{q(k)}, {num(v)}}}" for k, v in params.items())
            out.append(f"        p.scene.layers.push_back(layer_by_id({q(effect)}, {{{ps}}}, "
                       f"{q(blend)}, {opacity}f, {q(group)}));\n")
        for src, target, layer, amount, param, invert in s["routes"]:
            out.append(f"        p.scene.routes.push_back(route_by_id({q(src)}, {q(target)}, {layer}, "
                       f"{num(amount)}, {q(param)}, {'true' if invert else 'false'}));\n")
        for kind, params in s["post"]:
            assert kind == "afterglow"
            out.append(f"        p.scene.post.push_back({{AfterglowParams{{{num(params['decay_beats'])}, "
                       f"{num(params['amount'])}}}, true}});\n")
        out.append("        looks.push_back(std::move(p));\n")
        out.append("    }\n")
        scenes_cpp.append("".join(out))
scenes_cpp.append(END_S)

packs_cpp = [BEGIN_P]
for name, pack in PACKS.items():
    names = ", ".join(f"find(looks, {q(s['name'])})" for s in pack["scenes"])
    lists = ", ".join("{" + q(s["name"]) + ", {" + ", ".join(q(p) for p in s["palettes"]) + "}}"
                      for s in pack["scenes"])
    packs_cpp.append(
        f"    packs.push_back(rotating_pack({q(name)},\n"
        f"                                  {wrap(pack['description'], 34)},\n"
        f"                                  {{{names}}},\n"
        f"                                  {{{lists}}}));\n")
packs_cpp.append(END_P)

src = open(PATH, encoding="utf-8").read()
src = re.sub(re.escape(BEGIN_S) + ".*?" + re.escape(END_S), "", src, flags=re.S)
src = re.sub(re.escape(BEGIN_P) + ".*?" + re.escape(END_P), "", src, flags=re.S)
anchor = "    for (ScenePreset &p : looks) {\n        p.scene.name = p.name;"
assert src.count(anchor) == 1
src = src.replace(anchor, "".join(scenes_cpp) + anchor)
anchor = "    packs.push_back(std::move(hex2));\n"
assert src.count(anchor) == 1
src = src.replace(anchor, anchor + "".join(packs_cpp))
open(PATH, "w", encoding="utf-8", newline="\n").write(src)
print("ok", sum(len(p["scenes"]) for p in PACKS.values()), "scenes")
