# Psyight

Psyight listens to music and turns it into LED pixel shows. It detects musical events with high accuracy and drives WS2812-class strips over sACN or Art-Net. It is built first for dark psytrance at around 150 to 160 BPM, where the rolling bassline matters as much as the kick: every bass note's onset, length, gate, decay, pitch and slide is tracked, not just the beat.

It is a Windows desktop application in C++20 with a Dear ImGui interface, plus firmware for a custom STM32F767 pixel node.

**Status:** a personal project under active development. The analysis, engine, output, live mode and operator tools all work in software. Real-LED and venue testing are still to come, so expect rough edges.

## What it does

**Analysis (file mode).** A whole track is analysed ahead of playback:
- tempo, beat grid, downbeats and 8-bar phrases;
- kicks and hats;
- the bassline as notes (onset, offset, gate ratio, pitch in MIDI and cents, decay, slides, velocity, confidence);
- sections (intro, groove, breakdown, build, drop, outro), each with an energy level, plus a riser curve through builds.

Optional offline Demucs stems improve every detector.

**Live mode.** A causal path for DJ sets, from WASAPI loopback or an audio interface:
- a small causal neural network (trained offline, run in plain C++ with no ML runtime) for kicks, hats and bass;
- a phase-locked loop for the beat, with prediction;
- the same section rules, applied causally.

**Show engine.**
- Effects run on rig geometry in metres: plasma, kick shockwave, bass conveyor, hat sparkle, residual shimmer, build compression, drop burst, chase and wave.
- Layers with blend modes and group masks.
- A modulation matrix: musical sources, LFOs, macros and MIDI; curves, smoothing, quantise.
- Palettes; scenes chosen per section with automation; presets and show packs.
- Rendering is deterministic: the same audio, settings and seed always give the same frames, so recordings replay exactly.

**Safety.**
- A flash limiter (at most 3 full-field flashes a second) is on by default.
- A brightness cap, a power budget and preflight checks.
- A state ladder (Preview, Ambient, React, Show), so nothing reaches the lights by accident.

**Output.**
- sACN (E1.31) and Art-Net with sync.
- A virtual node and test patterns.
- Node discovery.

**Operator tools.**
- An Ableton-style device rack for scenes.
- A Perform view, MIDI learn and a setup assistant.
- Undo for show edits.
- A bass roll with labelling and scoring against hand-labelled ground truth.

## Repository layout

```
app/                  the application (CMake, vcpkg manifest)
  src/analysis/       front end, beat tracker, bass tracker, sections, live analysis
  src/engine/         pixel model, effects, modulation, scenes, automation, limiters
  src/output/         sACN, Art-Net, test patterns
  src/gui/            panels, plots, bass roll, show rack
  tests/              doctest suites and a synthetic test-signal generator
firmware/node-f767/   STM32F767 Art-Net and sACN pixel node (8 strips, DMA driven)
tools/python/         offline only: Demucs separation, model training and export
prototypes/           browser prototypes from the design phase
docs/                 specification, operator guide, results, plans
```

## Building

You need:
- Windows 10 or 11, x64;
- Visual Studio 2022 with the C++ workload (its bundled vcpkg is used);
- CMake 3.25 or later.

Open the "x64 Native Tools Command Prompt for VS 2022" (or PowerShell after `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64`). Then, from `app/`:

```
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
build\windows-msvc\release\psyight.exe
```

The default Developer PowerShell targets x86, which the build rejects. A Visual Studio 2026 18.0 shell fails too: its bundled vcpkg doesn't know the v145 toolset.

To build a portable zip, run `cmake --build --preset windows-msvc-release --target package`. It writes `build\windows-msvc\Psyight-<version>-win64.zip`, which runs on any Windows 10 or 11 x64 machine.

The node firmware builds separately: see [firmware/node-f767/README.md](firmware/node-f767/README.md).

## Model weights and audio

No audio, stems, track labels or trained weights are in this repository. The live-mode network is trained offline on private music with `tools/python` and loaded from `data/m4/student.weights`. Without those weights, file mode works fully but live mode is unavailable.

## Documentation

- [docs/SPEC.md](docs/SPEC.md): requirements and design, with a decision log.
- [docs/OPERATOR_GUIDE.md](docs/OPERATOR_GUIDE.md): running a show.
- [docs/results/](docs/results/): measured accuracy, latency and soak results per milestone.
- [CLAUDE.md](CLAUDE.md): conventions for working on the code (written for an AI coding agent, useful for humans too).

## Third-party code

Libraries come through vcpkg or are vendored under permissive licences only: Dear ImGui, ImPlot, GLFW, miniaudio, pffft, nlohmann/json, spdlog, doctest, RtMidi, and Signalsmith Stretch. Their notices ship in the package's `licenses` folder. Fonts: Inter and JetBrains Mono (SIL Open Font License 1.1); icons: Lucide (ISC).

## Licence

No licence has been chosen yet. Until one is, all rights are reserved: you're welcome to read the code, but please ask before reusing it.
