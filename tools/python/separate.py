"""Separate tracks into Demucs stems for the app's stems analysis source (SPEC 6.2, M2).

Usage, from the repository root:
    tools/python/.venv/Scripts/python tools/python/separate.py data/OculusDrift.mp3 [more audio...]

Writes data/stems/htdemucs/<track name>/{drums,bass,other,vocals}.wav next to the audio's folder,
the layout the app looks for (stems_dir_for in app/src/io/stems.h). Skips tracks whose stems all
exist. Uses the GPU when PyTorch sees one. Stems of private tracks stay in data/ (gitignored).
"""

import argparse
import pathlib
import subprocess
import sys

MODEL = "htdemucs"
STEMS = ("drums", "bass", "other", "vocals")


def stems_dir(audio: pathlib.Path) -> pathlib.Path:
    return audio.parent / "stems" / MODEL / audio.stem


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("audio", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    status = 0
    for audio in args.audio:
        out = stems_dir(audio)
        if all((out / f"{stem}.wav").exists() for stem in STEMS):
            print(f"{audio.name}: stems exist in {out}")
            continue
        print(f"{audio.name}: separating into {out}", flush=True)
        result = subprocess.run(
            [sys.executable, "-m", "demucs", "-n", MODEL, "-o", str(audio.parent / "stems"),
             str(audio)])
        status = status or result.returncode
    return status


if __name__ == "__main__":
    sys.exit(main())
