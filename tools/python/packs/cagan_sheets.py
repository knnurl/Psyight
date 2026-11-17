"""Contact sheets and figures for the Cagan piece (docs/PLAN_CAGAN_PACK.md sections 4 and 5).

Each frame is drawn twice: as the piece would look (pixels in group "glow" blurred behind the
white layer on a dark wood ground, pixels in "sharp" as dots on top) and plain (every pixel a dot,
unlit ones dim grey). The diffuser view is a picture for judging, not a measurement; the figures
come from psyight_show_check.

    python cagan_sheets.py groups --exe <psyight_show_check.exe> --project <base.psyproj>
                                  --rig <Cagan flat.json> --audio <any audio file> --out <dir>
    python cagan_sheets.py candidates --exe ... --project ... --rig ... --times <times.json>
                                      --out <dir> [--only name,name] [--workers 4]
                                      [--no-reports] [--redraw]

groups: the bare rig with each group lit in turn, both views, in <out>/groups.png.
candidates: every candidate of cagan_candidates.py held by --scene over each track of the times
file, 16 frames a sheet at the times given for the candidate's first section kind, and a
whole-track report each, run one at a time so the render cost is measured alone. A review
project is the base project with the rig replaced, the flash limiter on and the candidates as its
only scenes (all of them in <out>/cagan.psyproj, one each in <out>/projects for the runs); limits, palettes and the power budget are the base project's (extra palettes from
cagan_candidates.PALETTES are appended). The times file holds the private paths:
    {"tracks": {"<track>": {"audio": "<path>",
                            "times": {"<section kind>": ["<caption>", [t_s, ...]]}}}}
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

from PIL import Image, ImageChops, ImageDraw, ImageFilter

GROUND = (34, 24, 16)  # Dark wood.
BLUR_M = 0.04  # Gaussian sigma of the white layer [Verify against a photo of the lit piece].
DOT_PX = 2.0
TILE = 260
GROUPS = ["arms", "arms_inner", "arms_outer", "sharp", "domes", "dome_1", "dome_2", "dome_3",
          "panels", "rim", "halo", "centre", "glow", "left", "right"]


def review_project(base_path, rig_path, scenes, palettes, out_dir, file_name="cagan.psyproj"):
    base = json.load(open(base_path, encoding="utf-8"))
    rig = json.load(open(rig_path, encoding="utf-8"))
    p = json.loads(json.dumps(base))
    order = base["rig"]["strips"][0].get("colour_order", "grb")
    p["rig"] = {"source": {"preset": rig["name"], "locked": False, "values": {}},
                "strips": [dict(s, colour_order=order) for s in rig["strips"]]}
    p["limiter"]["flash_limiter"] = True
    names = [q["name"] for q in p["palettes"]]
    for name, colours in palettes.items():
        if name not in names:
            p["palettes"].append({"name": name, "colours": colours})
            names.append(name)
    p["scenes"] = [scene_json(s, names) for s in scenes]
    path = os.path.join(out_dir, file_name)
    json.dump(p, open(path, "w", encoding="utf-8"), indent=1)
    return path, p, rig


def scene_json(s, palette_names):
    return {
        "name": s["name"], "sections": s["sections"],
        "palette": palette_names.index(s["palettes"][0]),
        "palette_list": [palette_names.index(q) for q in s["palettes"]],
        "layers": [{"effect": e, "blend": b, "opacity": o, "enabled": True, "group": g,
                    "params": prm} for e, b, o, g, prm in s["layers"]],
        "routes": [{"source": src, "target": t, "layer": l, "amount": a, "param": prm,
                    "invert": inv, "curve": "linear", "quantise": "off", "attack_s": 0.0,
                    "release_s": 0.0} for src, t, l, a, prm, inv in s["routes"]],
        "post": [{"type": t, "enabled": True, "params": prm} for t, prm in s["post"]],
    }


def run_frames(exe, proj, scene, times, audio, out):
    args = [exe, "--project", proj]
    if scene:
        args += ["--scene", scene]
    args += ["--at", ",".join(f"{t:.4f}" for t in times), "--frames-out", out, audio]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode not in (0, 3):
        print("FAILED", scene, audio, r.stderr[-400:], flush=True)
        return None
    return out


def read_frames(path):
    lines = open(path).read().splitlines()
    return json.loads(lines[0])["pixels"], [json.loads(l) for l in lines[1:]]


class View:
    """Pixel positions to tile coordinates, and which pixels glow."""

    def __init__(self, pixels, rig, size=TILE):
        xs = [p[0] for p in pixels]
        ys = [p[1] for p in pixels]
        self.cx, self.cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        span = max(max(xs) - min(xs), max(ys) - min(ys)) or 1.0
        self.size = size
        self.scale = (size - 16) / span
        self.xy = [(size / 2 + (x - self.cx) * self.scale, size / 2 - (y - self.cy) * self.scale)
                   for x, y, _ in pixels]
        self.sharp = ["sharp" in rig["strips"][s]["groups"] for _, _, s in pixels]
        self.sigma_px = BLUR_M * self.scale
        # A row of dots at 60 pixels a metre, blurred, peaks this much below one dot; applied
        # after the blur so colours keep their hue (sparser strips come out dimmer, as they would).
        pitch_px = self.scale / 60.0
        self.glow_gain = (math.sqrt(2 * math.pi) * self.sigma_px * min(pitch_px, 2 * DOT_PX) /
                          (math.pi * DOT_PX ** 2))

    def diffuser(self, colours, label):
        glow = Image.new("RGB", (self.size, self.size), (0, 0, 0))
        d = ImageDraw.Draw(glow)
        for (px, py), c, sharp in zip(self.xy, colours, self.sharp):
            if not sharp and any(c):
                d.ellipse((px - DOT_PX, py - DOT_PX, px + DOT_PX, py + DOT_PX), fill=c)
        g = self.glow_gain
        glow = glow.filter(ImageFilter.GaussianBlur(self.sigma_px)).point(
            lambda v: min(255, int(v * g)))
        # Sharp pixels as dots, added to what is behind them (a dim pixel never darkens it).
        dots = Image.new("RGB", glow.size, (0, 0, 0))
        d = ImageDraw.Draw(dots)
        for (px, py), c, sharp in zip(self.xy, colours, self.sharp):
            if sharp and any(c):
                d.ellipse((px - 1.6, py - 1.6, px + 1.6, py + 1.6), fill=c)
        im = ImageChops.add(ImageChops.add(Image.new("RGB", glow.size, GROUND), glow), dots)
        ImageDraw.Draw(im).text((4, 3), label, fill=(170, 170, 170))
        return im

    def plain(self, colours, label):
        im = Image.new("RGB", (self.size, self.size), (0, 0, 0))
        d = ImageDraw.Draw(im)
        for (px, py), c in zip(self.xy, colours):
            d.ellipse((px - 1.4, py - 1.4, px + 1.4, py + 1.4),
                      fill=c if any(c) else (26, 26, 26))
        d.text((4, 3), label, fill=(170, 170, 170))
        return im


def grid(tiles, columns, title):
    w = tiles[0].width
    rows = (len(tiles) + columns - 1) // columns
    im = Image.new("RGB", (columns * w, rows * w + 20), (0, 0, 0))
    ImageDraw.Draw(im).text((4, 4), title, fill=(255, 255, 255))
    for i, t in enumerate(tiles):
        im.paste(t, ((i % columns) * w, 20 + (i // columns) * w))
    return im


def groups_sheet(a):
    os.makedirs(a.out, exist_ok=True)
    dark = {"name": "Dark", "sections": ["groove"], "palettes": [], "layers": [], "routes": [],
            "post": []}
    proj, p, rig = review_project(a.project, a.rig, [], {}, a.out)
    p["scenes"] = [dict(scene_json(dict(dark, palettes=[p["palettes"][0]["name"]]),
                                   [q["name"] for q in p["palettes"]]))]
    json.dump(p, open(proj, "w", encoding="utf-8"), indent=1)
    path = run_frames(a.exe, proj, None, [1.0], a.audio, os.path.join(a.out, "positions.jsonl"))
    pixels, _ = read_frames(path)
    view = View(pixels, rig)
    lit, dim = (255, 150, 60), (0, 0, 0)
    tiles = []
    for g in GROUPS:
        colours = [lit if g in rig["strips"][s]["groups"] else dim for _, _, s in pixels]
        n = sum(1 for c in colours if c == lit)
        strips = sorted({s for _, _, s in pixels if g in rig["strips"][s]["groups"]})
        tiles.append((view.diffuser(colours, f"{g}: {n} px, {len(strips)} strips"),
                      view.plain(colours, f"{g} (plain)")))
    # Diffuser and plain side by side, pairs four to a row.
    flat = [t for pair in tiles for t in pair]
    title = (f"{rig['name']}: each group lit in turn, diffuser view (blur {BLUR_M * 100:.0f} cm) "
             f"and plain view; {len(rig['strips'])} strips, {len(pixels)} px")
    out = os.path.join(a.out, "groups.png")
    grid(flat, 8, title).save(out)
    print(out)


def report_figures(text):
    """The figures the plan asks for, from a whole-track report."""
    f = {}
    m = re.search(r"after: (\d+) \(most in any 1 s: (\d+)", text)
    f["flashes_after"], f["flashes_1s"] = (int(m[1]), int(m[2])) if m else (None, None)
    m = re.search(r"current: mean ([\d.]+) A, p95 ([\d.]+) A, max ([\d.]+) A", text)
    f["mean_a"], f["p95_a"], f["max_a"] = (float(m[1]), float(m[2]), float(m[3])) if m else (
        None, None, None)
    m = re.search(r"power limited ([\d.]+)% of frames", text)
    f["power_limited_pct"] = float(m[1]) if m else None
    m = re.search(r"dark pixels \(luminance < 0.05\): mean (\d+)%", text)
    f["dark_pct"] = int(m[1]) if m else None
    m = re.search(r"max byte (\d+), median lit (\d+) px, over a quarter above byte 20 in (\d+)%",
                  text)
    f["max_byte"], f["median_lit"], f["quarter_pct"] = (int(m[1]), int(m[2]), int(m[3])) if m \
        else (None, None, None)
    m = re.search(r"p50 ([\d.]+) ms, p99 ([\d.]+) ms", text)
    f["p50_ms"], f["p99_ms"] = (float(m[1]), float(m[2])) if m else (None, None)
    return f


def candidates_sheets(a):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import cagan_candidates as cc
    os.makedirs(a.out, exist_ok=True)
    for d in ("frames", "report"):
        os.makedirs(os.path.join(a.out, d), exist_ok=True)
    cfg = json.load(open(a.times, encoding="utf-8"))["tracks"]
    # Every candidate in one project (cagan.psyproj), to open in the app; the runs use one each.
    _, p, rig = review_project(a.project, a.rig, cc.CANDIDATES, cc.PALETTES, a.out)
    # Bytes back to the level the scene asked for: undo the gamma and the brightness cap. A
    # monitor's own gamma then shows light in proportion to the LEDs', scaled up by 1/cap.
    cap = p["limiter"]["brightness_cap"]
    gamma = p["limiter"].get("gamma", 2.2)
    shade = [min(255, int(round(255.0 * (b / 255.0) ** (1.0 / gamma) / cap))) for b in range(256)]
    chosen = [s for s in cc.CANDIDATES if not a.only or s["name"] in a.only.split(",")]
    jobs = [(s, t) for s in chosen for t in cfg if s["sections"][0] in cfg[t]["times"]]

    def slug(*parts):
        return "_".join(parts).replace(" ", "_")

    # One project per candidate, holding it alone: the reports then render nothing else.
    os.makedirs(os.path.join(a.out, "projects"), exist_ok=True)
    alone = {s["name"]: review_project(a.project, a.rig, [s], cc.PALETTES,
                                       os.path.join(a.out, "projects"),
                                       slug(s["name"]) + ".psyproj")[0] for s in chosen}

    def frames(job):
        s, t = job
        times = cfg[t]["times"][s["sections"][0]][1]
        out = os.path.join(a.out, "frames", slug(t, s["name"]) + ".jsonl")
        if a.redraw and os.path.exists(out):
            return out
        return run_frames(a.exe, alone[s["name"]], s["name"], times, cfg[t]["audio"],
                          os.path.join(a.out, "frames", slug(t, s["name"]) + ".jsonl"))

    with ThreadPoolExecutor(a.workers) as pool:
        paths = list(pool.map(frames, jobs))
    for (s, t), path in zip(jobs, paths):
        if not path:
            continue
        pixels, fr = read_frames(path)
        view = View(pixels, rig)
        caption = cfg[t]["times"][s["sections"][0]][0]
        for kind, draw in (("", view.diffuser), ("_plain", view.plain)):
            tiles = []
            for f in fr[:16]:
                rgb = f["rgb"]
                colours = [tuple(shade[v] for v in rgb[3 * i:3 * i + 3])
                           for i in range(len(pixels))]
                tiles.append(draw(colours, f"{f['t_s']:.2f} s"))
            title = (f"{s['name']} ({s['sections'][0]}): {t}, {caption} (bytes shown as "
                     f"levels: gamma {gamma}, cap {cap}"
                     f"{', plain' if kind else ', diffuser view'})")
            name = f"sheet_{slug(t, s['name'])}{kind}.png"
            grid(tiles, 8, title).save(os.path.join(a.out, name))
        print("sheet", s["name"], t, flush=True)
    if a.no_reports:
        return
    rows = []
    for s, t in jobs:  # One at a time: the render cost is measured alone.
        r = subprocess.run([a.exe, "--project", alone[s["name"]], "--scene", s["name"],
                            cfg[t]["audio"]],
                           capture_output=True, text=True)
        text = r.stdout + r.stderr
        open(os.path.join(a.out, "report", slug(t, s["name"]) + ".txt"), "w",
             encoding="utf-8").write(text)
        scene_line = next((l for l in text.splitlines()
                           if l.strip().startswith(f"scene {s['name']}:")), "")
        f = report_figures(text.split("scenes:")[0] + scene_line + "\n" +
                           "\n".join(l for l in text.splitlines() if "p50" in l))
        rows.append((s["name"], t, f))
        print(s["name"], t, f, flush=True)
    keys = ["dark_pct", "quarter_pct", "max_byte", "median_lit", "mean_a", "p95_a", "max_a",
            "power_limited_pct", "flashes_after", "flashes_1s", "p50_ms", "p99_ms"]
    with open(os.path.join(a.out, "figures.txt"), "w", encoding="utf-8") as out:
        out.write("candidate | track | " + " | ".join(keys) + "\n")
        for name, t, f in rows:
            out.write(f"{name} | {t} | " + " | ".join(str(f[k]) for k in keys) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["groups", "candidates"])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--project", required=True)
    ap.add_argument("--rig", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--audio")
    ap.add_argument("--times")
    ap.add_argument("--only")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--no-reports", action="store_true")
    ap.add_argument("--redraw", action="store_true", help="reuse frames already rendered")
    a = ap.parse_args()
    groups_sheet(a) if a.mode == "groups" else candidates_sheets(a)


if __name__ == "__main__":
    main()
