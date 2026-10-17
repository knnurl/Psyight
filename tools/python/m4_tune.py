"""Choose the M4 student's decoding settings on its validation tracks (never on labelled audio).

Usage, from the repository root (after m4_train.py):
    tools/python/.venv/Scripts/python tools/python/m4_tune.py

Runs data/m4/student.onnx over the validation tracks named in data/m4/student.json, rebuilds the
teacher's notes from its targets, and decodes the student's outputs (m4_decode.py) over a grid of
settings. Onset settings (threshold, minimum gap, kick mask) are chosen by onset F within +-4 frames (21 ms);
then the gate settings (threshold, minimum length) by the mean absolute note-length error on the
matched notes. Writes the choice to student.json ("decoder") and prints the grid.
"""

import itertools
import json
import pathlib
import sys

import numpy as np
import onnxruntime as ort

import m4_decode
from m4_train import load_track

ROOT = pathlib.Path(__file__).resolve().parents[2]
M4 = ROOT / "data" / "m4"
TOLERANCE = 4  # Frames.


def teacher_notes(t: dict) -> list[tuple[int, int, int]]:
    onsets = np.where(t["bass_onset"] == 1.0)[0]
    gate = t["bass_gate"]
    out = []
    for n, k in enumerate(onsets):
        stop = int(onsets[n + 1]) if n + 1 < len(onsets) else len(gate)
        end = int(k) + 1
        while end < stop and gate[end] > 0.5:
            end += 1
        out.append((int(k), end, int(t["bass_pitch"][min(k + 1, len(gate) - 1)])))
    return out


def match(est, ref):
    """Greedy one-to-one matches within TOLERANCE frames, nearest first."""
    ref_on = np.array([r[0] for r in ref])
    used = np.zeros(len(ref), bool)
    pairs = []
    for e in est:
        j = int(np.searchsorted(ref_on, e[0]))
        best = None
        for c in (j - 1, j):
            if 0 <= c < len(ref) and not used[c] and abs(ref_on[c] - e[0]) <= TOLERANCE:
                if best is None or abs(ref_on[c] - e[0]) < abs(ref_on[best] - e[0]):
                    best = c
        if best is not None:
            used[best] = True
            pairs.append((e, ref[best]))
    return pairs


def score(outputs, refs, info, params):
    hits = est_count = ref_count = 0
    length_err = []
    pitch_ok = []
    for (binary, pitch), ref in zip(outputs, refs):
        est = m4_decode.notes(binary, pitch, info["lookahead"], info["first_midi"], params)
        pairs = match(est, ref)
        hits += len(pairs)
        est_count += len(est)
        ref_count += len(ref)
        length_err += [abs((e[1] - e[0]) - (r[1] - r[0])) for e, r in pairs]
        pitch_ok += [e[2] == r[2] for e, r in pairs if r[2] >= 0]
    precision = hits / max(1, est_count)
    recall = hits / max(1, ref_count)
    f = 2 * precision * recall / max(1e-9, precision + recall)
    return {"f": f, "precision": precision, "recall": recall,
            "length_mae_frames": float(np.mean(length_err)) if length_err else 0.0,
            "pitch": float(np.mean(pitch_ok)) if pitch_ok else 0.0}


def main() -> int:
    info = json.loads((M4 / "student.json").read_text(encoding="utf-8"))
    session = ort.InferenceSession(str(M4 / "student.onnx"), providers=["CPUExecutionProvider"])
    outputs, refs = [], []
    for track_id in info["validation_tracks"]:
        t = load_track(M4 / "tracks" / track_id)
        x = (t["features"].astype(np.float32) * t["scale"]).T[None]
        binary, pitch = session.run(None, {"features": x})
        outputs.append((binary[0], pitch[0]))
        refs.append(teacher_notes(t))
    print(f"{len(refs)} validation tracks, {sum(len(r) for r in refs)} teacher notes")

    best_onset = None
    for threshold, gap, kick in itertools.product((0.3, 0.4, 0.5, 0.6), (0, 8), (0, 2)):
        params = {"onset_threshold": threshold, "min_gap_frames": gap, "kick_mask_frames": kick}
        s = score(outputs, refs, info, params)
        print(f"  onset {threshold}, gap {gap}, kick mask {kick}: F {s['f']:.3f} "
              f"(P {s['precision']:.3f}, R {s['recall']:.3f})")
        if best_onset is None or s["f"] > best_onset[0]["f"]:
            best_onset = (s, params)
    best = None
    for gate, min_frames in itertools.product((0.3, 0.4, 0.5, 0.6), (1, 4)):
        params = dict(best_onset[1], gate_threshold=gate, min_frames=min_frames)
        s = score(outputs, refs, info, params)
        print(f"  gate {gate}, min {min_frames}: length MAE {s['length_mae_frames']:.2f} frames, "
              f"pitch {s['pitch']:.3f}")
        if best is None or s["length_mae_frames"] < best[0]["length_mae_frames"]:
            best = (s, params)
    info["decoder"] = dict(m4_decode.DEFAULTS, **best[1])
    info["decoder_validation"] = best[0]
    (M4 / "student.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
    print(f"chosen: {info['decoder']}: {best[0]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
