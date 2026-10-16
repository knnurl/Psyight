# Offline Python tooling

Offline helpers only. The app is C++ and links nothing from here (CLAUDE.md licensing rule). Private audio, stems and weights stay in `data/`, which is gitignored.

## Setup

From the repository root:

```
python -m venv tools/python/.venv
tools/python/.venv/Scripts/python -m pip install -r tools/python/requirements.txt --extra-index-url https://download.pytorch.org/whl/cu124
```

Python 3.10 on Windows was used. The CUDA 12.4 build needs an NVIDIA driver that reports CUDA 12.4 or later (`nvidia-smi`).

## Stems (`separate.py`)

Splits tracks into Demucs v4 (htdemucs) stems: drums, bass, other and vocals.

```
tools/python/.venv/Scripts/python tools/python/separate.py data/OculusDrift.mp3 "data/Phase Change.mp3"
```

Each track's stems go to `data/stems/htdemucs/<track name>/<stem>.wav` at 44.1 kHz. That is the layout the app expects. Tracks whose stems already exist are skipped.

Licence (SPEC 18 A6): the Demucs code is MIT. The upstream repository was archived on 1 January 2025, with a maintained fork at github.com/adefossez/demucs that takes only important bug fixes. The licence of the pretrained weights is not stated, so stems are for analysis only and the weights are never shipped.

## M4 student (`m4_dataset.py`, `m4_train.py`)

Builds the training set from a music library and trains the causal student (SPEC 6.9). Needs a Release build of the app (for `psyight_teacher.exe`).

```
tools/python/.venv/Scripts/python tools/python/m4_dataset.py fingerprint <folder> [more folders]
tools/python/.venv/Scripts/python tools/python/m4_dataset.py plan
tools/python/.venv/Scripts/python tools/python/m4_dataset.py export
tools/python/.venv/Scripts/python tools/python/m4_train.py
```

`fingerprint` and `plan` find duplicates and every copy of the four reference tracks (evaluation tracks are never trained on) and write `data/m4/manifest.json`. `export` separates one track at a time in `data/m4/work/`, writes its teacher data to `data/m4/tracks/` (about 16 MB a track) and deletes the stems, which would take about 0.37 GB a track. Source folders are only read. Training writes `data/m4/student.{pt,onnx,json}`. All of `data/` is gitignored.
