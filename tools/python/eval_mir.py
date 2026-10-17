"""Cross-check psyight's bass note scoring (app/src/analysis/scoring.*) with mir_eval (SPEC 6.10).

Usage, from the repository root (after a Release build of the app):
    tools/python/.venv/Scripts/python tools/python/eval_mir.py <labels.json> <estimates.json>
    tools/python/.venv/Scripts/python tools/python/eval_mir.py --fuzz [N] [--seed S]

The first form scores the estimates (a labels-format file, from psyight_eval --save-notes or the
M4 student's from m4_eval.py) against every complete region of the labels, here and with
psyight_eval --notes, and compares every metric. The second does the same on N random cases
(default 300) built to reach the edge cases: onsets at the tolerance, double triggers, notes at
region edges, octave and semitone errors, incomplete regions.

How the metrics are computed here, independently of the C++:
- Onset matching: mir_eval.transcription.match_note_onsets gives the maximum number of matches.
  Among the maximum matchings psyight takes the one with the least total onset distance
  (mir_eval's pairing is arbitrary); scipy's linear_sum_assignment finds it here, and its size is
  checked against mir_eval's.
- Regions: as score_track. A note belongs to a region when its onset is in [start, end); matching
  also sees notes up to one tolerance outside; unmatched estimates count only inside.
- Offsets: mir_eval's offset rule (ratio 0.2, at least 50 ms, distances rounded to 0.1 ms).
- Pitch: exact cents within 50, as psyight (mir_eval goes through Hz).
Prints a line per disagreement and exits 1 if there is any.
"""

import argparse
import json
import math
import pathlib
import re
import subprocess
import sys
import tempfile

import mir_eval
import numpy as np
from scipy.optimize import linear_sum_assignment

ROOT = pathlib.Path(__file__).resolve().parents[2]
EVAL = ROOT / "app" / "build" / "windows-msvc" / "release" / "psyight_eval.exe"

ONSET_TOLERANCE_S = 0.020
OFFSET_RATIO = 0.2
OFFSET_MIN_TOLERANCE_S = 0.050
PITCH_TOLERANCE_CENTS = 50.0


def rounded(d):
    return np.around(np.abs(d), 4)  # mir_eval's rounding of time distances.


def select(notes, regions, margin_s):
    chosen, inside = [], []
    for n in notes:
        on = n["onset_s"]
        if any(r["start_s"] - margin_s <= on < r["end_s"] + margin_s for r in regions):
            chosen.append(n)
            inside.append(any(r["start_s"] <= on < r["end_s"] for r in regions))
    return chosen, inside


def intervals(notes):
    return np.array([[n["onset_s"], n["offset_s"]] for n in notes], dtype=float).reshape(-1, 2)


def match(refs, ests):
    """Maximum matching with the least total onset distance, as (ref, est) index pairs."""
    if not refs or not ests:
        return []
    r = np.array([n["onset_s"] for n in refs])
    e = np.array([n["onset_s"] for n in ests])
    raw = np.abs(r[:, None] - e[None, :])
    feasible = rounded(r[:, None] - e[None, :]) <= ONSET_TOLERANCE_S
    big = ONSET_TOLERANCE_S * (min(len(refs), len(ests)) + 1)  # One more match beats any distance.
    rows, cols = linear_sum_assignment(np.where(feasible, raw - big, 0.0))
    pairs = [(int(i), int(j)) for i, j in zip(rows, cols) if feasible[i, j]]
    expected = mir_eval.transcription.match_note_onsets(
        intervals(refs), intervals(ests), onset_tolerance=ONSET_TOLERANCE_S, strict=False)
    if len(expected) != len(pairs):
        raise AssertionError(f"matching size {len(pairs)} but mir_eval finds {len(expected)}")
    return pairs


def cents(n):
    return n["midi"] * 100.0 + n.get("cents", 0.0)


def score_regions(reference_notes, estimates, regions, slot_s):
    refs, ref_inside = select(reference_notes, regions, ONSET_TOLERANCE_S)
    ests, est_inside = select(estimates, regions, ONSET_TOLERANCE_S)
    pairs = match(refs, ests)
    est_matched = {j for _, j in pairs}
    pairs = [(i, j) for i, j in pairs if ref_inside[i]]
    n_ref = sum(ref_inside)
    n_est = len(pairs) + sum(1 for j, inside in enumerate(est_inside)
                             if inside and j not in est_matched)
    s = {"labelled": n_ref, "detected": n_est, "matched": len(pairs)}
    if n_ref and n_est:
        p, rc = len(pairs) / n_est, len(pairs) / n_ref
        s.update(precision=p, recall=rc, f=0.0 if p + rc == 0 else 2 * p * rc / (p + rc))
    else:
        s.update(precision=0.0, recall=0.0, f=0.0)
    nan = float("nan")
    if not pairs:
        s.update(offset_median_ms=nan, offset_abs_median_ms=nan, offset_abs_p90_ms=nan,
                 offset_hit_rate=nan, pitch_accuracy=nan, octave_errors=0, gate_mae=nan)
        return s
    offsets, hits, pitch_ok, octaves, gate = [], 0, 0, 0, []
    for i, j in pairs:
        ref, est = refs[i], ests[j]
        offsets.append(est["offset_s"] - ref["offset_s"])
        tol = max(OFFSET_RATIO * (ref["offset_s"] - ref["onset_s"]), OFFSET_MIN_TOLERANCE_S)
        hits += rounded(est["offset_s"] - ref["offset_s"]) <= tol
        d = cents(est) - cents(ref)
        k = round(d / 1200.0)
        if abs(d) <= PITCH_TOLERANCE_CENTS:
            pitch_ok += 1
        elif k != 0 and abs(d - k * 1200.0) <= PITCH_TOLERANCE_CENTS:
            octaves += 1
        gate.append(abs((est["offset_s"] - est["onset_s"]) - (ref["offset_s"] - ref["onset_s"]))
                    / slot_s)
    offsets = np.array(offsets)
    s.update(offset_median_ms=1e3 * np.percentile(offsets, 50),
             offset_abs_median_ms=1e3 * np.percentile(np.abs(offsets), 50),
             offset_abs_p90_ms=1e3 * np.percentile(np.abs(offsets), 90),
             offset_hit_rate=hits / len(pairs), pitch_accuracy=pitch_ok / len(pairs),
             octave_errors=octaves, gate_mae=float(np.mean(gate)))
    return s


NUMBER = r"([-+]?(?:\d+\.?\d*|nan|inf)(?:\(ind\))?)"
PATTERNS = {
    ("labelled", "detected", "matched"):
        rf"notes: {NUMBER} labelled, {NUMBER} detected, {NUMBER} matched",
    ("precision", "recall", "f"): rf"onset: precision {NUMBER}, recall {NUMBER}, F {NUMBER}",
    ("offset_median_ms", "offset_abs_median_ms", "offset_abs_p90_ms", "offset_hit_rate"):
        rf"offset error: median {NUMBER} ms, \|median\| {NUMBER} ms, \|p90\| {NUMBER} ms, "
        rf"hit rate {NUMBER}",
    ("pitch_accuracy", "octave_errors"): rf"pitch: accuracy {NUMBER} \(within 50 cents\), "
                                         rf"octave errors {NUMBER}",
    ("gate_mae",): rf"gate ratio MAE: {NUMBER}",
}
# Printed precision of each value: a difference within half of it is rounding.
UNIT = {"offset_median_ms": 0.1, "offset_abs_median_ms": 0.1, "offset_abs_p90_ms": 0.1,
        "labelled": 1, "detected": 1, "matched": 1, "octave_errors": 1}


def to_float(text):
    text = text.lower()
    return float("nan") if "nan" in text else float(text)


def psyight_scores(labels_path, estimates_path):
    out = subprocess.run([str(EVAL), "audio-not-read", str(labels_path), "--notes",
                          str(estimates_path)], capture_output=True, text=True, encoding="utf-8",
                         errors="replace", check=True).stdout
    blocks = re.split(r"^Region ", out, flags=re.M)[1:]
    scores = []
    for block in blocks:
        s = {}
        for names, pattern in PATTERNS.items():
            found = re.search(pattern, block)
            if not found:
                raise AssertionError(f"cannot parse psyight_eval output:\n{block}")
            s.update({n: to_float(v) for n, v in zip(names, found.groups())})
        scores.append(s)
    return scores


def compare(labels, estimates, labels_path, estimates_path, tag):
    slot_s = 60.0 / labels["grid"]["bpm"] / 4.0
    complete = [r for r in labels["regions"] if r["complete"]]
    mine = [score_regions(labels["bass_notes"], estimates["bass_notes"], [r], slot_s)
            for r in complete]
    theirs = psyight_scores(labels_path, estimates_path)
    problems = []
    if len(mine) != len(theirs):
        return [f"{tag}: {len(mine)} complete regions here, {len(theirs)} from psyight_eval"]
    for k, (a, b) in enumerate(zip(mine, theirs)):
        for name, value in a.items():
            other = b[name]
            if math.isnan(value) and math.isnan(other):
                continue
            unit = UNIT.get(name, 0.001)
            if math.isnan(value) or math.isnan(other) or abs(value - other) > 0.5 * unit + 1e-9:
                problems.append(f"{tag} region {k}: {name} {value} here, {other} psyight")
    return problems


def write(path, labels):
    path.write_text(json.dumps(labels), encoding="utf-8")


def note(onset_s, length_s, midi, cents_=0.0):
    return {"onset_s": onset_s, "offset_s": onset_s + length_s, "midi": midi, "cents": cents_,
            "slide_in": False, "slide_out": False, "velocity": 1.0}


def random_case(rng):
    bpm = float(rng.uniform(140.0, 190.0))
    slot = 60.0 / bpm / 4.0
    duration = 20.0
    refs, ests = [], []
    for k in range(int(duration / slot)):
        if rng.random() > 0.6:
            continue
        on = k * slot + rng.normal(0.0, 0.003)
        if on < 0.0:
            continue
        long_note = rng.random() < 0.2  # Long enough for the offset ratio to set the tolerance.
        length = rng.uniform(2.0, 8.0) * slot if long_note else rng.uniform(0.15, 1.3) * slot
        midi = int(rng.integers(24, 41))
        refs.append(note(on, length, midi, float(rng.uniform(-20, 20))))
        if rng.random() < 0.12:
            continue  # Missed.
        roll = rng.random()
        if roll < 0.1:  # Close to the tolerance on either side.
            est_on = on + rng.choice([-1.0, 1.0]) * (ONSET_TOLERANCE_S + rng.uniform(-3e-4, 3e-4))
        else:
            est_on = on + rng.normal(0.0, 0.008)
        est_len = max(0.002, length + rng.normal(0.0, 0.08 if long_note else 0.03))
        p = rng.random()
        est_midi = midi + (12 * int(rng.choice([-1, 1])) if p < 0.06 else
                           int(rng.integers(-2, 3)) if p < 0.12 else 0)
        ests.append(note(max(0.0, est_on), est_len, est_midi, float(rng.uniform(-60, 60))))
        if rng.random() < 0.05:  # Double trigger.
            ests.append(note(est_on + rng.uniform(0.004, 0.03), est_len, est_midi))
    for _ in range(int(rng.integers(0, 15))):  # False positives.
        ests.append(note(float(rng.uniform(0, duration)), 0.05, int(rng.integers(24, 41))))
    ests.sort(key=lambda n: n["onset_s"])
    regions = []
    for _ in range(int(rng.integers(1, 4))):
        anchor = refs[int(rng.integers(0, len(refs)))]["onset_s"] if refs else 1.0
        start = max(0.0, anchor + rng.uniform(-0.025, 0.025))
        end = min(duration, start + rng.uniform(1.0, 8.0))
        regions.append({"start_s": start, "end_s": end, "complete": bool(rng.random() < 0.8)})
    if not any(r["complete"] for r in regions):
        regions[0]["complete"] = True
    base = {"schema": 1, "audio": "fuzz.wav", "sample_rate": 48000,
            "grid": {"bpm": bpm, "first_downbeat_s": 0.0}, "kicks": [], "beats": [],
            "downbeats": [], "sections": []}
    return ({**base, "regions": regions, "bass_notes": refs},
            {**base, "regions": [], "bass_notes": ests})


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("labels", nargs="?")
    parser.add_argument("estimates", nargs="?")
    parser.add_argument("--fuzz", nargs="?", type=int, const=300)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    problems = []
    if args.fuzz:
        rng = np.random.default_rng(args.seed)
        with tempfile.TemporaryDirectory() as work:
            ref_path = pathlib.Path(work) / "ref.json"
            est_path = pathlib.Path(work) / "est.json"
            for case in range(args.fuzz):
                labels, estimates = random_case(rng)
                write(ref_path, labels)
                write(est_path, estimates)
                problems += compare(labels, estimates, ref_path, est_path, f"case {case}")
        print(f"{args.fuzz} random cases (seed {args.seed}): {len(problems)} disagreements")
    elif args.labels and args.estimates:
        labels = json.loads(pathlib.Path(args.labels).read_text(encoding="utf-8"))
        estimates = json.loads(pathlib.Path(args.estimates).read_text(encoding="utf-8"))
        slot_s = 60.0 / labels["grid"]["bpm"] / 4.0
        for r in (r for r in labels["regions"] if r["complete"]):
            s = score_regions(labels["bass_notes"], estimates["bass_notes"], [r], slot_s)
            print(f"Region {r['start_s']:.3f} to {r['end_s']:.3f} s: " +
                  ", ".join(f"{k} {v:.3f}" if isinstance(v, float) else f"{k} {v}"
                            for k, v in s.items()))
        problems = compare(labels, estimates, args.labels, args.estimates, "track")
        print(f"psyight_eval agrees on every metric" if not problems
              else f"{len(problems)} disagreements")
    else:
        parser.error("give a labels file and an estimates file, or --fuzz")
    for p in problems[:40]:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
