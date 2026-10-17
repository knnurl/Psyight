"""Bass notes from the M4 student's outputs (shared by m4_tune.py and m4_eval.py).

Onsets: peaks of the onset probability above `onset_threshold`, keeping only the strongest
within `min_gap_frames` (8 frames, 43 ms; the DSP tracker's minimum gap is 45 ms): a causal
model's onset curve can have two local peaks around one real onset. A note then lasts at least
`min_frames` (4 frames, 21 ms; the gate head lags the onset by a frame or two) and ends when the
gate probability falls below `gate_threshold`, or at the next onset. Pitch: the most common
class over the note. Times are input frames: output frame k describes input frame k - lookahead.
"""

import numpy as np

DEFAULTS = {"onset_threshold": 0.5, "gate_threshold": 0.5, "min_gap_frames": 8, "min_frames": 4}


def sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-x))


def onset_peaks(prob: np.ndarray, threshold: float, min_gap: int) -> np.ndarray:
    candidates = np.where((prob[1:-1] >= threshold) & (prob[1:-1] >= prob[:-2]) &
                          (prob[1:-1] > prob[2:]))[0] + 1
    kept = []
    for k in candidates[np.argsort(-prob[candidates], kind="stable")]:  # Strongest first.
        if all(abs(int(k) - j) >= min_gap for j in kept):
            kept.append(int(k))
    return np.array(sorted(kept), dtype=np.int64)


def notes(binary: np.ndarray, pitch: np.ndarray, lookahead: int, first_midi: int,
          params: dict | None = None) -> list[tuple[int, int, int]]:
    """(onset frame, end frame, MIDI) per note, in input frames."""
    p = dict(DEFAULTS, **(params or {}))
    onset = sigmoid(binary[2])
    gate = sigmoid(binary[3])
    classes = pitch.argmax(axis=0)
    peaks = onset_peaks(onset, p["onset_threshold"], p["min_gap_frames"])
    out = []
    for n, k in enumerate(peaks):
        stop = int(peaks[n + 1]) if n + 1 < len(peaks) else len(gate)
        end = min(stop, int(k) + p["min_frames"])
        while end < stop and gate[end] >= p["gate_threshold"]:
            end += 1
        midi = int(np.bincount(classes[k:end]).argmax()) + first_midi
        out.append((int(k) - lookahead, end - lookahead, midi))
    return out
