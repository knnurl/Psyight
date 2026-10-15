# CLAUDE.md

Instructions for Claude Code in this repository. Read this file fully before writing any code.

## What this project is

A Windows C++ application that listens to music, detects musical events with high accuracy, and turns them into LED pixel shows. The initial genre is dark psytrance at around 160 BPM. The detectors cover kick, beat, bar, phrase and section, with the bassline getting the most attention: onset, duration, gate length, decay, pitch, octave, slides and rests. Output goes over sACN or Art-Net to a custom STM32F767 pixel node driving WS2812-class strips. Home testing comes first, event production later.

The project is named **Psyight** (the design conversation used the working name PsyLight).

The owner is Kaan Ural: a robotics and embedded engineer working in C/C++, Python, ROS 2, STM32, KiCad and control systems. Treat him as an expert. Be direct, skip the basics, and flag uncertainty explicitly.

## Read these first

1. `docs/SPEC.md`: the full specification and the source of truth for requirements and design.
2. `docs/CONVERSATION_EXPORT.md`: the design conversation that produced the spec. It includes the owner's answers to 37 scoping questions.
3. `prototypes/`: two browser prototypes, a bass reaction simulator and the colour and motion modes. Use them as behavioural references, not code to port line by line.

Where `SPEC.md` and this file disagree, `SPEC.md` wins on requirements and this file wins on process and conventions.

## Priorities (owner's ranking, highest first)

1. Detection accuracy
2. Visual creativity
3. Reliability
4. Ease of live operation
5. Low latency

When trade-offs conflict, follow this order and state which priority drove the decision.

## Current milestone: M1, "Bass roll with ground truth"

**Goal:** "Occulus Drift" by Lemurians (a local file) plays inside the app. A bass roll shows every detected bass note's onset, duration, gate ratio and pitch on a 16th-note grid, scored against about 60 seconds of ground truth the owner labels by hand in the app.

**Done when:**
- [ ] The app builds from a clean checkout with the documented commands, and launches a docking Dear ImGui window.
- [ ] It loads WAV, FLAC and MP3. The transport supports play, pause, seek and loop region. Playback goes through WASAPI.
- [ ] A scrolling log-frequency spectrogram and band-energy plots follow the playhead.
- [ ] The grid is manual: a BPM field (default 160) plus a draggable first downbeat. Automatic beat tracking is M2.
- [ ] Bass tracker v0 (DSP, see SPEC section 6.3) outputs onset, offset, duration, gate ratio, pitch (MIDI and cents), decay time constant, slide flags, long-note flag, velocity and confidence for every note.
- [ ] The bass roll view shows detected notes and the ground-truth overlay. Clicking a note opens an inspector with all its fields.
- [ ] The labelling tools work: create, move, resize and delete ground-truth notes, snap to 1/16 or free, and set pitch. Labels save to and load from `<audio>.labels.json` (schema in SPEC 6.10).
- [ ] The scoring panel shows onset precision, recall and F at ±20 ms, the offset error distribution, pitch accuracy within ±50 cents (octave errors counted separately) and gate-ratio MAE, per labelled region.
- [ ] A synthetic test-signal generator (SPEC 6.11) and unit tests for every DSP block are in place. Runs are deterministic: the same input and parameters give bit-identical results, enforced by a test.

**Out of scope for M1:** LED output, node firmware, ML, live input, automatic beat tracking, scenes and effects. Don't build ahead.

## Tech stack (don't change without asking)

- C++20, CMake 3.25 or later, MSVC (Visual Studio 2022), Windows 10/11 x64. Keep platform-specific code behind interfaces so other platforms stay possible.
- Dependencies come through vcpkg manifest mode (`vcpkg.json`).
- GUI: Dear ImGui (docking branch) and ImPlot, on a GLFW plus OpenGL 3 backend.
- Audio: miniaudio for file decoding and WASAPI playback, loopback and capture.
- FFT: pffft, with KissFFT as an acceptable fallback.
- JSON: nlohmann/json. Logging: spdlog. Tests: doctest. MIDI (M5): RtMidi.
- ML inference (M4): ONNX Runtime with the DirectML execution provider and a CPU fallback.
- Offline tooling only (Demucs separation, model training, metric cross-checks with mir_eval): Python 3 in `tools/python/`. The application itself is C++ only.

## Licensing rule

The owner hasn't decided whether this will be personal, open source or commercial, so keep every option open:
- **Allowed in the app:** MIT, BSD, zlib, Apache-2.0, BSL-1.0, ISC, public domain and MIT-0.
- **Not allowed in the app without asking:** GPL, LGPL, AGPL, JUCE, Qt, or anything with a non-commercial clause.
- Python tooling may use any OSI licence, but nothing from it gets linked into the app.
- Never commit audio, stems, labels for private tracks, or model weights trained on private music. `data/` is gitignored.

## Architecture rules (summary; details in SPEC sections 4 and 5)

- **Master clock:** the audio sample position. Every feature, event and output frame carries a sample timestamp. Never use wall-clock time for musical logic.
- **Threads:**
  - Audio I/O thread: no allocation, locks, file I/O or logging.
  - Analysis thread: DSP now, ML later.
  - Engine thread: fixed-rate render.
  - Output thread: network.
  - GUI thread.
- **Communication:** single-producer single-consumer lock-free queues and double-buffered snapshots. The GUI only reads snapshots and never blocks another thread.
- **Determinism:** analysis and engine output are pure functions of input audio, parameters and seed. This is what makes session replay and A/B comparison trustworthy.
- **Parameter registry:** every tunable value is declared once, with id, label, unit, range, default, depth tier (Operate, Tune, Engineer), modulatable flag, MIDI-learnable flag and description. The GUI generates controls from the registry, and presets serialise by id.
- **File mode versus live mode:** file mode may analyse the whole track ahead of playback (non-causal). Live mode must be causal. The two share detector interfaces, so the GUI can A/B them on the same audio.

## Code conventions

- Use `.clang-format` (LLVM base, 4-space indent, 100 columns) and treat warnings as errors (`/W4 /permissive-`).
- Naming:
  - Types are `PascalCase`.
  - Functions and variables are `snake_case`.
  - Constants are `kPascalCase`.
  - Private members end in `_`.
  - Files are `snake_case`.
- Put units in names: `_ms`, `_hz`, `_samples`, `_db`, `_s`.
- No exceptions or allocation on real-time paths. Use a small `Result<T>` type for fallible calls.
- Each DSP block is a class with `prepare(sample_rate, max_block)`, `reset()` and `process(...)`. Its header comment lists its assumptions and latency in samples.

## Testing

- Unit-test every DSP block against the synthetic generator: kick plus saw bass at 160 BPM, with known gate ratios, decays, slides, sidechain ducking and rests. The ground truth is exact, so tests assert numeric tolerances.
- Regression test: run the full analysis on the synthetic set and compare against stored golden metrics. A metric that gets worse fails the test.
- Reference tracks are private and local. Score them manually through the app's scoring panel, never in CI.

## Writing conventions (docs, UI text, comments, commit messages)

- British English: colour, behaviour, analyse.
- Never use em dashes. Use colons, commas or full stops instead.
- Use sentence case for UI labels.
- Mark unverified technical claims in docs with `[Verify]`.

## How to work

- Before starting a task, restate the acceptance criteria you're targeting. List any items from SPEC section 18 ("Assumptions to verify") the task depends on.
- Make small, reviewable commits, one concern per commit.
- Ask before adding a dependency, changing the stack, or going beyond the current milestone.
- Measure, don't guess. Every claim about latency, CPU or accuracy needs a measurement in this repo and the command that reproduces it.
- Keep `SPEC.md` current. When a decision changes, update the spec in the same commit and add an entry to its decision log (section 16).
- When detection quality is in question, build the view or metric that shows the problem before tuning any thresholds.

## Commands

Run these from `app/` in an x64 Visual Studio 2022 developer shell: the "x64 Native Tools Command Prompt for VS 2022", or PowerShell after `& "<VS 2022 dir>\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64`. The default Developer PowerShell targets x86, which the build rejects. The shell sets `VCPKG_ROOT` to the vcpkg bundled with Visual Studio, and the build makes vcpkg compile the dependencies with that same Visual Studio instance. A Visual Studio 2026 18.0 shell fails: its bundled vcpkg (2025-09-03) doesn't recognise the v145 toolset. Keep this section accurate.
- Configure: `cmake --preset windows-msvc`
- Build: `cmake --build --preset windows-msvc-release`
- Test: `ctest --preset windows-msvc-release`
- Run: `build\windows-msvc\release\psyight.exe`

## Repository layout (target)

```
CLAUDE.md
docs/SPEC.md
docs/CONVERSATION_EXPORT.md
prototypes/                 browser prototypes from the design chat
app/
  CMakeLists.txt  CMakePresets.json  vcpkg.json
  src/core/                 timebase, queues, snapshots, params registry, Result
  src/audio/                miniaudio wrapper: file decode, playback, loopback, capture
  src/analysis/             stft, filterbank, onset, kick, hats, beat, bass, residual, sections
  src/ml/                   ONNX Runtime wrapper (M4)
  src/engine/               pixel model, layers, effects, modulation, palettes, scenes
  src/output/               sACN, Art-Net, sync, limiters, power budget, virtual node
  src/gui/                  panels, workspaces, plots, bass roll, labelling
  src/io/                   project, rig, presets, labels, session recorder
  tests/                    doctest suites, synthetic generator, golden metrics
tools/python/               offline: Demucs, label helpers, mir_eval checks, training (M2, M4)
firmware/node-f767/         STM32F767 Art-Net/sACN pixel node (parallel track)
data/                       gitignored: private audio, stems, labels, models
```
