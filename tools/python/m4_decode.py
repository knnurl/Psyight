"""Bass notes from the M4 student's outputs (shared by m4_tune.py and m4_eval.py).

Onsets: peaks of the onset probability above `onset_threshold`, keeping only the strongest
within `min_gap_frames` (8 frames, 43 ms; the DSP tracker's minimum gap is 45 ms): a causal
model's onset curve can have two local peaks around one real onset. With `kick_mask_frames`, a
bass onset within that many frames of a kick peak (kick probability above `kick_threshold`) is
dropped: in K-B-B-B the bass does not start on the kick (the DSP tracker masks kicks the same way).
A note then lasts at least `min_frames` (4 frames, 21 ms; the gate head lags the onset by a frame
or two) and ends when the gate probability falls below `gate_threshold`, or at the next onset.
Times are refined below a frame (5.33 ms): the onset by a parabola through the peak and its
neighbours, the end by linear interpolation of the gate's crossing. Pitch: the most common class
over the note. Times are input frames: output frame k describes input frame k - lookahead.
"""

import numpy as np

DEFAULTS = {"onset_threshold": 0.5, "gate_threshold": 0.5, "min_gap_frames": 8, "min_frames": 4,
            "kick_mask_frames": 0, "kick_threshold": 0.5, "subframe": True}


def sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-x))


def peaks(prob: np.ndarray, threshold: float, min_gap: int) -> np.ndarray:
    candidates = np.where((prob[1:-1] >= threshold) & (prob[1:-1] >= prob[:-2]) &
                          (prob[1:-1] > prob[2:]))[0] + 1
    kept = []
    for k in candidates[np.argsort(-prob[candidates], kind="stable")]:  # Strongest first.
        if all(abs(int(k) - j) >= min_gap for j in kept):
            kept.append(int(k))
    return np.array(sorted(kept), dtype=np.int64)


def parabolic(y: np.ndarray, k: int) -> float:
    """Offset (-0.5 to 0.5) of the vertex of the parabola through y[k - 1], y[k], y[k + 1]."""
    if k <= 0 or k >= len(y) - 1:
        return 0.0
    a, b, c = y[k - 1], y[k], y[k + 1]
    denominator = a - 2.0 * b + c
    return float(np.clip(0.5 * (a - c) / denominator, -0.5, 0.5)) if denominator < 0 else 0.0


def notes(binary: np.ndarray, pitch: np.ndarray, lookahead: int, first_midi: int,
          params: dict | None = None) -> list[tuple[float, float, int]]:
    """(onset, end, MIDI) per note, in input frames (fractional when refined)."""
    p = dict(DEFAULTS, **(params or {}))
    onset = sigmoid(binary[2])
    gate = sigmoid(binary[3])
    classes = pitch.argmax(axis=0)
    found = peaks(onset, p["onset_threshold"], p["min_gap_frames"])
    if p["kick_mask_frames"] > 0:
        kicks = peaks(sigmoid(binary[0]), p["kick_threshold"], 4)
        found = np.array([k for k in found
                          if kicks.size == 0 or np.min(np.abs(kicks - k)) > p["kick_mask_frames"]],
                         dtype=np.int64)
    out = []
    for n, k in enumerate(found):
        stop = int(found[n + 1]) if n + 1 < len(found) else len(gate)
        end = min(stop, int(k) + p["min_frames"])
        while end < stop and gate[end] >= p["gate_threshold"]:
            end += 1
        midi = int(np.bincount(classes[k:end]).argmax()) + first_midi
        start = float(k)
        finish = float(end)
        if p["subframe"]:
            start += parabolic(onset, int(k))
            if end < stop and end > 0 and gate[end - 1] > gate[end]:
                # The gate crosses the threshold between frames end - 1 and end.
                finish = end - 1 + (gate[end - 1] - p["gate_threshold"]) / (gate[end - 1] - gate[end])
        out.append((float(start - lookahead), float(finish - lookahead), midi))
    return out
