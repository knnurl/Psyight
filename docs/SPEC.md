# Psyight specification

Version 0.1, 15 October 2026. Derived from the design conversation in `docs/CONVERSATION_EXPORT.md`.

Confidence tags used in this document:
- **[Certain]** Established fact or straightforward arithmetic.
- **[Likely]** Strong inference, expected to hold.
- **[Guessing]** Plausible but untested.
- **[Verify]** Must be checked before anything relies on it. Section 18 lists these.

---

## 1. Context and goals

The owner wants an audio-reactive LED system for psytrance. It starts as a home-testing setup and later moves into event production. The design rests on three decisions:

1. **The PC renders and the node is dumb.** The application analyses audio and renders every pixel; the node only receives sACN or Art-Net and drives strips. That keeps the node compatible with professional tools (Resolume, Madrix, lighting consoles) and avoids making the PC software a single point of failure.
2. **Accuracy is measured, not eyeballed.** Hand-labelled ground truth and metrics are built before effects.
3. **Variety comes from musical structure.** Colour and motion follow 16th, beat, bar, phrase and section, not raw energy alone.

**Initial musical target:** dark psytrance, about 160 BPM.

**Reference tracks** (titles as given by the owner [Verify] spelling):
- "Occulus Drift" by Lemurians
- "Phase Change" by Kabayun
- "Hall of Purity" by Varazslo
- "Hagios" by Depuratus

**Non-goals for now:** LED video walls, macOS and Linux builds, and DMX fixtures (planned later, see section 8).

---

## 2. Requirements

### 2.1 Owner decisions (from the 37 scoping questions)

| Topic | Decision |
|---|---|
| What "bass duration" means | All of them: gate length, decay shape, slides, long sustained notes. All customisable |
| Duration units | Customisable: absolute ms and relative to the 16th slot |
| Visual response to duration | Customisable |
| Per-note or aggregate | Both: per note, plus bar and phrase aggregates |
| Slides and glides | Detected as their own events; visual treatment customisable |
| Rests | Whatever sound remains in a rest is analysed and turned into light (residual layer) |
| Genre and tempo | Dark psy at 160 BPM first |
| Playback context | Both mixed DJ sets and single tracks |
| Audio source | Laptop first; later an option for the mixer booth output |
| Home testing | Both files and live playback |
| Stems | Full mix input; separating stems from the mix (ML, like DJ software) is desired |
| Latency tolerance | Unknown: "try and discover" |
| Offline versus live | Leaning live; file-mode pre-analysis also available |
| Automation | Fully automatic detection |
| Low confidence behaviour | Both options: hold last confident state, or fall back to ambient (selectable) |
| ML versus DSP | Leaning ML, with both available |
| Analysis debug view | Yes |
| What gets lit | LED strips first; later fixtures, 2D pieces and more |
| Pixel count | Under 1,500 pixels |
| Physical layout | Customisable (position-aware rig) |
| Scene selection | Both automatic and manual override |
| Colours to avoid | Customisable |
| UV decor and other lighting | Customisable |
| Operator | Depends on the day: owner, someone else, or unattended |
| Physical controls | Yes, MIDI |
| Sync with other systems | Customisable |
| Laptop crash behaviour | Node-side fallback show, customisable |
| OS | Windows for now |
| Language | Straight to C++ for the app (Python only for offline tooling) |
| GUI | From the start; comprehensive and intuitive |
| Commercial intent | Undecided, so keep licensing open |
| First milestone | Assistant's pick, accepted: M1 bass roll with ground truth |
| Deadline | None |
| Priorities | Accuracy, creativity, reliability, ease of live operation, latency |
| Time available | Undecided |

### 2.2 Non-functional requirements

- **Accuracy:** detection quality is measured against hand labels and synthetic ground truth. Targets get set after the M1 baseline. Placeholders are in section 15.
- **Latency:** an end-to-end audio-to-light latency of L ms, measured. A user latency offset is always available (section 4.3).
- **Frame rate:** the engine renders at 60 Hz by default, configurable from 30 to 120 Hz.
- **Scale:** up to 1,500 pixels at first, which is about 9 RGB universes and fits on one F767 node.
- **Reliability:** no audio dropouts under normal load. The GUI never blocks the audio or output threads. The node falls back cleanly when the PC stops sending.
- **Determinism:** the same audio, parameters and seed give bit-identical features and frames.
- **Platform:** Windows 10/11 x64, with platform-specific code behind interfaces.

---

## 3. System overview

```
 Audio sources                 Analysis                      Engine                   Output
 ┌──────────────┐   ring    ┌─────────────────────┐ feature ┌──────────────────┐ frames ┌──────────────┐
 │ File player  │──buffer──▶│ Front end (STFT,    │──bus───▶│ Rig / pixel model│──────▶ │ Limiters     │
 │ WASAPI loop- │           │ filterbank, CQT)    │ (time-  │ Layers + effects │        │ Patch        │
 │ back capture │           │ Detectors: kick,    │ stamped)│ Modulation matrix│        │ sACN/Art-Net │──▶ F767 node(s)
 │ Interface in │           │ hats, beat, bass,   │         │ Palettes, scenes │        │ Sync         │──▶ Virtual node
 └──────────────┘           │ residual, sections  │         │ Scene automation │        └──────────────┘    (preview)
                            │ ML path (ONNX)      │         └──────────────────┘
                            └─────────────────────┘
        ▲                              ▲                            ▲                         ▲
        └──────────── GUI (reads snapshots; edits params through registry) ───────────────────┘
                               Session recorder taps audio, features, params, frames
```

---

## 4. Timebase, threading and data flow

### 4.1 Clock

- **[Certain]** The master clock is the audio sample index: the file playback position in file mode, or the capture sample counter in live mode. The sample rate is 48 kHz by default.
- Every analysis frame, feature, event and output frame carries a sample timestamp. Musical logic never reads wall-clock time.
- Live-mode features are timestamped with the source sample time of the audio they describe, not the time they were computed. The difference between the two is the analysis latency, which is measured and shown in the GUI.

### 4.2 Threads

| Thread | Work | Rules |
|---|---|---|
| Audio I/O | miniaudio callback: playback or capture, writes to an SPSC ring | No allocation, locks, logging or file I/O |
| Analysis | Pulls hops, runs the front end and detectors (later ML), publishes timestamped features | Allocates only during `prepare()` |
| Engine | Fixed-rate render: reads features for the render time, writes a frame | Deterministic; no I/O |
| Output | Limiters, patch, packetising, sockets, sync | Never waits on the GUI |
| GUI | ImGui frame, reads double-buffered snapshots, edits params through the registry | Never blocks other threads |
| Recorder | Optional: writes audio, features, params and frames to disk | Has its own queue; drops with a counter rather than blocking |

### 4.3 Render time and latency offset

- **File mode:** render time is the playhead time plus the offset. Features are precomputed, so effects may read events slightly ahead, giving zero perceived analysis latency.
- **Live mode:** render time is the capture time plus the offset. Events become available only after the analysis latency L. A beat-phase predictor (section 6.5) lets the engine schedule expected 16ths ahead of detection and use detection to confirm or correct them.
- **Latency offset:** a user setting in ms that can be negative. It includes a helper that adds speaker-to-audience distance at about 2.9 ms per metre. **[Certain]** Sound travels at about 343 m/s. **[Likely]** People notice sound leading picture by more than about 45 ms (ITU-R BT.1359). Light that lags the electronic audio by 30 to 60 ms can therefore look in sync on a dance floor 10 to 20 m from the speakers.

---

## 5. Audio input

| Source | Milestone | Notes |
|---|---|---|
| File player (WAV, FLAC, MP3) | M1 | Transport, loop region, seek. Decoded and resampled to 48 kHz |
| WASAPI loopback (laptop audio) | M5 | Captures whatever the laptop plays (DJ software, browser) |
| WASAPI capture from an audio interface | M5 | Mixer booth output, line level. Exclusive mode is optional for lower latency |
| ASIO | Later, optional | Only if WASAPI latency proves insufficient. Check SDK licensing first [Verify] |

- Stereo is summed to mono for analysis by default; per-channel analysis is optional later.
- The input gain stage shows peak and RMS meters and warns on clipping.
- The input path reports device buffer size and estimated device latency, shown as a status chip.

---

## 6. Analysis

### 6.1 Front end

- 48 kHz with a 256-sample hop (5.33 ms) as the analysis frame clock.
- Multi-resolution STFT: windows of 1024 samples (21 ms) for timing, and 2048 and 4096 samples for pitch and display. In file mode windows are centred; in live mode they're causal.
- An IIR band-pass filterbank with configurable bands and envelope followers (separate attack and release) gives low-latency timing.
- Log-frequency spectrogram for display. CQT is optional in M2.
- **[Certain]** At 48 kHz with a 1024-sample window, FFT bins are 46.9 Hz wide, so a 50 to 100 Hz kick covers only 1 or 2 bins. Low-band timing therefore relies on the filterbank, and low-band pitch on longer windows and harmonics.

### 6.2 Feature catalogue

| Feature | Output | Milestone |
|---|---|---|
| Band energies | Per configurable band, linear and dB, auto-gain normalised | M1 |
| Spectral flux | Overall and per band | M1 |
| Bass notes | See 6.3 | M1 (DSP v0) |
| Bass activity | Continuous gate envelope 0 to 1 | M1 |
| Kick | Onset time, strength, confidence | M1 basic, M2 full |
| Hats | Onset time, strength (8 to 12 kHz default) | M2 |
| Beat | Phase 0 to 1, period, beat index, confidence | M2 (manual grid in M1) |
| Bar and downbeat | Bar index, bar phase, downbeat confidence | M2 |
| Phrase | Phrase index and position (8 or 16 bars, configurable) | M2 |
| Residual | Energy, spectral centroid, flux and flatness of non-bass content; mid and high energies | M2 |
| Section | Intro, groove, breakdown, build, drop or outro, with confidence | M2 |
| Stems | Kick, bass, other and vocals energies (offline Demucs in file mode; ML in live mode) | M2 file mode, M4 live |
| Bass aggregates | Per bar and phrase: mean and variance of gate ratio, slide count, rest count, note density, pitch range | M2 |

### 6.3 Bass note tracker

#### Physics constraints at 160 BPM

- **[Certain]** A 16th note (one slot) lasts 93.75 ms, and a beat lasts 375 ms.
- **[Certain]** A 40 to 60 Hz fundamental has a 17 to 25 ms period. Estimating pitch needs about 2 periods, so 35 to 50 ms, roughly half a slot.
- **[Likely]** Estimate pitch from harmonics (80 to 400 Hz), where it resolves faster, and track the envelope (and so duration) on the harmonics with about 5 ms resolution.
- **[Likely]** Sidechain compression ducks the bass on every kick. The tracker has to know beat and kick positions so ducking isn't read as a note ending.

#### Note record

`onset_s`, `offset_s`, `duration_ms`, `slot_len_ms`, `gate_ratio = duration / slot_len`, `pitch_midi`, `cents`, `octave`, `decay_tau_ms`, `slide_in`, `slide_out`, `is_long` (spans more than 1.5 slots with no re-attack), `velocity` (peak relative to local maximum), `confidence`, `slot_index` (position in bar, 0 to 15).

#### DSP v0 algorithm

This is the M1 baseline. Every threshold is a registry parameter, and ML is expected to beat it.

1. **Kick handling.** Detect kicks from a click-band onset (2.5 to 6 kHz) combined with a low-band rise. Build a kick mask window (default 0 to 60 ms after each kick) where bass offsets aren't declared and bass onsets are down-weighted.
2. **Bass envelope.** Combine the filterbank energy over the harmonic region (default 80 to 400 Hz) with sub energy (30 to 80 Hz). Use fast attack and short release followers.
3. **Onsets.** Use spectral flux in the bass region with adaptive-threshold peak picking. Optionally attract onsets to the grid within ±15 ms, but record the raw time too.
4. **Offsets.** A note ends at the first of: the envelope dropping below k × note peak (default k = 0.25, configurable); the noise floor; or the next onset. Drops inside the kick mask are ignored.
5. **Pitch.** File mode uses harmonic-sum spectrum or YIN on a low-passed signal with a 4096-sample centred window. Live mode uses a 2048-sample causal window weighted towards harmonics. Output MIDI, cents and confidence.
6. **Derived values.**
   - Gate ratio.
   - Decay time constant, from an exponential fit between the peak and the offset.
   - Slide: continuous pitch across a boundary with no envelope dip deeper than X dB.
   - Long note: no re-attack at the next slot.
   - Rests: a slot with no onset and activity below a threshold.
7. **Aggregates.** Per bar and per phrase statistics (section 6.2).

#### Rests and residual

When a slot is a rest, the residual features (non-bass content) drive the residual layer, so whatever sound remains still produces light. This was an explicit owner requirement.

### 6.4 Kick, hats and residual

- **Kick:** click-band onset plus low-band rise. In M2, add a BPM-aware minimum inter-onset gap and expected-position weighting from the beat tracker.
- **Hats:** 8 to 12 kHz onsets with a minimum gap, used for sparkle and detail layers.
- **Residual:** content outside the bass and kick bands: centroid, flux, flatness, energy. With stems available, use the "other" stem instead.

### 6.5 Beat, bar and phrase

- **File mode:** offline tempo estimation constrained to a prior (default 150 to 170 BPM, centred on 160), then dynamic-programming beat tracking (Ellis 2007 style). Downbeats come from bar-level patterns: bass note changes, crashes, phrase-level energy changes.
- **Live mode:** a phase-locked loop on kick onsets. **[Likely]** This suits a control engineer's toolset: kick onsets feed the phase detector, and the loop filter gains become registry parameters. Lock state and confidence are exposed. Manual tap and nudge controls are always available.
- **Phrase:** counted from the downbeat in groups of 8 or 16 bars, configurable. It resets on section changes.
- **Prediction:** the PLL provides the next expected 16th times, so the engine can schedule ahead in live mode.

### 6.6 Section detection

- **Breakdown:** no kick for N beats (default 4).
- **Build:** kick absent or sparse, with high-band energy and spectral flatness rising (risers, noise sweeps).
- **Drop:** **[Likely]** the kick returning after 8 or more bars without one is a reliable drop detector.
- **Groove:** a stable kick and bass pattern.
- Outputs carry a label, a confidence and a timestamp. Scene automation (section 7.6) consumes them.

### 6.7 Confidence and fallback

Every detector outputs confidence. When confidence drops below a threshold for a configurable time, the engine applies the selected policy: **hold** the last confident state, or **fall back to ambient**. The owner wants both available.

### 6.8 File mode versus live mode

| | File mode | Live mode |
|---|---|---|
| Source | Local files played by the app | Loopback or interface input |
| Causality | May look ahead (full-track pre-analysis) | Strictly causal |
| Separation | Offline Demucs stems (best quality) | ML student model (M4) or DSP only |
| Perceived latency | Zero (events known ahead) | L ms analysis latency, hidden partly by prediction and offset |
| Use | Tuning, labelling, scoring, prepared sets | Real gigs, DJ sets, unknown music |

**[Likely]** DJ software with stems can analyse ahead because it owns the playback. A mixer feed gives no lookahead, which is why live mode needs a causal student model.

### 6.9 ML plan (M4)

1. **Teacher.** Separate the owner's dark psy library offline with Demucs v4 (HTDemucs) into drums, bass, other and vocals. Run it from `tools/python/`; the GPU is optional but faster. **[Likely]** The Demucs code is MIT licensed [Verify] (repository status and weights licence).
2. **Labels from the teacher.**
   - From the bass stem: note onsets, offsets and pitch (pYIN or similar).
   - From the drums stem: kick and hat onsets, from band-limited onset detection.
   - From hand labels: corrections on the evaluation set only. Evaluation tracks are never used for training.
3. **Student.** A small causal CRNN or TCN on log-mel or CQT frames (hop 5.33 ms), with about 0.5 to 1 s of past context and 0 to 2 frames of lookahead (configurable; adds latency). Its heads:
   - kick onset probability
   - hat onset probability
   - bass activity (gate) probability
   - bass onset probability
   - bass pitch (MIDI 24 to 60 classification, or pitch class plus octave)
   - residual activity
   - optional bass envelope regression
4. **Data.** **[Guessing]** 50 to 200 tracks from the owner's library. Augmentations:
   - gain and EQ tilt
   - synthetic DJ transitions: two tracks crossfaded over 16 to 64 bars
   - tempo stretch of ±5%
   - pitch shift of ±2 semitones, with labels transformed to match
   - MP3 re-encoding
   - mild room or PA colouration
5. **Export.** ONNX, run through ONNX Runtime with DirectML on Windows and a CPU fallback. **[Guessing]** Expect a few ms per frame on a laptop GPU [Verify] by measuring.
6. **Evaluation.** Use the same scoring panel and labels as DSP. The GUI A/Bs DSP against ML on identical audio.
7. **Data hygiene.** Audio, stems and weights trained on private music never enter the repo.

### 6.10 Labels and evaluation

**Labels file:** `<audio filename>.labels.json`, saved next to the audio in `data/`.

```json
{
  "schema": 1,
  "audio": "Occulus Drift.flac",
  "sample_rate": 48000,
  "grid": { "bpm": 160.0, "first_downbeat_s": 0.000 },
  "regions": [ { "start_s": 60.0, "end_s": 120.0, "complete": true } ],
  "kicks": [ { "t_s": 60.012 } ],
  "bass_notes": [
    { "onset_s": 60.105, "offset_s": 60.171, "midi": 29, "cents": 0,
      "slide_in": false, "slide_out": false, "velocity": 1.0 }
  ],
  "beats": [], "downbeats": [],
  "sections": [ { "start_s": 0.0, "label": "intro" } ]
}
```

A region marked `complete: true` means every event inside it is labelled, so missed detections in it count as errors.

**Metrics,** reported per labelled region and per track:
- Onset precision, recall and F-measure at ±20 ms (configurable). **[Likely]** The mir_eval default is ±50 ms, which is too loose for 94 ms slots.
- Offset error distribution (median, p90), plus a mir_eval-style offset hit rate (offset ratio 0.2, minimum tolerance 50 ms) [Verify] defaults.
- Pitch accuracy within ±50 cents, with octave errors counted separately.
- Gate-ratio mean absolute error.
- Beat F-measure and downbeat F-measure (M2).
- Section boundary accuracy within ±1 bar (M2).
- Measured latency in live mode (M5).

A Python cross-check script (`tools/python/eval_mir.py`) recomputes onset and note metrics with mir_eval to validate the C++ implementation.

### 6.11 Synthetic generator (tests and demos)

A C++ generator mirrors the browser prototypes (`prototypes/`). It produces:
- a kick: a sine sweep from 170 to 45 Hz over 110 ms with exponential decay, plus an optional noise click high-passed at 3 kHz
- a saw bass: low-passed with a filter envelope, with configurable gate ratio, decay, velocity, pitch sequence, slides (portamento), long notes and rests
- optional sidechain ducking on kick positions, hats, and mixing noise
- patterns: rolling KBBB, ghost note, octave jump, triplet gallop, offbeat double, offbeat K-B-, all at any BPM

It emits exact ground truth in the labels schema, which makes every DSP unit test numeric and deterministic.

---

## 7. Engine

### 7.1 Rig and pixel model

- Each pixel has an id, a position (x, y, z in metres), a strip id, an index along its strip, group tags, a colour order (GRB, RGB and so on), and gamma.
- Strips are built in the rig editor from a polyline plus pixel pitch (for example 60 per metre), count and direction. 2D and 3D layouts are first-class, so effects work by position, not strip index.
- **Groups:** tags such as `left`, `booth`, `canopy` and `mandala_1` are used for masks and per-group routing.
- **Patch:** pixels map to node, output, universe and channel (section 8). The patch is separate from the geometry.

### 7.2 Timescale hierarchy (core design principle)

| Timescale | Psytrance element | Visual job |
|---|---|---|
| 16th (about 10.7 per second at 160 BPM) | Rolling bass | Motion steps, never full-field brightness |
| Beat | Kick | Impact: shockwave, crest boost, edge flash |
| Bar | Bass note and root changes | Hue offset, direction flip |
| Phrase (8 to 16 bars) | Arrangement changes | Palette change |
| Section | Breakdown, build, drop | Scene change |

**[Certain]** At 160 BPM, 16ths arrive 10.67 times per second. Full-strip brightness on every 16th is therefore a strobe in the photosensitive range (section 7.7).

### 7.3 Layers and effects

A scene is a stack of layers. Each layer has a type, parameters, a blend mode (add, max, alpha, multiply, screen), opacity, and a group mask. An effect is a deterministic function of (pixel position, render time, beat phase, features, params, seed) that returns RGB.

| Layer type | Effects (initial library) |
|---|---|
| Ambient | Plasma (two sine fields at different speeds), moiré, slow noise flow, palette drift |
| Kick | Shockwave from an origin point or group, crest boost on waves, segment-edge flash |
| Bass | Conveyor stripes stepping one segment per note, note-history conveyor (direction flips per bar), chase comets, pitch-to-hue fill, duration-shaped pulses (length or trail tied to gate ratio and decay) |
| Hats | Sparkle (random pixels, short decay) |
| Residual | Centroid-driven shimmer, flux texture (drives light during rests) |
| Build/drop | Compression toward the centre with a speed ramp during builds, release burst on the drop |
| Modifiers | Feedback trails (decay plus outward shift each frame), mirror, kaleidoscope (2D), hue drift, chromatic offset |

**Duration-aware ideas** (owner requirement; mappings customisable):
- Gate ratio sets comet length or pulse width.
- Decay time constant sets the trail decay.
- Slides morph hue continuously instead of jumping.
- Long notes hold and breathe instead of stepping.
- Bar-level gate-ratio averages shift motion style between staccato and legato looks.

The two browser prototypes in `prototypes/` demonstrate shockwave plus conveyor, beat-phase waves with pitch to hue, feedback trails, note history, sparkle and the ambient fallback.

### 7.4 Modulation matrix

- A route is: **source**, then **transform**, then **target**, with an amount.
- **Sources:** any feature (6.2), beat, bar or phrase-synced LFOs, MIDI CC, constants, random (seeded).
- **Transforms:** gain, offset, threshold, curve (linear, exponential, logarithmic, S-curve), attack and release smoothing, sample-and-hold, quantise to grid, invert, range map.
- **Targets:** any registry parameter flagged as modulatable.
- Multiple routes to one target are summed, then clamped to range.
- The GUI shows modulation on each control as a ring or arc, and lists the routes affecting it on hover.

### 7.5 Colour system

- **Palettes:** named colour sets, including UV-friendly saturated violet and magenta sets that don't wash out UV decor.
- **Mapping functions** (all customisable):
  - bass pitch to hue: chromatic circle, default 28 degrees per semitone
  - spectral centroid to saturation or colour temperature: brighter, acid-filter openings move toward cyan or white; dark sounds toward violet
  - complementary hues for the kick and bass layers so they stay distinct
  - per-bar hue offset
  - per-phrase palette change, with slewed transitions along the shortest hue path
- **Default rule:** white is reserved for kicks and drops. Colours to avoid can be excluded per project.

### 7.6 Scenes and automation

- A scene is a layer stack, palette, modulation routes and a parameter snapshot, shown as a grid of scene cards like a clip launcher.
- **Automation rules:**
  - on a section change (breakdown to an ambient scene, build to a build scene, drop to a drop scene, chosen in rotation or at random among tagged scenes)
  - on a phrase boundary (rotate palette every N phrases)
  - on the low-confidence policy (section 6.7)
- Manual selection always overrides automation until released.
- **Transitions:** cut on the next downbeat, or crossfade over N beats.

### 7.7 Restraint and safety limiters

- **Contrast:** **[Likely]** keep 30 to 50% of pixels dark most of the time, because contrast is what makes hits land.
- **Flash limiter:** on by default. It caps full-field luminance transitions at 3 per second. **[Certain]** UK broadcast guidance (Ofcom) limits flashing to 3 per second because of photosensitive epilepsy risk. **[Likely]** Following it at events is good practice. This limiter is a safeguard, not a certification. It can only be disabled from the Engineer tier, with a persistent warning.
- **Brightness cap:** global, and per group.
- **Power budget:** estimate current per PSU from frame content, then scale brightness to stay under each PSU's limit. **[Likely]** That's about 20 mA per WS2812B channel at full, roughly 60 mA per pixel at full white [Verify] for the strip in use.
- Gamma correction applies per pixel type.

---

## 8. Output and network

- **sACN (E1.31):** UDP port 5568, multicast 239.255.hi.lo per universe, or unicast. Includes priority, sequence numbers, and a sync universe. **[Certain]**
- **Art-Net 4:** UDP port 6454, with ArtDmx, ArtSync, and ArtPoll/ArtPollReply for discovery. **[Certain]**
- **Universe sizing:** 510 channels carry 170 RGB pixels or 127 RGBW pixels. **[Certain]** The budget of 1,500 RGB pixels is about 9 universes.
- **Sending:** every frame is sent in full (not just changes), followed by a sync packet, so multiple nodes latch together without tearing.
- The output rate is tied to the engine frame rate (default 60 Hz).
- **Virtual node:** the same packets are looped back to an in-app virtual node that drives the preview. This also tests packetising.
- **Fault injection** (Engineer tier): packet loss %, jitter, node reboot, audio dropout.
- **Network:** **[Certain]** if sACN multicast passes through a switch, the switch needs IGMP snooping, or multicast floods the network.
- **DMX fixtures (later):** the same universes can feed DMX nodes or the F767's optional DMX512 output (section 12). Fixture profiles are a later milestone.
- **Sync with other systems** (customisable, later): MIDI clock in and out, OSC, and Ableton Link [Verify] licence before adding.

---

## 9. GUI

### 9.1 Principles

- **Three depths of control.** These answer "make everything customisable" without becoming a wall of sliders:
  - **Operate:** the state ladder, scenes, master intensity, blackout and hold, MIDI.
  - **Tune:** per-detector and per-layer presets, each exposing its 3 to 5 key parameters.
  - **Engineer:** every registry parameter, raw plots, A/B comparisons, fault injection.
  Each parameter declares its tier. A global tier switch filters every panel.
- **Workspaces** are saved docking layouts:
  - **Operate:** large preview, scene grid, master faders, ladder.
  - **Tune:** analysis plots, presets, modulation.
  - **Label:** bass roll editing, spectrogram, loop transport.
  - **Engineer:** everything.
- **Safety controls are fixed.** Blackout and Hold look are always visible in the top bar, in the same position, with hotkeys (`B` and `H`). They act immediately with no confirmation dialog. Space is play and pause.
- **Visible state.** Status chips show system state at a glance, and a single fault banner shows the highest-severity active problem. Clicking the banner opens details and the log.
- **Undo and A/B.** Every parameter edit is undoable. Presets can be A/B compared with one key.
- **Inspectability.** Hovering any value shows its unit, range, current modulation and source. Right-clicking any control offers MIDI learn, reset to default, and copy id.
- **Venue-ready.** Dark theme by default, colour-blind-safe plot palettes, large hit targets in Operate.

### 9.2 Inspirations

The FR3 operator panel was an example, not a template. Ideas borrowed from several tools:

| Source | Idea taken |
|---|---|
| DAWs (Ableton, Bitwig) | Device chains, macro knobs, modulators with visible rings, piano roll (bass roll) |
| Sonic Visualiser | Analysis layers stacked over audio, with editable annotation layers |
| Resolume | Layer stack and clip-style scene grid |
| Madrix | Patch editor and live pixel preview of the real rig |
| TouchDesigner | Every value inspectable live |
| Owner's FR3 panel | Status chips, single fault banner, state ladder, fixed stop control, mock with fault injection, record and replay, preflight |

### 9.3 Layout

- **Top bar:**
  - transport
  - status chips: audio source, buffer and latency, BPM and confidence, grid lock, analysis mode (DSP, ML or A/B), nodes online, output fps, CPU and GPU load, recording
  - the state ladder
  - Blackout and Hold, fixed at the right
- **State ladder:**
  - PREFLIGHT: checks running, output disabled
  - PREVIEW: engine runs, virtual node only
  - AMBIENT: output on, ambient layer only
  - REACT: reactive layers on, scenes manual
  - SHOW: full scene automation

### 9.4 Panels

| Panel | Contents | Milestone |
|---|---|---|
| Source and transport | File browser, waveform overview, loop region, input device picker, gain and meters | M1 |
| Spectrogram | Scrolling log-frequency, playhead, band overlays, zoom | M1 |
| Band plots | Band energies, flux, envelopes (ImPlot, scrolling) | M1 |
| Grid | BPM, first downbeat (draggable), grid visible on all time views; later automatic beat, downbeat and phrase display | M1 manual, M2 auto |
| Bass roll | Detected notes on the 16th grid: length is duration, height is pitch, opacity is confidence, markers flag slides and long notes. Ground-truth overlay in a second colour | M1 |
| Note inspector | All note-record fields, plus local envelope and pitch curves | M1 |
| Labelling tools | Create, move, resize, delete notes, snap, pitch set, region complete flag, kick and section marks | M1 |
| Scoring | Metrics per region and track, confusion details, jump to the worst errors | M1 |
| Detector presets | Per detector: preset picker plus tiered parameters | M1 (bass), M2 (all) |
| Residual view | Residual features over time, rests highlighted | M2 |
| Section timeline | Detected sections with confidence | M2 |
| Stems | Stem meters, with stem audition in file mode | M2 (Demucs), M4 (ML) |
| DSP and ML A/B | Side-by-side or overlaid detections with per-path metrics | M4 |
| Rig editor | Strips, positions, groups, patch, nodes | M3 |
| Preview | 2D rig render (3D later), with fps and power estimates | M3 |
| Layers and scenes | Layer stack, effect parameters, scene grid, palette editor | M3 |
| Modulation matrix | Route table plus curve editor | M3 |
| Limiters | Flash, brightness and power, with live status | M3 |
| Nodes | Discovery, status, packet stats, fallback configuration | M3 |
| MIDI | Device picker, learn mode, mapping table | M5 |
| Latency calibration | Offset slider, speaker-distance helper, tap test | M5 |
| Recorder | Record, replay and compare sessions | M3 |
| Preflight | Checklist (section 11) | M3 |
| Log | Filterable log with severity | M1 |

---

## 10. Parameters, presets and persistence

- **Registry:** each parameter has an id (stable string), label, unit, min, max, default, step, tier, modulatable flag, MIDI-learnable flag, description and group.
- **Project file** (`.psyproj`, JSON): rig, patch, scenes, palettes, modulation routes, detector presets, MIDI map, latency offset and limiter settings.
- **Presets** are JSON files keyed by parameter id. Unknown ids are ignored with a warning, so presets survive version changes.
- **Labels** follow section 6.10. Their schema version is stored in the file.
- Autosave runs every N minutes, plus on exit. The last N project versions are kept.

---

## 11. Recording, replay, virtual node and preflight

- **Session recorder:** a folder containing:
  - `audio.wav`
  - `features.bin` (with a JSON schema)
  - `frames.bin` (output frames)
  - `params.jsonl` (timestamped parameter changes)
  - `events.jsonl` (detections)
  - `meta.json` (versions, settings)
- **Replay** is deterministic and lets you re-run analysis with new parameters on the same audio and diff the results.
- **Virtual node:** an in-app sACN/Art-Net receiver that feeds the preview, with fault injection.
- **Preflight checks:**
  - audio device and sample rate
  - network interface selection
  - node discovery
  - patch consistency (no overlapping universes or channels)
  - power budget against PSU ratings
  - limiter state
  - latency offset set
  - recording disk space

---

## 12. Node firmware (STM32F767)

### 12.1 Platform choice

**F767 over H755.** **[Likely]** The H755's second core and its Ethernet memory rules (DMA can't reach DTCM; descriptors belong in D2 SRAM, set non-cacheable through the MPU) add setup risk for no gain at this data rate. The F767 is a single-core Cortex-M7 at 216 MHz with 512 KB SRAM and an Ethernet MAC. The Nucleo-F767ZI has a LAN8742 PHY and RJ45 onboard.

### 12.2 Design

- **[Certain]** The data rate is trivial: 8 universes is about 180 KB/s at 44 fps.
- **Memory placement:**
  - **[Likely]** Ethernet descriptors, receive buffers and pixel DMA buffers go in DTCM, which is DMA-reachable and uncached on F7 [Verify].
  - Alternatively, mark the SRAM1 region non-cacheable with the MPU.
- **Parallel output (8 strips, one GPIO port):**
  - TIM1 or TIM8 drives DMA2 with three streams writing to the port's BSRR: all pins high, then data bits low, then all pins low.
  - **[Likely]** Only DMA2 can reach AHB1 GPIO [Verify].
  - The CPU transposes each frame into 24 bytes per pixel, one bit per lane.
- **Networking:**
  - Art-Net is UDP port 6454. Reply to ArtPoll.
  - sACN is multicast 239.255.x.y. Enable `LWIP_IGMP` and join one group per universe.
  - Honour ArtSync and the sACN sync universe: latch the frame on sync.
- **Electrical:**
  - Use a 74HCT245 for 3.3 V to 5 V level shifting, with a 33 to 100 Ω series resistor at each output.
  - Fit fusing per output.
  - Long runs can use optional RS-485 differential drivers per output.
- **Robustness:**
  - independent watchdog
  - static IP with DHCP fallback
  - config stored in flash and set over the network: a small UDP config protocol, or Art-Net ArtAddress
  - status LED
- **Signal loss** is configurable after T ms: hold the last frame, fade, run the **node-side fallback show**, or blackout. The fallback show is a small built-in effect set (ambient plasma, slow palette drift) with parameters stored in flash. The owner requirement is node-side fallback, customisable.
- **DMX512 output (optional):** a UART plus an RS-485 transceiver per universe, to drive fixtures.

### 12.3 Limits

Frame time against pixels per output, for WS2812 (30 µs per pixel plus a reset of about 300 µs) with 8 parallel outputs. **[Certain]**

| Per output | Total | Frame time | Max fps |
|---|---|---|---|
| 170 px (1 universe) | 1,360 | 5.4 ms | ~185 |
| 340 px (2) | 2,720 | 10.5 ms | ~95 |
| 510 px (3) | 4,080 | 15.6 ms | ~64 |
| 680 px (4) | 5,440 | 20.7 ms | ~48 |
| 850 px (5) | 6,800 | 25.8 ms | ~39 |

- **[Likely]** Stay at 3 universes per output or fewer for sharp beat hits. At this project's budget (under 1,500 px, about 190 per output), one node runs at well over 100 fps.
- **[Likely]** Single-ended 5 V data is reliable over a few metres. For longer runs, mount the node near the tape or use RS-485 per output.
- **[Certain]** The parallel trick suits single-wire chips (WS2812, WS2815, SK6812). Clocked chips (APA102) need a clock lane too, which halves the outputs per port.

### 12.4 Bring-up order (firmware track)

- **F1:** one strip driven by timer DMA from a static buffer, with no network. Verify the timing on a scope.
- **F2:** LwIP receives Art-Net and sACN and prints universe and sequence numbers.
- **F3:** join the two, then add sync, watchdog, config storage, the signal-loss policy and the fallback show.

---

## 13. LED hardware notes

**5 V versus 12 V**
- **[Certain]** For the same power, 12 V carries 2.4 times less current. Voltage drop as a fraction of supply improves by about 5.8 times.
- **[Likely]** 12 V ICs hold their colour better under voltage sag, and less ground-return current means fewer data glitches.
- **[Certain]** WS2815 and GS8208 have a backup data line.
- **[Certain]** Cheap 12 V WS2811 tape groups 3 LEDs per IC, so check that each LED is its own pixel.

**Rule of thumb**
- **[Likely]** Use 5 V for short, dense runs, 12 V (WS2815) for runs of several metres, and 24 V for long architectural runs where pixels are grouped anyway.

**Always**
- Inject power every few metres and fuse each injection point.
- Use aluminium profiles with diffusers.
- Size PSUs from the power budget in section 7.7.

---

## 14. Event readiness

- **[Likely]** Mains-powered kit needs PAT testing, fusing per output, and a proper enclosure. Venues and production companies ask about these.
- The flash limiter stays on, with photosensitivity considered in show design.
- Preflight runs before doors open. Keep a spare node and spare PSU, and print a one-page operator guide.
- The node-side fallback show is tested by unplugging the laptop during preflight.

---

## 15. Milestones and acceptance criteria

Initial metric targets are placeholders, marked **[Guessing]**. Replace them with real numbers once the M1 baseline exists.

### M1: Bass roll with ground truth (current)
- **Scope:** app skeleton, file playback, spectrogram and band plots, manual grid, bass tracker v0 (DSP), bass roll with inspector, labelling tools, scoring panel, synthetic generator, unit and regression tests, log panel. The detailed checklist is in `CLAUDE.md`.
- **Exit:** about 60 s of "Occulus Drift" labelled, with a scored baseline recorded in `docs/results/M1.md`.
- **Placeholder targets [Guessing]:** onset F at least 0.90 at ±20 ms on the synthetic set, and at least 0.80 on the labelled region; gate-ratio MAE at most 0.10 on the synthetic set.

### M2: Full analysis on all four reference tracks
- Automatic beat, downbeat and phrase tracking (offline in file mode).
- Kick, hats, residual and section detectors, and bass aggregates.
- Labels for at least one region in each reference track.
- Offline Demucs stems loaded as an optional analysis source in file mode (Python tool writes them; the app reads them).
- Exit: metrics table for all four tracks, DSP against Demucs-assisted.

### M3: Engine and output
- Rig editor, pixel model, patch, layers and effect library (section 7.3), modulation matrix, palettes, scenes and automation, limiters, preview, virtual node, recorder, preflight.
- sACN and Art-Net output with sync to the F767 node (firmware F1 to F3 in parallel).
- Exit: a full reference track plays as a show on real strips at 60 fps or more, with the flash limiter verified.

### M4: ML student model
- Data pipeline from teacher stems, training, ONNX export, inference in the app, A/B against DSP in the GUI.
- Exit: the student matches or beats DSP on the labelled regions, with measured inference latency.

### M5: Live operation
- WASAPI loopback and interface capture, causal analysis path, PLL beat tracking with prediction.
- Latency measurement and calibration, MIDI learn and mapping, node-side fallback tested.
- Exit: a mixed DJ set from loopback or the mixer drives the rig with measured end-to-end latency and stable lock.

### M6: Event hardening
- Soak tests (4 hours or more), crash recovery, autosave, operator guide, spare-node workflow, PAT-ready hardware build, per-output fusing, enclosure.
- Exit: a dry run in a venue-like setup with the full preflight passed.

---

## 16. Decision log

| Date | Decision | Reason |
|---|---|---|
| 2026-10-15 | Build a custom node instead of buying an Enttec OCTO | Owner has the skills; the OCTO's value is support and certification, which isn't needed for home testing |
| 2026-10-15 | STM32F767 over STM32H755 for the node | Single core, simpler memory and cache, Nucleo board with PHY; the H755 adds risk for no gain |
| 2026-10-15 | The PC renders and the node is dumb | Compatible with pro tools; the software isn't a single point of failure; the node holds a fallback show |
| 2026-10-15 | No paid tools for now | Owner preference; free and open tools used for comparison only (LedFx, QLC+, xLights, TouchDesigner Non-Commercial) |
| 2026-10-15 | Structure-locked visuals | Variety from the 16th, beat, bar, phrase and section hierarchy, not raw energy |
| 2026-10-15 | App in C++ from the start; Python only for offline tooling | Owner decision (Q31) |
| 2026-10-15 | Dear ImGui and ImPlot, miniaudio, ONNX Runtime; no JUCE or Qt | Permissive licences keep commercial options open; ImGui suits dense real-time tool UIs |
| 2026-10-15 | Two analysis paths: file mode (lookahead, Demucs) and live mode (causal, student model) | A mixer feed gives no lookahead; file mode gives the best accuracy for tuning and labelling |
| 2026-10-15 | Three GUI depths: Operate, Tune, Engineer | Keeps "everything customisable" usable live |
| 2026-10-15 | First milestone is the bass roll with ground truth | Accuracy is priority one and needs a measurement before any effects |
| 2026-10-15 | Flash limiter on by default at 3 per second | Photosensitivity: rolling 16ths at 160 BPM are a 10.7 Hz strobe if mapped to brightness |
| 2026-10-15 | Build with CMake presets (Ninja Multi-Config) and vcpkg manifest mode, triplet `x64-windows-static-md`. The registry baseline is pinned in `app/vcpkg-configuration.json` to the snapshot bundled with Visual Studio (`b1b19307`, 2025-08-30). vcpkg compiles the dependencies with the developer shell's Visual Studio instance | Ninja uses whichever MSVC the shell provides and exports `compile_commands.json`. The pinned baseline makes clean checkouts reproducible and matches the bundled vcpkg's scripts. Static third-party libraries with the shared CRT leave no third-party DLLs to ship; the target machine needs the VC++ 2015-2022 x64 redistributable. One toolset for app and dependencies avoids static-library link mismatches |
| 2026-10-15 | Project name is Psyight (replaces the working name PsyLight) | Owner decision |
| 2026-10-15 | Labelling audition at 0.5x with pitch kept, using Signalsmith Stretch 1.4.0 and Signalsmith Linear 0.6.4 (MIT, header-only, vendored in `app/third_party`) | Owner chose it over writing WSOLA: much cleaner transients and sustained bass. Rubber Band (GPL) and SoundTouch (LGPL) are excluded by the licensing rule. Measured latency at 0.5x: 90 ms of input, compensated in the displayed playhead |

Superseded during the conversation: an early suggestion to use JUCE for the C++ engine (replaced by the permissive stack) and an early "prototype in Python first" plan (replaced by the owner's choice of C++ from the start).

---

## 17. Open questions

1. Commercial intent: personal, open source or product? This affects the licence and installer.
2. Hours per week available, and any first event date.
3. Rig specifics for the first real setup: strip lengths, layout, mounting, PSU locations.
4. Audio interface for mixer input (M5), and whether WASAPI latency is enough.
5. Show laptop specs, especially the GPU, which matters for DirectML.
6. RGBW support, and which strip types first (WS2812B at 5 V, or WS2815 at 12 V).
7. Timing of DMX fixtures and other lighting; which protocol sync is needed (MIDI clock, OSC, Ableton Link, timecode).
8. Training library size and track selection for M4.

---

## 18. Assumptions to verify

| # | Assumption | Tag | Where it matters |
|---|---|---|---|
| A1 | On STM32F7, DTCM is reachable by Ethernet DMA and general DMA, and is uncached | [Likely] | Node buffer placement |
| A2 | On STM32F7, only DMA2 can write GPIO (AHB1) | [Likely] | Parallel output design |
| A3 | ITU-R BT.1359 thresholds: about 45 ms sound-leads-picture detectability | [Likely] | Latency targets and offset helper |
| A4 | Ofcom guidance of 3 flashes per second is a sensible event default | [Likely] | Flash limiter |
| A5 | WS2812B draws about 20 mA per channel at full (about 60 mA per pixel at full white) | [Likely] | Power budget |
| A6 | Demucs v4 code licence, weights licence and repository maintenance status | [Verify] | M2 and M4 teacher |
| A7 | ONNX Runtime with DirectML gives a few ms per frame for the student on a laptop GPU | [Guessing] | M4 and M5 latency |
| A8 | mir_eval defaults: onset ±50 ms; note offset ratio 0.2 with 50 ms minimum | [Likely] | Metric cross-check |
| A9 | The kick returning after 8 or more bars is a reliable drop detector in dark psy | [Likely] | Section detection |
| A10 | vcpkg ports exist for imgui (docking), implot, miniaudio, pffft, nlohmann-json, spdlog, doctest, rtmidi; ONNX Runtime DirectML may need NuGet or a manual package. The M1 set was verified on 2026-10-15 at baseline `b1b19307`: it builds and links with MSVC 19.44, and `ctest --preset windows-msvc-release` passes smoke tests for pffft, miniaudio and nlohmann-json. rtmidi and ONNX Runtime are not yet checked | [Certain] for the M1 set; [Verify] for rtmidi and ONNX Runtime | Build setup |
| A11 | WASAPI loopback via miniaudio captures the laptop output reliably at 48 kHz | [Likely] | M5 |
| A12 | Single-ended 5 V pixel data is reliable over only a few metres | [Likely] | Rig and node placement |

---

## 19. Prior art and tools mentioned

| Tool or product | Relevance |
|---|---|
| Enttec OCTO MK2 and MK3 | Commercial Art-Net/sACN to SPI pixel controller, 8 universes, about £208 to £284 in the UK listings seen. The reference for what the node replaces |
| Advatek PixLite | **[Likely]** The pro standard for pixel controllers |
| Madrix | Pro pixel mapping and sound-to-light; inspiration for patch and preview |
| Resolume, MadMapper | Paid VJ and mapping tools (not used; owner prefers free for now) |
| TouchDesigner Non-Commercial | Free, resolution capped at 1280×1280, non-commercial licence only; useful for visual experiments, not paid gigs |
| LedFx | Free, open-source real-time audio-reactive pixels over sACN, Art-Net and DDP; quick comparison baseline |
| QLC+ | Free, open-source lighting console with Art-Net, sACN, RGB matrix and audio triggers |
| xLights | Free, open-source timeline sequencing for E1.31 and Art-Net, with Vamp-based beat detection |
| WLED | ESP32 firmware with Art-Net and E1.31 input; **[Likely]** over Wi-Fi it drops packets beyond 2 or 3 universes |
| OctoRig | Consumer, ESP32-class [Likely] 4-output controller at $49.99. The app sends effect parameters rather than pixels; Art-Net and sACN unreleased as of September 2026; planned subscription. Shows that the software, not the hardware, is the product |
| Demucs v4 | Offline source separation (teacher for M4) |
| aubio, Essentia, librosa, mir_eval | Python MIR libraries for prototyping, labels and metric cross-checks |

---

## 20. Glossary

- **Slot:** one 16th note; 93.75 ms at 160 BPM.
- **Gate ratio:** note duration divided by slot length.
- **Rolling bass (KBBB):** kick on the beat with bass on the three remaining 16ths, the classic psytrance pattern.
- **Universe:** 512 DMX channels; 510 are usable for 170 RGB pixels.
- **sACN (E1.31) and Art-Net:** protocols carrying DMX universes over UDP.
- **Sync packet:** ArtSync, or the sACN sync universe; tells nodes to latch the frame together.
- **File mode and live mode:** non-causal analysis with lookahead versus strictly causal analysis.
- **Teacher and student:** a heavy offline model labels data for a small real-time model.
- **Residual:** sound content outside the kick and bass, used to light rests.
- **Ladder:** the operating states PREFLIGHT, PREVIEW, AMBIENT, REACT and SHOW.
