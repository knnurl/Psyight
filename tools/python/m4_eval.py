"""Score the M4 student on a labelled track and measure its inference latency (SPEC 6.9, M4 exit).

Usage, from the repository root (after m4_train.py):
    tools/python/.venv/Scripts/python tools/python/m4_eval.py data/OculusDrift.mp3

Computes the track's student features with psyight_teacher (from the mix, as live mode would
hear it), runs data/m4/student.onnx over them, decodes bass notes with m4_decode.py and the
settings m4_tune.py chose on the validation tracks, writes them as a labels-format file, and scores them against the track's
labels with psyight_eval --notes, the same scoring as the DSP tracker. Then times one inference
over the model's receptive field (what a live, per-hop call costs) with onnxruntime on the CPU
and on DirectML.
"""

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
import time

import numpy as np
import onnxruntime as ort

import m4_decode

ROOT = pathlib.Path(__file__).resolve().parents[2]
M4 = ROOT / "data" / "m4"
RELEASE = ROOT / "app" / "build" / "windows-msvc" / "release"


def read(base: pathlib.Path, name: str) -> tuple[np.ndarray, dict]:
    meta = json.loads(base.with_suffix(".json").read_text(encoding="utf-8"))
    a = meta["arrays"][name]
    raw = base.with_suffix(".bin").read_bytes()
    out = np.frombuffer(raw, dtype=np.dtype(a["dtype"]), count=int(np.prod(a["shape"])),
                        offset=a["offset"]).reshape(a["shape"])
    return out, meta


def decoded_notes(binary: np.ndarray, pitch: np.ndarray, info: dict,
                  frame_s: float) -> list[dict]:
    notes = m4_decode.notes(binary, pitch, info["lookahead"], info["first_midi"],
                            info.get("decoder"))
    return [{"onset_s": k * frame_s, "offset_s": end * frame_s, "midi": midi, "cents": 0.0,
             "slide_in": False, "slide_out": False, "velocity": 1.0}
            for k, end, midi in notes]


def latency_ms(model: pathlib.Path, dims: int, frames: int,
               provider: str) -> tuple[float, float] | None:
    if provider not in ort.get_available_providers():
        return None
    session = ort.InferenceSession(str(model), providers=[provider])
    x = np.random.default_rng(1).uniform(-1, 1, (1, dims, frames)).astype(np.float32)
    for _ in range(20):
        session.run(None, {"features": x})
    times = []
    for _ in range(300):
        start = time.perf_counter()
        session.run(None, {"features": x})
        times.append(1e3 * (time.perf_counter() - start))
    return float(np.median(times)), float(np.percentile(times, 99))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("audio", type=pathlib.Path)
    args = parser.parse_args()
    info = json.loads((M4 / "student.json").read_text(encoding="utf-8"))
    model = M4 / "student.onnx"
    with tempfile.TemporaryDirectory() as tmp:
        base = pathlib.Path(tmp) / "features"
        subprocess.run([str(RELEASE / "psyight_teacher.exe"), str(args.audio), str(base)],
                       check=True, capture_output=True)
        features, meta = read(base, "features")
        x = (features.astype(np.float32) * meta["features_scale"]).T[None]
        session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        binary, pitch = session.run(None, {"features": x})
        frame_s = meta["hop_samples"] / meta["sample_rate_hz"]
        notes = decoded_notes(binary[0], pitch[0], info, frame_s)
        labels_path = args.audio.with_name(args.audio.name + ".labels.json")
        labels = json.loads(labels_path.read_text(encoding="utf-8"))
        estimates = {"schema": 1, "audio": labels["audio"], "sample_rate": labels["sample_rate"],
                     "grid": labels["grid"], "bass_notes": notes}
        out = pathlib.Path(tmp) / "student.labels.json"
        out.write_text(json.dumps(estimates), encoding="utf-8")
        print(f"student (lookahead {info['lookahead']} frames), decoder "
              f"{info.get('decoder', m4_decode.DEFAULTS)}")
        result = subprocess.run([str(RELEASE / "psyight_eval.exe"), str(args.audio),
                                 "--notes", str(out)], capture_output=True, text=True)
        print(result.stdout + result.stderr, end="")
    for provider in ("CPUExecutionProvider", "DmlExecutionProvider"):
        timing = latency_ms(model, info["dims"], info["receptive_field"], provider)
        if timing is None:
            print(f"{provider}: not available")
        else:
            print(f"{provider}: one inference over {info['receptive_field']} frames: median "
                  f"{timing[0]:.2f} ms, p99 {timing[1]:.2f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
