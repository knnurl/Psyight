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

This is the M1 baseline. Every threshold is a registry parameter (`bass.*`), and ML is expected to beat it. Implemented in `app/src/analysis/bass_tracker.*` (file mode).

**[Certain]** A low bass note's period (24 ms at E1) is longer than a 1024-sample window and five 5.33 ms hops: shorter analysis sees the waveform, not the envelope, and its energy and flux swing by 30 dB within a period. Every bass feature below is therefore at least one period long.

1. **Kick handling.** Detect kicks from click-band (2.5 to 6 kHz) flux peaks that are broadband (click flux at least 0.2 × low-mid flux), come with a low-band rise of at least 6 dB, and lie within ±35 ms of a beat of the grid (the manual grid in M1, beat tracking in M2). Build a kick mask (default 60 ms after each kick) where bass offsets aren't declared and bass onsets are suppressed; for onsets it starts as early as the onset function can see the kick (half its window plus its smoothing, plus 15 ms).
2. **Bass envelope.** The bass band's power (80 to 400 Hz) averaged over a centred 25 ms window (about one period).
3. **Onsets.** A 2048-sample STFT over 40 Hz to 2 kHz; each bin's compressed magnitude is averaged over the envelope window, then differenced across that span (centred) and rectified, so period ripple cancels before rectification. Adaptive-threshold peak picking on the track-normalised result; a peak needs bass activity and must not drop the bass level by more than 12 dB (the click of an abrupt offset). Optional grid attraction is not built.
4. **Offsets.** A note ends at the first of: the envelope dropping below k × note peak (default k = 0.25, the window's lag removed); the noise floor; the next onset; or the next kick (v0: a long note held under a kick is split there). Drops inside the kick mask are ignored. Gaps shorter than the envelope window are not resolved: the note runs to the next onset.
5. **Pitch.** File mode averages the front end's harmonic-sum salience (8 harmonics, 8192-sample centred window: the bass roll's pitch background) over the central half of the note; cents by parabolic interpolation across semitone rows. **[Certain]** Notes shorter than about two periods have no reliable pitch, and get low confidence. Live mode (M5) uses a 2048-sample causal window weighted towards harmonics. Output MIDI, cents and confidence.
6. **Derived values.**
   - Gate ratio.
   - Decay time constant, from an exponential fit between the peak and the offset.
   - Slide: a note (no re-attack) whose halves differ in salience pitch is split where the second pitch's salience overtakes the first's, minus a 20 ms bias (about half a glide) [Verify] on real slides.
   - Long note: no re-attack at the next slot.
   - Rests: a slot with no onset and activity below a threshold.
7. **Aggregates.** Per bar and per phrase statistics (section 6.2).

#### Rests and residual

When a slot is a rest, the residual features (non-bass content) drive the residual layer, so whatever sound remains still produces light. This was an explicit owner requirement.

### 6.4 Kick, hats and residual

- **Kick:** click-band onset plus low-band rise, within ±35 ms of a grid beat, in runs (another candidate one beat away). Real kicks forced looser gates than the synthetic ones: sidechained mixes barely raise the low band at a kick (Phase Change: 2.3 dB median) and distorted kicks carry low-mid energy (Hall of Purity click ratio 0.19), so the rise gate is 1 dB and the click ratio 0.1. With Demucs stems, kicks come from the drums stem.
- **Hats:** 8 to 12 kHz onsets with a minimum gap, used for sparkle and detail layers. Built (`app/src/analysis/residual.*`); kick clicks also reach this band and are kept. With stems, from the drums stem.
- **Residual:** content outside the bass and kick bands: centroid, flux, flatness, energy. With stems available, use the "other" stem instead. Built: per frame above 400 Hz (`app/src/analysis/residual.*`).
- **Bass aggregates** (SPEC 6.2): built per bar and phrase (`app/src/analysis/bass_aggregates.*`).

### 6.5 Beat, bar and phrase

- **File mode** (`app/src/analysis/beat_tracker.*`): a kick onset function (low-band rise times click flux, weighted down where the click is weak against the low-mid flux, as in bass attacks); tempo from its autocorrelation with a harmonic comb and a prior of 120 to 190 BPM centred on 150 (the reference tracks run at 150, 152, 170 and 187.5 BPM); dynamic-programming beats (Ellis 2007 style); a constant-tempo grid by template matching over the whole track (the period and phase whose beats collect the most onset strength), refined by least squares. Downbeats come from kick breakdowns first: where the kick returns after a break of a bar or more (and, at half weight, where it stops), plus every isolated kick (kick-on-one sections). Bar-level novelty of band energies and bass salience decides only when a track has no breaks.
- **Live mode:** a phase-locked loop on kick onsets (built: `app/src/analysis/live_analysis.*`, measured in docs/results/M5.md). Kicks come from the student's kick head, confirmed one frame after their peak. The tempo is acquired from the folded inter-onset intervals of the first kicks; each kick near a predicted beat then corrects the phase and the period (a second-order loop); lock is declared after 4 kicks within 0.06 beat. A run of kicks returning after a break marks a downbeat. Not built yet: a bar-level cue for tracks without breaks, the loop gains in the registry, and tap and nudge controls.
- **Phrase:** counted from the downbeat in groups of 8 or 16 bars, configurable (default 8), positioned by kick breaks of two bars or more. Resetting on section changes is not built yet.
- **Prediction:** the PLL provides the next expected 16th times, so the engine can schedule ahead in live mode.

### 6.6 Section detection

Built per bar of the beat grid (`app/src/analysis/sections.*`). The kick state per bar comes from a two-state Viterbi: a bar costs the beats that disagree with its state and each change of state costs 6 beats (`section.kick_switch_penalty`). A breakdown inside the track therefore needs four kickless bars; dropouts of up to three bars (fills, missed kicks) stay in the groove, and stray kicks stay in the breakdown.
- **Intro and outro:** the bars without kicks before the first and after the last kick bar.
- **Breakdown:** no kick for 2 bars or more. One kickless bar is a fill: real tracks are full of them, so the SPEC's first default of 4 beats was too short.
- **Build:** first from a riser: in a breakdown of 4 bars or more, the residual's spectral flatness (above 400 Hz) peaks within the last 8 bars at 2.5 times or more its lower quartile in the breakdown and at 0.03 or more. The build starts where flatness first passes the quartile times √2.5 on the way up and runs to the kick's return, so a pre-drop gap of up to 4 bars stays in it. Flatness that is high from the breakdown's first bar is texture, not a riser. Without a riser, the old rule: the high band (8 to 12 kHz) rises 6 dB or more above the breakdown's quietest bar. Live mode applies both rules causally to 1 s averages (the riser against the flatness floor since the breakdown began). `psyight_analyse --bars` prints what the rules see per bar.
- **Drop:** **[Likely]** the kick returning after 8 or more bars without one is a reliable drop detector; the 8 bars after it are labelled drop.
- **Groove:** every other bar with kicks. A groove splits on a phrase line where the median bar level of the phrase after differs from the phrase before by 3 dB or more (`section.energy_step_db`), each piece at least a phrase long: a groove that steps up gets its own section.
- **Build snap:** a build start within a bar of a 4-bar line of the phrase grid moves onto it.
- **Energy:** each section gets low, mid or high from its bars' mean level (all bands) against the terciles of the track's bar levels. Live mode: low in intros and breakdowns, mid in builds, high in drops; a groove is judged once a phrase against the grooves' mean residual activity (3 dB over: high, under: low).
- **Riser curve:** 0 to 1 per frame. Through each build it follows the residual's flatness (1 s average) from the build's start to its peak, never falling, and holds to the build's end (a build without a flatness rise ramps linearly); 0 elsewhere. Live: 0 when the build is declared, 1 at the riser ratio squared, never falling within the build. It is an engine input and a modulation source ("some parts follow the risers").
- Outputs carry a label, an energy, a confidence and a timestamp. Scene automation (section 7.6) consumes them. `psyight_analyse --sections-report <audio or folder>` measures fragmentation: sections per minute, median length, short sections, boundaries on the phrase grid and kick flips per minute.

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
5. **Export.** ONNX for checks in Python; the app runs its own C++ inference of the exported weights (`app/src/ml/student.h`, decision log 2026-10-17), streaming one frame at a time in live mode and the whole track at once in file mode, bit-identical between the two. Measured: 37 µs median (p99 105 µs) per frame, and 1.1 s for a 7-minute track, on this laptop's CPU (`psyight_tests -tc="student: inference cost" --no-skip`). ONNX Runtime (CPU, prebuilt) is the fallback if the model family changes.
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

Optional `"sections_edited": true` (default false) marks sections edited by hand: the show then follows them instead of the detected sections. Sections drafted by a tool and never edited leave it false.

Optional `"phrases": { "start_s": 13.33, "bars": 8 }` (absent: from the detection) is the phrase grid labelled by ear: phrases of `bars` bars, one starting at `start_s`. Optional `"sections_done": true` (default false) means the owner checked the whole track's sections and phrases in the labelling mode (View > Label sections): only such tracks are ground truth for the section metrics (`psyight_analyse --sections-eval`: boundary precision, recall and F within one bar, the share of bars labelled alike, and whether the detected phrase grid lines up).

**Metrics,** reported per labelled region and per track:
- Onset precision, recall and F-measure at ±20 ms (configurable). **[Likely]** The mir_eval default is ±50 ms, which is too loose for 94 ms slots.
- Offset error distribution (median, p90), plus a mir_eval-style offset hit rate (offset ratio 0.2, minimum tolerance 50 ms) [Verify] defaults.
- Pitch accuracy within ±50 cents, with octave errors counted separately.
- Gate-ratio mean absolute error.
- Beat F-measure and downbeat F-measure (M2).
- Section boundary accuracy within ±1 bar (M2).
- Measured latency in live mode (M5).

A Python cross-check script (`tools/python/eval_mir.py`) recomputes onset and note metrics with mir_eval to validate the C++ implementation: P, R and F against mir_eval's maximum matching, the least-distance pairing found independently, and every metric compared with `psyight_eval --notes`, on real labels and on random edge cases.

The app's scoring panel shows these metrics for the loaded track's detections, per complete region and in total, the offset error histogram, and every error (missed, false positive, octave, pitch, offset) worst first; clicking one seeks there. A regression test runs the full analysis on the synthetic set and fails when any stored metric gets worse (`app/tests/golden/synthetic_metrics.json`).

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
- **Joints:** LED strip cannot run through another strip or bend sideways round a corner, so pixels are laid out on each strip's pieces: its polyline less 5 mm either side of every sharp corner (a turn over 20 degrees), less 5 mm (to the other strip's centre line) where its end meets another strip, and, where two strips cross away from their ends, less 5 mm either side on one of them (an open strip runs through a closed one, then the longer through the shorter). Contacts are with where the other strip really is, its corners already cut out. A strip ending on another strip's corner runs into the gap there and puts a pixel on the corner: its pixels are laid from that end (with both ends on corners, from the polyline's end). So each spoke of the hexagon mandala ends with a pixel on the outer ring's corner. Pixels sit at the strip's own pitch, centred on each piece they fill (the part of a pitch left over shared between its ends), so a spoke's pixels sit evenly between its two joints. Each strip carries `fills` (saved in the project): while it is set, the count follows the path and is fitted to the pieces after any change and on open; typing a count in the strips table clears it (typing what fits sets it again). A count below what the pieces hold is shared over them in proportion to their room, each piece's pixels centred, so a ring built with a pixel less per side matches. Files saved before the flag infer it once: a count no more than the whole path holds and at least three quarters of what the pieces hold filled it. Strips coarser than 20 px/m are strings of single pixels and have no joints.
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
| Pattern | Chase (comets with tails, looping or bouncing as a scanner) and wave (sine, triangle, ramp, square or bump across the rig); strobe accents (the drop, a build's last beat, kicks, beats or bars, rolling on 16ths, within the flash limiter); a stutter gate (multiply: the 16th, 8th or beat grid, the bass gate, or a kick duck); a noise field (coloured by the residual's brightness); fire (rising flames that grow with the riser); bursts (rings from seeded pixels on kicks, hats, beats, bars or bass notes, overlapping, optionally broken into sparks); a spiral (arms turning around the centre); bass bars (a pitch meter: a marker or bar at the bass note's pitch). The pattern effects run on a shared pixel mapping (along the strip, across x or y, radial, by strip, random, hex rings, around) with mirrored wings and a clock that is free or locked to the 16th, beat, bar, phrase, bass note or kick (`app/src/engine/space.*`, docs/PLAN_SECTIONS_EFFECTS.md part 2) |
| Hexagon | On the rig seen as a hexagon (its centre, a corner's direction from its shape, hex distance so each hexagon ring about the centre is one value): hex pulse (hexagons out from the centre or in from the edge on kicks, beats, bars, bass notes or hats, lighting a whole ring at once), cube (three shaded faces and a Y of edges, the Necker cube flipping on the beat, bar, two bars, phrase or kick), sectors (3 to 12 slices round the centre: a chase, alternating halves, a fill or seeded slices on a clock) and interference (three waves at 120 degrees making a drifting hexagonal lattice, its contrast pumped by kicks); the hex rings and around mappings let every pattern effect follow the hexagon (`app/src/engine/hex_effects.cpp`) |

**Duration-aware ideas** (owner requirement; mappings customisable):
- Gate ratio sets comet length or pulse width.
- Decay time constant sets the trail decay.
- Slides morph hue continuously instead of jumping.
- Long notes hold and breathe instead of stepping.
- Bar-level gate-ratio averages shift motion style between staccato and legato looks.

The two browser prototypes in `prototypes/` demonstrate shockwave plus conveyor, beat-phase waves with pitch to hue, feedback trails, note history, sparkle and the ambient fallback.

### 7.4 Modulation matrix

- A route is: **source**, then **transform**, then **target**, with an amount.
- **Sources:** any feature (6.2), beat, bar or phrase-synced LFOs, MIDI CC, constants, random (seeded), section progress and energy, the riser curve (6.6).
- **Transforms:** gain, offset, threshold, curve (linear, exponential, logarithmic, S-curve), attack and release smoothing, sample-and-hold, quantise to grid, invert, range map.
- **Targets:** any registry parameter flagged as modulatable.
- Multiple routes to one target are summed, then clamped to range.
- **Built so far:** sources include 8 macro knobs (saved with the project, MIDI-learnable); transforms curve, invert, hold per 16th, beat or bar (sample-and-hold), and attack and release smoothing. Holding and smoothing keep memory across frames in the show renderer, cleared on a seek, so a render stays deterministic for the same sequence of render times (replay reproduces it). Targets: layer opacity, layer hue shift, scene brightness, and any effect setting of a layer (by its id; the amount is a fraction of the setting's range, the sum clamped to it, and the effect remade for the frame). The Show tab draws a ring on every modulated knob (opacity, effect settings) and on each route's amount knob for the playing scene, and lists the routes on a modulated knob's hover. Not yet: registry parameters outside the scenes (limiters, automation) as targets.
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
  - on a section change (breakdown to an ambient scene, build to a build scene, drop to a drop scene, chosen in rotation or at random among tagged scenes); a section shorter than a phrase keeps the scene before it, unless it is a build or a drop (file mode); scenes may also carry an energy tag (low, mid, high), and a section prefers the scenes tagged with its energy, then those with none (file and live mode)
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
- **Docked layout** (restyle phase 4): the transport is a strip over the left column (analysis and bass roll below it) rather than in the top bar. Right column: the rig preview pinned on top (foldable), the note inspector with scoring, then the show controls with log, rig and preflight. In React and Show the editing panels hide and the rig is drawn large in their place (configurable in settings).

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
- **Saving:** project and labels files change only on an explicit save; the app asks before unsaved changes would be lost (new or other project, other track, quit). While there are unsaved changes, a recovery copy is written beside the file (`<file>.autosave`) once a minute and on exit, and offered back when the file opens again if newer than it. The last project opened is reopened at start.

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
- **Parallel output (8 strips, one GPIO port: PD0 to PD7, all on the Zio headers, since 2026-10-23):**
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
| 2026-10-16 | Bass tracker analysis windows are at least one bass period long: the envelope averages 25 ms, the onset function smooths each bin over that span before a centred difference | Shorter windows see the 24 ms waveform period of E1 and fire false onsets inside held notes; measured on the synthetic set (onset F from 0.40 to 1.00) |
| 2026-10-16 | In M1, kicks must lie within 35 ms of a beat of the manual grid | A synth bass attack's click passed the click-flux and low-rise tests; the grid is the M1 beat prior until M2's beat tracking |
| 2026-10-16 | Bass tracker v0 ends a note at the next kick | The slot-3 note of K-B-B-B closes at the kick, whose own energy hides the drop; cost: long notes held under a kick are split |
| 2026-10-16 | psyight_eval scores the tracker from the command line; the scoring panel remains M1 work | First real numbers before the panel; reproducible command for docs/results |
| 2026-10-16 | M2 starts before the remaining M1 GUI items (detections in the bass roll, scoring panel, golden metrics, log panel), which are parked in docs/PROGRESS.txt | Owner decision |
| 2026-10-16 | Tempo prior 120 to 190 BPM centred on 150 (was 150 to 170 centred on 160) | The reference tracks run at 150, 152, 170 and 187.5 BPM (owner confirmed Hagios); the range still excludes the half and double tempos |
| 2026-10-16 | Constant-tempo grid by template matching over the whole track | Numbering beats from one origin, or by counting dynamic-programming beats, slipped in breakdowns (Hall of Purity: 170.26 against 170.0 BPM) |
| 2026-10-16 | Downbeats and phrases from kick breakdowns, with novelty as the fallback | Rolling grooves repeat every beat; bar-level novelty put OculusDrift's downbeat one beat off, kick breaks get it right |
| 2026-10-16 | A breakdown needs 2 kickless bars (SPEC 6.6 said 4 beats) | One-bar fills and missed kicks split every groove into short breakdowns |
| 2026-10-16 | Kick gates loosened (rise 1 dB, click ratio 0.1) and kicks required in runs | Measured at grid beats against offbeats on the reference tracks; the synthetic-tuned gates found kicks on 7 to 33% of beats |
| 2026-10-16 | Demucs v4 (htdemucs) stems through tools/python (torch 2.5.1 CUDA 12.4, demucs 4.0.1), aligned to the mix by cross-correlation before analysis | Owner approved the install; stems decoded by Demucs ran 47 ms ahead of the app's MP3 decoding |
| 2026-10-16 | M2 closed as far as it can go without owner labels; M3 started, then M4 if possible | Owner decision. Labels for the M2 exit table remain an owner task (docs/PROGRESS.txt) |
| 2026-10-16 | Engine and output share one show thread at 60 Hz for now (SPEC 4.2 lists two), paced by a high-resolution waitable timer, render time from the audio sample position | Measured 0.15 ms a frame for 444 pixels including the sends; split the threads if a measurement shows the network stalling renders |
| 2026-10-16 | The preview draws what the in-app virtual node latched from the sent packets | Every preview frame exercises the patch, packetising, sequence numbers and sync, as SPEC 8 asks |
| 2026-10-16 | Output defaults: network sending off, sACN multicast, priority 100, sync universe 1000, first universe 1, 170 pixels a universe, GRB order; CID derived from a fixed name | Nothing leaves the machine until the owner turns it on; the sync universe sits clear of the data universes for any rig under about 170,000 pixels |
| 2026-10-16 | Scene automation v0: rotation among scenes tagged with the section label, cuts into grooves and drops, crossfades (4 beats, at most half the section) into the rest, palette shift every 2 phrases with a 4-beat RGB crossfade; a manual scene applies at once | Sections already start on downbeats, so a cut at the section start lands on one; impact sections want cuts, ambient ones fades. The next-downbeat rule for manual changes needs transport-time state and waits for the live tiers |
| 2026-10-16 | The project file (`psyight.psyproj`) lives in the working directory, loads at start and saves on exit when changed; scenes and palettes are not in it yet | One rig per installation for home testing; the same place as imgui.ini. A file that fails to load is never overwritten |
| 2026-10-16 | Flashes are tracked per pixel as pairs of opposing changes (rise of 0.1 above the trough, re-armed by a fall of 0.1 below the peak); a flash is a run of frames in which a quarter of the pixels are rising or recently rose; the flash limiter runs last, after the power budget | The per-frame rise test counted one kick ring as four flashes and let a held strobe through as a 0.3-amplitude ramp; judging before the power budget let Hagios reach 4 flashes in a second. Measured with psyight_show_check on all four tracks (docs/results/M3.md) |
| 2026-10-16 | Bass conveyor lights one segment in five and steps one segment per note; the residual shimmer twinkles 15% of pixels | At 50% duty with half-period steps the conveyor inverted on every 16th, a full-field strobe against 7.2; the scenes made 3344 flashes on OculusDrift, up to 17 a second |
| 2026-10-16 | Modulation v0 targets a layer's opacity and hue and the scene's brightness, not registry parameters; effects are saved as a type plus parameters described per type | Covers the common mappings (kick ducking, phrase hue drift) without making every effect parameter a per-frame value; the per-type tables give the file format and generated controls in one place |
| 2026-10-17 | M4 teacher data is streamed: separate a track, extract the teacher targets and student features, delete the stems; every copy of the four reference tracks is excluded from training; the student is trained and scored in Python before ONNX Runtime enters the app | Stems for ~200 tracks need ~75 GB with 26 GB free; SPEC 6.9 keeps evaluation tracks out of training; measuring the student first avoids a C++ dependency that may not pay off (owner decisions) |
| 2026-10-17 | M1 finished: scoring panel with an error list, "shift all labels", log panel, golden metrics on the synthetic set (beat phase, not downbeat: the set has no bar-level cue), eval_mir.py; mir_eval 0.8.2 and scipy in the Python tooling | The remaining M1 checklist items; mir_eval is the reference the scoring claims to match |
| 2026-10-17 | The student runs in the app through hand-written streaming C++ inference, not ONNX Runtime; Microsoft's prebuilt ONNX Runtime (CPU) through a vcpkg overlay port is the fallback if the model family changes | The student is small (1x1 and dilated 3-tap convolutions, folded batch norm): no dependency, bit-identical runs, causal by construction, well under a millisecond a hop; the pinned vcpkg has only the CUDA-only, DLL-only onnxruntime-gpu (owner decision) |
| 2026-10-17 | Live mode v0: the student decodes kicks, hats, bass and residual causally; the beat is a second-order PLL on its kicks (phase gain 0.25, period gain 0.05, capture 0.25 beat, lock after 4 kicks within 0.06 beat, reacquire after 4 kicks off the grid); downbeats and sections come from kicks counted only in runs; the live clock is the capture sample count, with silent gaps filled at wall-clock pace | Measured on the four reference tracks (docs/results/M5.md): stray kicks inside breakdowns ended them until kicks counted only in runs; WASAPI loopback delivers nothing while nothing plays |
| 2026-10-18 | UI restyle (docs/RESTYLE.md) phase 1: one theme module (app/src/gui/theme.*) holds every UI colour, size and font, with Desk and Show modes and theme.json overrides; fonts Inter and JetBrains Mono (SIL OFL 1.1) and Lucide icons (ISC) bundled in assets/fonts with their licences; no FreeType (not built; ImGui's rasteriser); LED palettes and preview pixels stay show data, outside the theme | The owner's restyle plan names the fonts and their OFL files; OFL allows bundling with any software. FreeType would be a new dependency for hinting only |
| 2026-10-18 | Sections edited by hand live in the labels file (sections, plus sections_edited); the show follows them only when sections_edited is set, through a section timeline passed beside the analysis (phrases restart at each section); recordings keep them so replay matches | Detection is imperfect (fragmented kick states); the owner's correction should drive the show; unchecked drafted sections must not |
| 2026-10-18 | Undo for show edits: snapshots of the project, changes within 0.5 s merged into one step; Ctrl+Z and Ctrl+Y act on the panel with the focus (show panels: the project; elsewhere: the labels) | One key pair for two histories without surprising the operator; snapshots are simple and the project is small |
| 2026-10-18 | Status chips and the fault banner go in a status bar along the bottom of the window, not the top bar (SPEC 9.3): the top bar is full with the menus, the state ladder and the safety controls | Glanceable state without crowding the safety controls; the banner opens Preflight |
| 2026-10-18 | Scene launch quantisation: a scene picked by hand, or the return to the section rules, takes over at the next bar by default (now, next bar or next phrase per project), at once while the transport stands still; keys 1 to 9 pick scenes, 0 returns to the rules | SPEC 7.6 asked for a cut on the next downbeat; the default changes from the v0 immediate cut |
| 2026-10-18 | Preflight test patterns (chase, colour order, identify universe, all on) replace the scenes in the show renderer, through the limiters; allowed only in Preflight and Preview, and they may reach the node there ("Send to the lights") although the show output is off | Wiring checks need the real node; limiting them to the safe steps keeps them out of a live show |
| 2026-10-18 | Presets: a built-in library compiled in (8 scene presets with their palettes, 8 more palettes, 4 show packs), and the user's own as JSON files in presets/scenes, presets/palettes and presets/shows beside the project, sharing the project file's scene and palette JSON | Owner's choice: scene presets, show packs and palette presets, in a library folder |
| 2026-10-18 | Rig templates as generators: pillars (row or circle, serpentine option) and a matrix panel (zigzag rows); a few numbers make ordinary strips with group tags (pillars or matrix, left or right, top or bottom, even or odd), replacing the rig after a confirmation | Owner's choice: generators that fit the real build, over fixed examples |
| 2026-10-18 | Routes can target any effect setting of a layer, by the setting's stable id; amount is a fraction of its range | Owner: rings on most knobs; ids survive parameter reordering, and an unknown id is ignored |
| 2026-10-18 | The Show tab gets an Ableton-style device look: rotary knobs (drag, double-click to type, right-click for a menu: reset, modulate with, map to macro, MIDI learn), the scene's layers as a device chain (drag to reorder), routes as cards, a Macros device, modulation rings; short knob labels with the long names in tooltips | Owner request ("a quality Ableton plugin feel"): knobs, device chain and rings, in the Show tab first |
| 2026-10-18 | Modulation: macro knobs as sources (8, in the project, MIDI-learnable), hold per grid division and attack and release smoothing per route; the memory these need lives in the show renderer and resets on a seek | Owner request; determinism kept for the same render-time sequence, as the limiters already are |
| 2026-10-18 | Builds from spectral flatness risers, with the high-band rule as the fallback; live builds read 1 s averages | Owner request: builds into drops caught better. Hall of Purity's 181 to 203 s build (hats entering mid-breakdown) becomes 195 to 203 s (the riser); live false builds on OculusDrift drop from 5 to 3. No section ground truth exists: the owner checks by ear |
| 2026-10-18 | Restyle phase 4 layout: transport strip over the left column, rig preview pinned top right (foldable), show controls in their own Show tab. Show mode is React or Show (the app moves between them by itself when a scene is clicked, so tying it to Show alone would flip the layout mid-set): Show theme, editing panels hidden, the rig drawn large in their place | Owner's choice of layout; ease of live operation |
| 2026-10-18 | Restyle phase 3: number fields drag (double-click to type), one undo step per drag; the universe fields stay typed and stepped because a drag would sweep live output across universes; Re-analyse gets no confirm because it never touches labels | Owner's restyle plan; reliability |
| 2026-10-22 | Files change only on save: the project and labels autosave now writes recovery copies (`<file>.autosave`), offered back at the next open; Save, Don't save or Cancel before a new or other project, another track, or quitting; projects get New, Open, Open recent and Save as (File menu, Rig tab), the last one reopens at start; Ctrl+S saves the project and the labels. The Rig tab's free-text project path is gone | Owner: "files change automatically without me saving". The editable path also redirected the minute autosave and the save on exit to whatever name was being typed, which could overwrite another project |
| 2026-10-22 | Node config protocol 2: apply (rig settings live between two frames, no flash write or restart), palette (the fallback show's colours), a protocol line first in get; the app's Preflight > Node setup plans universes and data lines from the rig (strips chained in patch order, at most 680 pixels a line, 8 lines) and sends, applies, reads back and saves them | The node's settings fitted only the default frame (3 universes), so every new rig but one stayed mostly dark, and changing them meant typing commands, a flash save and a restart |
| 2026-10-22 | The rig's sources at one level, as tabs under Rig > New rig: Templates (pillars, matrix panel), Presets (the built-in shapes under four category headings) and My rigs (the user's own, saved as rig presets in presets/rigs: geometry and groups, colour orders stay the project's) | Owner: one dropdown mixed the generators with the libraries ("template can be at the same level as user") |
| 2026-10-22 | Rig presets: 17 named, fixed stage shapes compiled in beside the two generators (Rig > Templates > Preset, also in the setup assistant), each within the 1,500-pixel budget: stage shapes (DJ booth chevron, triple arch tunnel, goalpost truss, triangle row, pixel curtain, pixel wall) and sacred-geometry and organic ones (sunburst, hexagon mandala, Hive temple, Hive portal, honeycomb, lotus, third eye, string-art star, alien ribcage, fractal tree, vortex). Symmetric shapes run each half from the centre outwards, so mirrored and radial mappings line up; every strip is tagged left and/or right plus its shape's groups | Owner request (2026-10-21): ready-made rigs to start from, the Hive ones after the Mo:Dem festival's Hive stage. The generators stay for rigs that must match a real build |
| 2026-10-24 | Sections and beats tuned on the owner's first 10 labelled tracks (darkpsy, full-on, hi-tech, 150 to 214 BPM), scored with `psyight_analyse --sections-eval data`: boundaries within a bar F 0.38 (P 0.59, R 0.28) before, 0.49 (P 0.57, R 0.43) after. (1) File-mode tempo range 120 to 230 BPM with the prior centred on 170 (was 190 and 150): three tracks above 190 were read at 2/3 or clamped; all 10 tempos now match the labels within 0.005 BPM. Live mode reaches 230 too. (2) A texture phase decides the downbeat and the 4-bar and 8-bar lines (80% of labelled boundaries fall on 4-bar lines, 58% on 8-bar lines): 5 of 6 downbeats match where the beats agree, against 3 of 6 from kick breaks, which still decide on short tracks or with confidence 0.5 or more. (3) Grooves and drops split on 4-bar lines where the bars' texture (band levels, bass and hats density, scaled over the track) changes by texture_step (1.5); kick dropouts are stepped over. The labelled grid may start bar 1 before the audio, so bar numbers match the music's (Hagios: bar 27 at 31.6 s) | The owner's labels showed most missed boundaries were changes with the kick running on both sides (groove to groove recall 0.12), and that the grid itself was wrong on the fast tracks. Tuned on the same tracks it is scored on: one threshold on a broad plateau (F 0.49 to 0.50 from 1.0 to 1.5), so expect slightly less on new tracks [Verify] |
| 2026-10-27 | Hexagon effects pack: hex distance (each ring about the centre one value) as the geometry, four effects (hex pulse, cube, sectors, interference), hex rings and around mappings for every pattern effect, three hexagon scene presets (Hex tunnel, Hex radar, Hex implosion) and the Hexagon show pack, completed with Comet rain, Nebula, Drop bloom and Deep space from Psy club (the owner found the cube and honeycomb scenes weaker); the wave's rate may run negative (inwards) | Owner: "a new hexagon effects pack, surprise me", for the hexagon mandala at home; circles reach a hexagon's corners late, hex distance lights a ring at once |
| 2026-10-26 | A strip ending on another's corner puts a pixel on the corner (laid from that end) | Owner: "where's the corner pixel?": the spokes stopped 5 mm short of the outer corner, leaving the corner dark between the ring's pieces |
| 2026-10-26 | Strips carry `fills` (the count follows the path) instead of the app guessing from counts after each edit | The guess failed when the clearance changed: the owner's spokes kept their 8 mm counts (16) and stopped short of the outer corner; with the flag they refit (18) |
| 2026-10-26 | Joint clearance 5 mm (was 8): with 10 mm strip the pieces' edges just meet. Contacts are taken with the other strip's corner-cut pieces, so spokes reach into a ring's corner gap, level with the ring's pieces | Owner: "make the 8 mm off distance 5, spokes have to reach till end, aligned with rings" |
| 2026-10-26 | Joints in the pixel layout for every rig (SPEC 7.1): strips stop 8 mm short where they meet, cross or turn a sharp corner, and counts fill the pieces. Spokes (open strips) run through rings at crossings. The default frame's sides moved a pitch outside so its 444 pixels stay | Owner: "consider where the lines intersect and overlap": on the 30 cm hexagon, pixels of different strips sat 1.2 to 6 mm apart (strips crossing at the middle ring, ends on corners), and rings had pixels on corners no strip can bend round; now 11 mm or more. Owner chose every rig, spokes through, joints at sharp corners, 8 mm |
| 2026-10-26 | Hexagon mandala gains Centre Y (three spokes from the middle to the inner hexagon's alternate corners, in group centre_y and not hex, off by default), so a layer on hex leaves them dark and a layer on centre_y switches the nested-cube look | The owner's choice among the cube options: the mandala as now in one mode, plus the inner Y in another |
| 2026-10-26 | Preset lengths shown at their real size (the setting times Scale; Scale itself shown only as Overall width, first in the list); the stored values and builds are unchanged. Build kit: a Fusion script (tools/fusion/PsyightBuildKit) builds the one DXF from its layer names, reading Psyight's own R12 output rather than through Fusion's DXF import | The owner's Outer radius read 0.30 m on a hexagon 0.20 m in radius (the width had set Scale 0.67); the owner works in Fusion |
| 2026-10-26 | Build kit style Open lattice beside Panel: back and front follow the channels (channel plus a wall width each side, default 5 mm), openings left open, the spacer is the walls; a wall too thin for a bolt is glued (round bolt ears past the acrylic were tried and dropped: the owner found them ugly). Both styles keep bolt holes 0.5 mm clear of the acrylic (the panel style could place one under its edge) | Owner asked whether the back should be full or cut to spokes; the open lattice suits a mandala on a wall, the panel a rig with a background or the electronics on its back |
| 2026-10-26 | Build kit export: four parts of a layered panel from the rig (back, spacer islands, acrylic lattice, front mask) with bolt holes at each island's deepest points, wire holes at strip ends, engraved island numbers; computed on a grid (0.2 mm, coarser for big rigs), traced and straightened; no new dependency. Wiring order numbers drawn in lines in the export | Owner building the hexagon mandala at 30 cm with acrylic over it, needing a way to hold the parts |
| 2026-10-25 | 3D rigs: DXF import keeps depth (z of lines, 3D polylines, spline control points, elevations of 2D entities); the Stage and Preview gain Top, Side and Orbit views (orthographic, far pixels first and dimmer in Orbit); editing stays in the Front view | Owner: strips following curves of a Rhino and Grasshopper design, in 2D and 3D |
| 2026-10-24 | Rigs export as drawings (Rig > Export drawing...): SVG and DXF R12 in mm, layers by kind (centre lines, channels at a set width, pixel dots, wiring arrows and labels with line and pin) | Owner: build a preset (the hexagon mandala) from the software's geometry; chose DXF and SVG, all four kinds, layers by kind, from the current rig |
| 2026-10-24 | Rigs from DXF drawings too (Rig > New rig > From drawing): LINE, LWPOLYLINE and POLYLINE with bulges, ARC, CIRCLE, ELLIPSE, SPLINE; layers as groups, off and frozen layers skipped, $INSUNITS; arcs and circles left of the middle reversed so mirrored halves run outwards. Our own reader, no new dependency | Owner designs in Fusion, whose 2D export is DXF, not SVG |
| 2026-10-24 | Rigs from SVG drawings (Rig > New rig > From SVG): one strip per subpath in the drawn direction, groups from the labels of the layers and groups holding it, real units from the document size, hidden layers skipped; parsed by our own small reader (no new dependency). A Hive stage preset sized to a 1.4 by 0.75 m frame | Owner: a custom hive-stage-like rig for home on a 140 by 75 cm frame; carved shapes are designed in a vector editor, so the drawing should be the rig (cut files and pixel layout from one source) |
| 2026-10-23 | Live mode: 1.5 s below about -55 dBFS ends the music: the beat loop lets go (tempo unknown, acquiring again), the section returns to the intro and the show fades out over 2 s. Rig presets become generators with settings (counts, sizes, switches, scale, density) | Owner: live mode kept pulsing long after the music stopped (the loop free-runs through breakdowns by design, so it needs to know silence from a breakdown); and wanted to edit presets, for example the pixel wall's rows |
| 2026-10-23 | The node broadcasts its ArtPollReply on its subnet (as Art-Net 3 nodes did) instead of unicasting it; the app resends a config command up to three times; the console prints receive counters (frames, polls, pool starvation, DMA resumes) | On a direct cable a unicast reply waited on ARP for seconds, past the app's discovery window; broadcasting needs no ARP and the app listens on 6454 for any reply. Config replies still need ARP, which a resend finds resolved. The counters showed where packets went while debugging |
| 2026-10-23 | Node data lines on PD0 to PD7 instead of PE0 to PE7 | All eight on the Zio headers (five side by side on CN9 pins 2 to 10 with a ground at pin 12, two on CN9 pins 25 and 27, one on CN8 pin 12); PE1 was only on the morpho header. No board function on PD0 to PD7 (CubeMX board file); the console's PD8 and PD9 share the port but the BSRR words never name them. The Zio D0 to D7 the owner asked about span ports E, F and G, which one DMA write cannot drive |
| 2026-10-23 | Node DHCP fallback: a link-local 169.254.x.y/16 from the MAC instead of 2.x.y.z/8 (a fixed address, by default 2.x.y.z/8, with dhcp 0); Node setup keeps lines at or under 545 pixels (60 fps) when the eight lines allow it | A PC cabled straight to the node takes 169.254.x.y (Windows), so the 2.x fallback put them on different subnets and discovery failed on the bench; the planner filled lines to 680 pixels, which put the lotus preset's lines at about 52 fps although three 60 fps lines fit |
| 2026-10-22 | A labelling mode (View > Label sections, F9) for sections and phrases: a track list from remembered music folders with each track's status, tap-to-mark keys snapped to bars or phrases over the detection, a labelled phrase grid and a done flag in the labels file, and `--sections-eval` to score the detection against done tracks | Owner: label phrases and sections quickly with track selection; the section rules (and a learned model later) need ground truth before any tuning (CLAUDE.md: measure first) |
| 2026-10-22 | Node protocol 3: a colour order per lane for the node's fallback show, the palette stored as RGB, config version 2 with an upgrade from version 1 so a firmware update keeps the node's settings; the signal-loss policy and fallback show are project settings that Node setup sends | The fallback show assumed GRB while the app supports six orders per strip; SPEC 12.2 wants the fallback customisable, and settings that live only on the node are lost when a node is swapped |
| 2026-10-19 | Choreography through scene tags and effects, not a separate sequencer: energy tags on scenes (low, mid, high) choose among a label's scenes; a stutter mode darkens a build's last beats; build scenes follow the riser; two choreographed show packs (Psy club, Dark forest) cover every section | Keeps one mechanism (tagged scenes in rotation) that works the same in file and live mode, and that operators already edit; a template sequencer can come later if the packs prove too static |
| 2026-10-19 | New effects share one engine: a pixel mapping to 0 to 1, wings and a musical clock, set per effect as integer settings with named values (EffectParamInfo::options, shown as stepped knobs); chase and wave first | The plan's console lesson: a few waveforms over a mapping give many looks; integer settings keep the project format, modulation and generated controls unchanged |
| 2026-10-19 | Sections: kick state by a two-state Viterbi (switch penalty 6 beats) instead of the majority vote with the half-bar and lone-bar rules; grooves split on 3 dB phrase-median level steps; build starts snap to 4-bar lines within a bar; an energy level per section; a riser curve as an engine input and modulation source; scenes hold through sections shorter than a phrase (builds and drops aside) | Owner: stable sections (docs/PLAN_SECTIONS_EFFECTS.md part 1), energy levels, "some parts follow the risers". Measured with `--sections-report`: Hagios 6.79 to 2.36 sections a minute, Hall of Purity 5.45 to 2.39, OculusDrift 2.11 to 1.05 (its 197 to 234 s breakdown no longer alternates with 3 s grooves; a 2-bar break at 179 s is now inside the groove) |
| 2026-10-18 | Operator preferences persist in psyight.settings.json (working directory): theme, interface depth, spectrogram colours and range, volume, bass roll toggles. Saved when they change; capture mode ignores the file | Owner: settings should persist |
| 2026-10-18 | Restyle phase 2: magma spectrogram by default (inferno and viridis selectable; the CC0 tables compiled in); the bass roll's pitch background becomes a neutral ramp so accent-filled notes stand out (plasma's yellow top hid them); phase 4 layout: compact transport over the left column, rig preview pinned top right | Owner's restyle plan; the layout is the owner's choice of three |
| 2026-10-17 | Node firmware: D-cache off (I-cache on) instead of DTCM placement; lwIP without an RTOS, UDP only; DHCP falling back after 10 s to a MAC-derived 2.x.y.z/8 address (or a fixed address with `dhcp 0`), not "static with DHCP fallback"; lanes mapped by global pixel index; config as text commands on UDP 6455 | Uncached RAM keeps every DMA buffer coherent with no MPU setup, and the node's load is far below the core's; a poll loop needs no locking; DHCP-first finds the node on any network while the fallback keeps it reachable without one; global pixel indices match the app's patch, which runs strips across universe boundaries; a text protocol needs no tool to configure |
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
| A6 | Demucs v4 code licence, weights licence and repository maintenance status. Checked 2026-10-16: code MIT; upstream archived 2025-01-01, maintained fork github.com/adefossez/demucs (important fixes only); weights licence not stated, so weights are never shipped | [Verify] weights | M2 and M4 teacher |
| A7 | ONNX Runtime with DirectML gives a few ms per frame for the student on a laptop GPU | [Guessing] | M4 and M5 latency |
| A8 | mir_eval defaults: onset ±50 ms; note offset ratio 0.2 with 50 ms minimum | [Likely] | Metric cross-check |
| A9 | The kick returning after 8 or more bars is a reliable drop detector in dark psy | [Likely] | Section detection |
| A10 | vcpkg ports exist for imgui (docking), implot, miniaudio, pffft, nlohmann-json, spdlog, doctest, rtmidi; ONNX Runtime DirectML may need NuGet or a manual package. The M1 set was verified on 2026-10-15 at baseline `b1b19307`: it builds and links with MSVC 19.44, and `ctest --preset windows-msvc-release` passes smoke tests for pffft, miniaudio and nlohmann-json. rtmidi and ONNX Runtime are not yet checked | [Certain] for the M1 set; [Verify] for rtmidi and ONNX Runtime | Build setup |
| A11 | WASAPI loopback via miniaudio captures the laptop output reliably at 48 kHz | [Likely] | M5 |
| A12 | Single-ended 5 V pixel data is reliable over only a few metres | [Likely] | Rig and node placement |
| A13 | A click to low-mid flux ratio separates real kicks from bass attacks. Synthetic: kicks 0.29 and up, bass attacks 0.03 to 0.07. Real kicks at grid beats: median 0.63 (OculusDrift), 0.68 (Phase Change), 0.19 (Hall of Purity, distorted kicks), so the gate is 0.1 | [Likely] | Kick detection, beat onset function |
| A14 | The slide split bias of 20 ms (half the generator's 40 ms glide) suits real slides | [Guessing] | Slide onset timing |
| A15 | On OculusDrift the tracker's bass onsets sit 7 ms (p5 to p95: 3 to 10 ms) before the 16ths of a grid aligned to the detected kicks: production timing or tracker bias on real attacks, undecided | [Verify] | Onset bias parameter |

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
