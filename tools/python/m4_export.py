"""Export the M4 student for the app's own inference (app/src/ml/student.h), and its test fixture.

Usage, from the repository root:
    tools/python/.venv/Scripts/python tools/python/m4_export.py weights
    tools/python/.venv/Scripts/python tools/python/m4_export.py fixture

weights: data/m4/student.pt and student.json (with the decoder settings m4_tune.py chose) to
    data/m4/student.weights.{json,bin}, the file the app loads. Trained on private music, so it
    stays in data/ (gitignored).
fixture: a small student with random weights (8 channels, the real input size and dilations),
    exported the same way, plus a random input, onnxruntime's outputs for it and m4_decode.py's
    notes from those outputs, to app/tests/data/student_fixture.{json,bin}. Nothing in it comes
    from music, so it is committed; app/tests/student_test.cpp checks the C++ against it.

Weights file: a JSON header naming float32 arrays in a .bin (offset and shape), as the teacher
files. Batch norm (fixed at inference) is folded into the convolution before it. Matrices are
stored input-major ([in][out], and [tap][in][out] for the dilated convolutions, tap 0 the oldest
frame), the order the C++ accumulates in.
"""

import argparse
import json
import pathlib

import numpy as np
import torch
import torch.nn.functional as F

import m4_decode
from m4_train import FIRST_MIDI, PITCHES, Student

ROOT = pathlib.Path(__file__).resolve().parents[2]
M4 = ROOT / "data" / "m4"
FIXTURE = ROOT / "app" / "tests" / "data" / "student_fixture"
BINARY_HEADS = ["kick_onset", "hat_onset", "bass_onset", "bass_gate", "residual_activity"]
FEATURES_SCALE = 1.0 / 127.0


def folded(model: Student) -> dict[str, np.ndarray]:
    out = {}
    w = model.inp.weight.detach().double()[:, :, 0]  # (out, in)
    out["input_w"] = w.T
    out["input_b"] = model.inp.bias.detach().double()
    for layer, (conv, norm) in enumerate(zip(model.convs, model.norms)):
        scale = norm.weight.detach().double() / torch.sqrt(norm.running_var.double() + norm.eps)
        w = conv.weight.detach().double() * scale[:, None, None]  # (out, in, tap)
        b = (conv.bias.detach().double() - norm.running_mean.double()) * scale + \
            norm.bias.detach().double()
        out[f"conv{layer}_w"] = w.permute(2, 1, 0)  # (tap, in, out)
        out[f"conv{layer}_b"] = b
    out["binary_w"] = model.binary.weight.detach().double()[:, :, 0].T
    out["binary_b"] = model.binary.bias.detach().double()
    out["pitch_w"] = model.pitch.weight.detach().double()[:, :, 0].T
    out["pitch_b"] = model.pitch.bias.detach().double()
    return {k: v.numpy().astype(np.float32) for k, v in out.items()}


def write(base: pathlib.Path, header: dict, arrays: dict[str, np.ndarray]) -> None:
    """Writes <base>.json and <base>.bin (appended names: with_suffix would eat ".weights")."""
    header = dict(header, arrays={})
    blob = bytearray()
    for name, a in arrays.items():
        a = np.ascontiguousarray(a)
        header["arrays"][name] = {"dtype": a.dtype.name, "shape": list(a.shape),
                                  "offset": len(blob)}
        blob += a.tobytes()
    base.parent.mkdir(parents=True, exist_ok=True)
    base.parent.joinpath(base.name + ".json").write_text(json.dumps(header, indent=1),
                                                         encoding="utf-8")
    base.parent.joinpath(base.name + ".bin").write_bytes(bytes(blob))


def header_of(model: Student, dims: int, lookahead: int, decoder: dict) -> dict:
    return {"format": "psyight-student", "version": 1, "dims": dims,
            "channels": model.inp.out_channels, "dilations": list(model.dilations),
            "lookahead": lookahead, "first_midi": FIRST_MIDI, "pitches": PITCHES,
            "binary_heads": BINARY_HEADS, "features_scale": FEATURES_SCALE,
            "decoder": dict(m4_decode.DEFAULTS, **decoder)}


def export_weights() -> int:
    info = json.loads((M4 / "student.json").read_text(encoding="utf-8"))
    if "decoder" not in info:
        print("student.json has no decoder settings: run m4_tune.py first")
        return 1
    model = Student(info["dims"], info["channels"])
    model.load_state_dict(torch.load(M4 / "student.pt", map_location="cpu", weights_only=True))
    model.eval()
    write(M4 / "student.weights", header_of(model, info["dims"], info["lookahead"],
                                            info["decoder"]), folded(model))
    print(f"wrote {M4 / 'student.weights.json'} and .bin")
    return 0


def export_fixture() -> int:
    import onnxruntime as ort

    torch.manual_seed(7)
    rng = np.random.default_rng(7)
    dims, frames, lookahead = 192, 400, 2
    model = Student(dims, channels=8)
    with torch.no_grad():  # Random batch norm statistics, so the folding is exercised.
        for norm in model.norms:
            norm.running_mean.uniform_(-0.5, 0.5)
            norm.running_var.uniform_(0.5, 2.0)
            norm.weight.uniform_(0.5, 1.5)
            norm.bias.uniform_(-0.2, 0.2)
        model.binary.bias.add_(torch.tensor([0.0, 0.0, 1.0, 1.0, 0.0]))  # Some notes to decode.
    model.eval()
    quantised = rng.integers(-127, 128, (frames, dims)).astype(np.int8)
    x = (quantised.astype(np.float32) * np.float32(FEATURES_SCALE)).T[None]
    onnx_path = FIXTURE.parent / (FIXTURE.name + ".onnx")
    FIXTURE.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(model, torch.zeros(1, dims, model.receptive_field()), onnx_path,
                      input_names=["features"], output_names=["binary", "pitch"],
                      dynamic_axes={"features": {2: "frames"}, "binary": {2: "frames"},
                                    "pitch": {2: "frames"}}, opset_version=17)
    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    binary, pitch = session.run(None, {"features": x})
    onnx_path.unlink()
    with torch.no_grad():  # Sanity: PyTorch agrees with onnxruntime.
        b_t, p_t = model(torch.as_tensor(x))
    assert np.abs(b_t.numpy() - binary).max() < 1e-4 and np.abs(p_t.numpy() - pitch).max() < 1e-4
    decoder = {"onset_threshold": 0.4, "gate_threshold": 0.3, "kick_mask_frames": 2}
    notes = m4_decode.notes(binary[0], pitch[0], lookahead, FIRST_MIDI, decoder)
    header = header_of(model, dims, lookahead, decoder)
    header["fixture"] = {"frames": frames, "notes": [list(n) for n in notes],
                         "note": "random weights and input: tests only"}
    arrays = folded(model)
    arrays["fixture_input"] = quantised
    arrays["fixture_binary"] = binary[0].T.copy()  # (frames, 5)
    arrays["fixture_pitch"] = pitch[0].T.copy()    # (frames, 44)
    write(FIXTURE, header, arrays)
    print(f"wrote {FIXTURE}.json and .bin: {len(notes)} decoded notes")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("what", choices=["weights", "fixture"])
    args = parser.parse_args()
    return export_weights() if args.what == "weights" else export_fixture()


if __name__ == "__main__":
    raise SystemExit(main())
