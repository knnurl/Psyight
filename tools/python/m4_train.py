"""Train the causal M4 student on the teacher data and export it to ONNX (SPEC 6.9).

Usage, from the repository root (after m4_dataset.py export):
    tools/python/.venv/Scripts/python tools/python/m4_train.py [--steps 20000] [--lookahead 2]

Reads data/m4/tracks/*.{json,bin}; splits by track (about 10% for validation, chosen by id, so
the split is stable); trains on random 5.5 s crops; writes data/m4/student.pt, data/m4/student.onnx
and data/m4/student.json (settings and validation metrics). Weights trained on private music
stay in data/ (gitignored).

Model: a 1x1 projection, then two stacks of causal dilated convolutions (kernel 3, dilations 1 to
32: 253 frames, 1.35 s, of context), residual, batch norm (fixed at inference, so still causal).
Output frame t predicts the targets of frame t - lookahead, adding lookahead * 5.33 ms of
latency. Heads: kick, hat and bass onset, bass gate, residual activity (sigmoid) and bass pitch
(44 classes, MIDI 21 to 64, trained where the gate is on).
"""

import argparse
import json
import pathlib
import time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

ROOT = pathlib.Path(__file__).resolve().parents[2]
M4 = ROOT / "data" / "m4"
FIRST_MIDI = 21
PITCHES = 44  # MIDI 21 to 64.
ONSETS = ("kick_onset", "hat_onset", "bass_onset")
CROP = 1024  # Frames, 5.5 s.


def load_track(base: pathlib.Path) -> dict:
    meta = json.loads(base.with_suffix(".json").read_text(encoding="utf-8"))
    raw = np.memmap(base.with_suffix(".bin"), dtype=np.uint8, mode="r")

    def arr(name):
        a = meta["arrays"][name]
        count = int(np.prod(a["shape"]))
        return np.frombuffer(raw, dtype=np.dtype(a["dtype"]), count=count,
                             offset=a["offset"]).reshape(a["shape"])

    out = {"id": base.stem, "meta": meta, "features": arr("features"),
           "scale": meta["features_scale"]}
    for name in ONSETS + ("bass_gate", "bass_pitch", "residual_activity"):
        out[name] = arr(name)
    return out


class Student(nn.Module):
    def __init__(self, dims: int, channels: int = 96, dilations=(1, 2, 4, 8, 16, 32) * 2):
        super().__init__()
        self.inp = nn.Conv1d(dims, channels, 1)
        self.dilations = dilations
        self.convs = nn.ModuleList(nn.Conv1d(channels, channels, 3, dilation=d) for d in dilations)
        self.norms = nn.ModuleList(nn.BatchNorm1d(channels) for _ in dilations)
        self.binary = nn.Conv1d(channels, 5, 1)  # kick, hat, bass onset, gate, residual.
        self.pitch = nn.Conv1d(channels, PITCHES, 1)

    def receptive_field(self) -> int:
        return 1 + 2 * sum(self.dilations)

    def forward(self, x):  # x: (batch, dims, frames).
        h = self.inp(x)
        for conv, norm, d in zip(self.convs, self.norms, self.dilations):
            y = conv(F.pad(h, (2 * d, 0)))  # Left padding only: causal.
            h = h + F.gelu(norm(y))
        return self.binary(h), self.pitch(h)


def batch(tracks, rng, size, lookahead, device):
    xs, ys, ps = [], [], []
    for _ in range(size):
        t = tracks[rng.integers(len(tracks))]
        frames = t["features"].shape[0]
        start = int(rng.integers(0, max(1, frames - CROP - lookahead)))
        x = t["features"][start:start + CROP].astype(np.float32) * t["scale"]
        # Output frame k sees input up to k and predicts the targets of frame k - lookahead.
        sl = slice(max(0, start - lookahead), start - lookahead + CROP)
        pad = CROP - (sl.stop - sl.start)
        y = np.stack([t[n][sl] for n in ONSETS + ("bass_gate", "residual_activity")])
        p = t["bass_pitch"][sl].astype(np.int64) - FIRST_MIDI
        p[(p < 0) | (p >= PITCHES) | (t["bass_gate"][sl] < 0.5)] = -100
        if pad:
            y = np.pad(y, ((0, 0), (pad, 0)))
            p = np.pad(p, (pad, 0), constant_values=-100)
        xs.append(x.T)
        ys.append(y)
        ps.append(p)
    as_tensor = lambda a, dtype: torch.as_tensor(np.stack(a), dtype=dtype, device=device)
    return as_tensor(xs, torch.float32), as_tensor(ys, torch.float32), as_tensor(ps, torch.int64)


def losses(model, x, y, p):
    binary, pitch = model(x)
    weight = torch.ones_like(y)
    weight[:, :3] += 9.0 * (y[:, :3] > 0)  # Onsets are rare: weigh their frames up.
    bce = F.binary_cross_entropy_with_logits(binary, y, weight=weight)
    ce = F.cross_entropy(pitch, p, ignore_index=-100)
    return bce + 0.5 * ce, bce.item(), ce.item()


def onset_f(prob: np.ndarray, target: np.ndarray, threshold=0.5, tolerance=4) -> float:
    """Peak-picked onsets against the target's exact frames (value 1), within +-4 frames."""
    peaks = np.where((prob[1:-1] >= threshold) & (prob[1:-1] >= prob[:-2]) &
                     (prob[1:-1] > prob[2:]))[0] + 1
    truth = np.where(target == 1.0)[0]
    if len(truth) == 0 and len(peaks) == 0:
        return 1.0
    used = np.zeros(len(truth), bool)
    hits = 0
    for k in peaks:
        j = np.searchsorted(truth, k)
        for c in (j - 1, j):
            if 0 <= c < len(truth) and not used[c] and abs(truth[c] - k) <= tolerance:
                used[c] = True
                hits += 1
                break
    precision = hits / max(1, len(peaks))
    recall = hits / max(1, len(truth))
    return 2 * precision * recall / max(1e-9, precision + recall)


@torch.no_grad()
def validate(model, tracks, lookahead, device):
    model.eval()
    scores = {n: [] for n in ONSETS}
    gate_acc, pitch_acc = [], []
    for t in tracks:
        x = torch.as_tensor(t["features"].astype(np.float32).T * t["scale"],
                            device=device)[None]
        binary, pitch = model(x)
        prob = torch.sigmoid(binary)[0].cpu().numpy()[:, lookahead:]
        pitch = pitch[0].argmax(0).cpu().numpy()[lookahead:]
        n = prob.shape[1]
        for i, name in enumerate(ONSETS):
            scores[name].append(onset_f(prob[i], t[name][:n]))
        gate = t["bass_gate"][:n] > 0.5
        gate_acc.append(float(np.mean((prob[3] > 0.5) == gate)))
        voiced = gate & (t["bass_pitch"][:n] >= FIRST_MIDI)
        if voiced.any():
            pitch_acc.append(float(np.mean(pitch[voiced] + FIRST_MIDI ==
                                           t["bass_pitch"][:n][voiced])))
    model.train()
    out = {f"{n}_f": float(np.mean(v)) for n, v in scores.items()}
    out["gate_accuracy"] = float(np.mean(gate_acc))
    out["pitch_accuracy"] = float(np.mean(pitch_acc)) if pitch_acc else 0.0
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--steps", type=int, default=20000)
    parser.add_argument("--batch", type=int, default=32)
    parser.add_argument("--lookahead", type=int, default=2)
    parser.add_argument("--channels", type=int, default=96)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    bases = sorted((M4 / "tracks").glob("*.json"))
    tracks = [load_track(b.with_suffix("")) for b in bases]
    if len(tracks) < 2:
        print("need at least two exported tracks (m4_dataset.py export)")
        return 1
    val = [t for t in tracks if int(t["id"], 16) % 10 == 0] or tracks[-1:]
    train = [t for t in tracks if t not in val]
    dims = tracks[0]["features"].shape[1]
    model = Student(dims, args.channels).to(device)
    params = sum(p.numel() for p in model.parameters())
    print(f"{len(train)} training and {len(val)} validation tracks, {params} parameters, "
          f"receptive field {model.receptive_field()} frames, lookahead {args.lookahead}",
          flush=True)
    opt = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=2e-3, total_steps=args.steps)
    start = time.time()
    best = None
    for step in range(1, args.steps + 1):
        x, y, p = batch(train, rng, args.batch, args.lookahead, device)
        loss, bce, ce = losses(model, x, y, p)
        opt.zero_grad()
        loss.backward()
        opt.step()
        sched.step()
        if step % 1000 == 0 or step == args.steps:
            metrics = validate(model, val, args.lookahead, device)
            print(f"step {step}: loss {loss.item():.4f} (bce {bce:.4f}, pitch {ce:.3f}); val "
                  + ", ".join(f"{k} {v:.3f}" for k, v in metrics.items())
                  + f"; {time.time() - start:.0f} s", flush=True)
            score = metrics["bass_onset_f"] + metrics["pitch_accuracy"]
            if best is None or score > best[0]:
                best = (score, metrics, step)
                torch.save(model.state_dict(), M4 / "student.pt")
    model.load_state_dict(torch.load(M4 / "student.pt", map_location=device))
    model.eval().cpu()
    example = torch.zeros(1, dims, model.receptive_field())
    torch.onnx.export(model, example, M4 / "student.onnx", input_names=["features"],
                      output_names=["binary", "pitch"],
                      dynamic_axes={"features": {2: "frames"}, "binary": {2: "frames"},
                                    "pitch": {2: "frames"}}, opset_version=17)
    info = {"dims": dims, "channels": args.channels, "lookahead": args.lookahead,
            "receptive_field": model.receptive_field(), "first_midi": FIRST_MIDI,
            "pitches": PITCHES, "best_step": best[2], "validation": best[1],
            "train_tracks": [t["id"] for t in train], "validation_tracks": [t["id"] for t in val]}
    (M4 / "student.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
    print(f"best at step {best[2]}: {best[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
