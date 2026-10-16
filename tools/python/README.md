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
