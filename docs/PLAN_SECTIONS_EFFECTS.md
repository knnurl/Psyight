# Plan: stable sections, and more effects

Owner request 2026-10-18. Revised 2026-11-11: status, an assessment of what the first version got wrong, and the open work. The open work is specified in batches in `docs/PLAN_SECTIONS_BATCHES.md`. Claims about other software are from general knowledge and marked [Verify] where unsure.

## Status (2026-11-11)

Line references and symbols are as of commit `7871d43` (branch `follow-mode`, which contains `hex-visuals`; neither is merged to `main`).

| Item | State | Where |
|---|---|---|
| 1.0 Fragmentation report | Done | `psyight_analyse --sections-report` |
| 1.0 Ground truth and scoring | Done: 23 tracks labelled, `--sections-eval` (boundaries within a bar, bars labelled alike, phrase grid) | `gui/labelling_view.*`, `io/section_marks.*`, `analyse_main.cpp` |
| 1.1 Bar features | Done | `SectionBar` in `analysis/sections.h` |
| 1.2 Kick state with hysteresis | Done (Viterbi, `section.kick_switch_penalty`) | `analysis/sections.cpp` |
| 1.3 Boundaries on the phrase grid | Partly: build starts snap to 4-bar lines, grooves split on energy steps at phrase lines and on texture steps at 4-bar lines. Not done: snapping of other boundaries, a local phrase anchor, block novelty | Batches B and C |
| 1.4 Energy per section | Done, plus the riser curve and an intensity curve | `assign_energy`, `riser_curve`, `analysis/intensity.h` |
| 1.5 Live | Partly: energy and riser. Not done: any scene dwell, a build that keeps climbing | Batch E |
| 1.6 Automation | Changed shape: a hold rule for short sections (tolerant by default), rotation and variations by phrase, phrase-end fills, energy following. The palette rule was never built and is withdrawn (see the assessment) | `engine/automation.*` |
| Finer sections (SPEC 6.6.1) | Detected, labelled and scored: phases, roles, pre-drops, breaks, fills. The engine reads none of them | Batch D |
| 2 Effects, infrastructure to presets | Done, all eleven steps | `engine/space.*`, `pattern_effects.cpp`, `presets.cpp` |
| 2 Trails | Done as scene post-effects with frame memory (afterglow, radial echo) | `engine/post.*` |
| 2 Follow-up from the visual review | Done on `hex-visuals`: units, hex topology, seven hexagon effects, note history, packs Hexagon 2, 3 and 4 | `docs/PLAN_HEX_VISUALS.md`, `docs/PROGRESS.txt` |
| 2 Choreography | Through scene tags, not a sequencer (SPEC 16, 2026-10-19). Open: Hexagon 4 has one breakdown scene and one build scene | Batch F |

## Assessment (2026-11-11)

What the first version of this plan got wrong or left loose, with the evidence.

1. **The goal was one-sided.** Every "done when" line rewarded fewer, longer sections. The owner's labels show the opposite failure too: on Alien Army (184 BPM, analysed from the mix) the detection finds 11 sections where the owner labelled 36, with boundary precision 1.00 and recall 0.29, and 19% of bars labelled alike (`docs/research/UX_VISUAL_REVIEW.md`). Thirty-six sections in 417 s is 5.2 a minute, four times the corpus median of 1.20 that part 1 reported as a success. Fragmentation cannot be judged without the labels: the measure is the detected count against the labelled count, and precision and recall together.
2. **"About 1 section per 8 bars" was never a constraint.** At 150 BPM it allows 4.7 sections a minute; the baseline was already at 2.64. It is replaced below.
3. **"OculusDrift's sections are unchanged" was broken on the first day** (its results section says so) and kept as a goal. It is replaced by "every changed boundary on the reference tracks is listed and explained".
4. **"A target set once the labels exist" was never set.** The labels have existed since 2026-10-24: F 0.58 within a bar over 22 tracks, 70% of bars labelled alike; F 0.49 on the ten tracks tuned on and 0.67 on the twelve labelled after. Targets are proposed below.
5. **The scoring tolerance hides the step 3 question.** `--sections-eval` counts a boundary within one bar as a hit, so moving a boundary by a bar (what snapping does) cannot change its score. A scene cut a bar early is visible, so the measure for snapping is the boundary on the right bar. Batch A adds it.
6. **Step 3's rule (snap within 2 bars, symmetric) does not fit the later research.** `docs/research/SECTIONS_PSYTRANCE.md`: fills and stop-cuts sit before the line, so the pull is asymmetric (Rocha et al.); one bar of slack is normal; an added bar shifts every later phrase line, so one phrase offset per track is not enough. The engine already restarts phrases at every section start (`phrase_starts`), but the detector still places its energy and texture splits from the single `phrase_bar_offset`.
7. **The on-grid figure (12 to 38%) measured the phrase offset as much as the boundaries.** The labelled phrase grid exists on 3 tracks only, and the tracker's offset confidence is low (0.22 on OculusDrift). It stays in the report but is not a target.
8. **Step 5 asked live mode for something it cannot know.** "A section that would last under a phrase is held back" needs the section's length. What is causal: delay committing a breakdown's scene by a few bars, and let a groove that returns within a phrase resume the look it left. No live figure existed when the plan was written; the 20 DJ sets now give one (Batch A measures scene dwell on them first).
9. **Step 6's palette rule is withdrawn.** "Never within 8 bars of a scene change" cannot hold once scenes rotate every phrase (Hexagon 4 does), and phrases restart at sections, so a palette step and a scene change now tend to coincide, which reads as one change. The colour-identity problem it aimed at is handled by `palette_rotation: scene_list`.
10. **Part 2 mixed design with a to-do list, and two items changed form.** Mirror, strobe gate and 16th gating became effects and settings (wings, Strobe, Stutter gate), not layer post-effects; triggers with envelopes became `resolve_trigger` and `hit_envelope` in `engine/shape.h`. The choreography "template" became energy-tagged scenes by decision. None of that is open.
11. **The budget line was never reported for a whole scene.** Effects were measured one at a time (at most 0.131 ms on about 1,500 pixels); the whole pipeline is p50 0.22 ms, p99 0.65 ms on the 408-pixel hexagon. A whole-scene figure on 1,500 pixels is still owed [Verify: measure the heaviest built-in scene].

## Done when (revised)

Targets are proposals for the owner to confirm. Baselines are from PROGRESS (2026-10-24) and are re-measured by Batch A before anything changes.

- **File mode, against the labels** (held-out tracks, see the batch spec for the split):
  - boundaries within a bar: F at least 0.65 (now 0.58 over all 22), recall at least 0.60;
  - bars labelled alike: at least 75% (now 70%);
  - boundaries on the right bar: reported, with a target set from Batch A's baseline;
  - detected boundaries per labelled boundary: between 0.7 and 1.3 on the median track;
  - for comparison with the literature, hit rates at ±0.5 s and ±3 s are reported (Rocha et al. 0.517 at ±0.5 s; EDMFormer 0.616).
- **File mode, corpus:** the median track keeps no section under 4 bars that is not a build.
- **Reference tracks:** every boundary that moves is listed in PROGRESS with the reason.
- **Live mode:** scene dwells shorter than one phrase, drops and builds aside, are at most a quarter of what Batch A measures on the 20 DJ sets, with no drop caught later than today.
- **The engine reads the finer detail:** breaks, fills and pre-drops reach the lights from labelled tracks; detected events stay off until they are scored.

## What mature tools do

**Structure-aware lighting (DJ software).**
- rekordbox "phrase analysis" labels each track's structure on bar boundaries: Intro, Up, Down, Chorus, Bridge, Verse, Outro, with a mood (High, Mid, Low) [Verify the exact set]. Its lighting mode and SoundSwitch's autoscripting pick looks per phrase type and change them on phrase boundaries, never mid-phrase.
- Lesson: boundaries live on the phrase grid (8 or 16 bars in psytrance), and an energy level per section matters as much as its name.

**Music-structure research.** More in `docs/research/SECTIONS_PSYTRANCE.md`.
- Foote's self-similarity novelty (2000): beat- or bar-synchronous features, a self-similarity matrix and a checkerboard kernel give a novelty curve whose peaks are boundaries. MSAF collects such methods [Verify name].
- Smoothing state sequences with an HMM or Viterbi with a switching penalty is the standard cure for flicker. Boundary metrics: hit rate at ±0.5 s and ±3 s, and frame-wise label accuracy (mir_eval segment).

**Lighting consoles (grandMA, ChamSys MagicQ, Avolites Titan).**
- One effect engine ("phasers", "FX", "shapes") whose power comes from parameters applied across fixtures: spread (phase offset across the selection), wings (mirroring), blocks and groups, direction. A handful of waveforms gives hundreds of looks.

**Pixel-mapping software (MADRIX, xLights, Jinx!, WLED).**
- MADRIX: colour scroll, wave and radial, plasma, fire, fluid, noise, particles and explosions, shapes, audio level and spectrum effects, layers with blend modes, a cue list.
- xLights: bars, butterflies, circles, colour wash, curtain, fan, fire, fireworks, galaxy, meteors, morph, pinwheel, plasma, ripple, shockwave, spirals, strobe, twinkle, wave.
- WLED: about 180 one-dimensional effects (chase, scanner, comet, meteor, juggle, ripple, noise, sparkle, twinkle, lightning, fireworks, popcorn) [Verify the count].
- Lesson: many named effects are one engine (a waveform, a mapping of pixels to a coordinate, a trail) with different settings.

**VJ software (Resolume, MadMapper).**
- A clip per layer plus a chain of post-effects (mirror, kaleidoscope, strobe, hue, blur, feedback and trails), BPM-synced parameters, transitions between clips.
- Lesson: post-effects on a layer multiply the library cheaply.

## 1. Stable sections (the plan as written 2026-10-18, annotated)

**Problem.** On Hall of Purity and Hagios the kick state per bar flips every few bars, so sections, phrases and scenes change every 3 to 5 seconds. OculusDrift is fine.

**Step 0: measure before tuning (CLAUDE.md rule).** Done.
- A fragmentation report per track and over the training corpus: sections per minute, median section length in bars, the share of boundaries on the 8-bar phrase grid, the number of sections under 4 bars. `psyight_analyse --sections-report` over a folder.
- Ground truth: the owner labels sections with the editor. Metric: boundary hit rate at ±1 bar, label accuracy per bar. (Now 23 tracks, not the four reference tracks.)

**Step 1: bar features.** Done. One feature vector per bar: kick presence, bass presence and gate, hats level, residual flatness and centroid, overall energy.

**Step 2: kick state with hysteresis.** Done. A two-state Viterbi (kick, no kick) whose switching penalty makes a one-bar dropout or a stray kick too costly to switch on.

**Step 3: boundaries on the phrase grid.** Partly done; the rest is redesigned in Batches B and C (assessment 5 to 7).
- Candidate boundaries come from step 2 and from a bar-level novelty curve (Foote, on the bar features).
- As written: each candidate snaps to the nearest phrase start when within 2 bars. Superseded: an asymmetric pull of at most a bar, measured on the right-bar score, with the line count restarting at each kick return.
- Minimum lengths: breakdown 8 bars unless confident, build 2, drop 8, groove 8. (Built instead: a breakdown inside a track needs four kickless bars; a split piece is at least a phrase for energy steps and 4 bars for texture steps.)

**Step 4: energy per section.** Done. Low, mid or high from bar energy, like rekordbox's mood.

**Step 5: live (causal).** Open; redesigned in Batch E (assessment 8).

**Step 6: the automation side.** Done in a different shape (status table); the palette rule is withdrawn (assessment 9).

## 2. More effects (as written, all built)

Built as a small engine plus parameters (the console lesson), not as many separate effects.

**Infrastructure.**
1. **Spatial mapping.** Every effect reads a coordinate u from 0 to 1 per pixel, chosen by a mapping: along the strip, across x, across y, radial from the centre, by strip index, or random (later: hex rings, around, along by length). Plus wings (mirror into 2 to 4 segments) and direction (forward, back, bounce).
2. **Spread.** A phase offset across the mapping, so one waveform becomes a chase, a wave or a fan.
3. **Time bases.** Free Hz, or synced to the 16th, beat, bar or phrase.
4. **Triggers with envelopes.** Kick, hat, bass note, section start, drop, each with attack and decay, as inputs to any effect.
5. **Post-effects.** Trails (afterglow) and radial echo, per scene, with frame memory cleared on a seek.

**Effect families, psytrance first.**

| Family | Inspired by | Musical driver |
|---|---|---|
| Chase, scanner, comet (with trails) | WLED, xLights meteors | Steps on each bass note or 16th; the rolling bassline |
| Wave and sweep across the rig | Consoles' phasers, MADRIX wave | Bar or phrase; spread and wings |
| Ripple and radial rings | xLights ripple, our shockwave | Kick and hats |
| Noise and fluid | MADRIX noise, WLED Pacifica | Ambient, breakdowns; centroid to colour |
| Particles, fireworks, popcorn | xLights, WLED | Hats, drop |
| Fire | MADRIX, xLights | Builds (intensity from progress) |
| Spiral and pinwheel | xLights | Matrix and circle rigs |
| Bass bars and level | MADRIX audio effects | Bass pitch and velocity per row |
| Stutter and gate | Resolume strobe, gated synths | Bass gate ratio, 16ths |
| Strobe accents (limited) | Consoles | Drop, build end; flash limiter enforced |

**Choreography: sections become sequences.**
- A build ramps tension (speed and brightness), goes dark for the last beat, then the drop burst and a short strobe accent, then the groove scene.
- Breakdown: slow textures, low energy, colour drift by phrase.
- Built through energy-tagged scenes in rotation, not a template sequencer (SPEC 16, 2026-10-19).

Each step had a unit test (determinism; known pixels at known times), a capture and a frame cost measurement.

## Owner's answers (2026-10-19)
1. Yes: the owner labels the four reference tracks' sections with the editor.
2. File and live together.
3. Energy levels: yes. Also: some parts should follow the risers (a riser level as an input, for modulation and effects).
4. No text; be creative with the effects.

## Results, part 1 (2026-10-19)
Done: steps 0, 2 and 4, the build snap from step 3, the riser input (owner's answer 3), and the scene dwell from step 6 (file mode). `psyight_analyse --sections-report`, the four reference tracks (stems):

| Track | Sections/min before | After | Median bars before | After | Short before | After | Kick flips/min before | After |
|---|---|---|---|---|---|---|---|---|
| Hagios | 6.79 | 2.36 | 4.0 | 8.0 | 31 | 2 | 5.53 | 1.73 |
| Hall of Purity | 5.45 | 2.39 | 4.0 | 10.0 | 17 | 2 | 4.52 | 1.33 |
| OculusDrift | 2.11 | 1.05 | 8.0 | 16.8 | 4 | 0 | 2.71 | 0.60 |
| Phase Change | 0.41 | 0.55 | 7.8 | 9.0 | 1 | 1 | 1.10 | 0.27 |

Over the four reference tracks plus Desktop\launch (109 tracks with a tempo; mix unless stems exist) before: medians 2.64 sections a minute, 6.0 bars, 5 short, 17% on the phrase grid, 2.68 kick flips a minute. After: 1.20 sections a minute, 16.0 bars, 0 short, 38% on the phrase grid, 0.58 kick flips a minute. Six tracks gained sections (groove splits on energy steps); 20 still have a section under 4 bars that is not a build.

OculusDrift changed, against the "unchanged" goal, but for the better on inspection: its 197 to 234 s breakdown no longer alternates with 3 s grooves; the one loss is a 2-bar break with a short riser at 179 s, now inside the groove.

Read with the assessment: these figures show the flicker was cured. They do not show the sections are right (assessment 1).

## Results, part 1 continued (2026-10-24, from PROGRESS and SPEC 16)
Tuned on the owner's first 10 labelled tracks: boundary F within a bar 0.38 to 0.49. Changes: tempo range 120 to 230 BPM, a texture phase for the downbeat and phrase lines, texture splits on 4-bar lines (`section.texture_step` 1.5). On 12 tracks labelled afterwards (mostly full-on): F 0.67 (P 0.69, R 0.65). Over all 22 with sections: F 0.58, 70% of bars labelled alike. Open then and now: drop against groove and build labels disagree most; on Conquest, El Bonito and Hagios the tracker's beats sit about half a beat from the labelled grid; the phrase grid is labelled on 3 tracks.

## Results, part 2 (2026-10-19)
Done, in the plan's order:
1. Infrastructure: `engine/space.*` (pixel mappings, wings, clocks free or locked to the 16th, beat, bar, phrase, bass note or kick; value noise). Named integer settings show as stepped knobs.
2. Chase (comets, scanner) and wave.
3. Strobe accents (drop, build end, kick, beat, bar; 16th rolls) and the stutter gate (grid, bass gate, kick duck, pre-drop gap).
4. Noise and fire (fire climbs with the riser).
5. Bursts (particles and fireworks: rings from seeded pixels, sparks), spiral, bass bars.
6. Choreography: energy tags on scenes, the pre-drop gap, riser-driven builds.
7. Presets: 11 new scene presets; show packs Psy club and Dark forest.

Every effect has unit tests (known pixels at known times, determinism). Frame cost on 1,500 pixels, Release: at most 0.11 ms (noise). Trails followed on 2026-11-10 as scene post-effects (`engine/post.*`; recordings replay them exactly when no frame was dropped).

## Open work

Specified in `docs/PLAN_SECTIONS_BATCHES.md`:

| Batch | What | Priority it serves |
|---|---|---|
| A | Measuring tools: right-bar score, offsets, recall by kind, confusion, label statistics, live scene dwell, parameter overrides | Detection accuracy (the view before the tuning) |
| B | Boundary placement: local phrase anchor, asymmetric snap (parked by the owner, 2026-11-11) | Detection accuracy |
| C | Block novelty for changes while the kick runs on | Detection accuracy |
| D | Breaks, fills, pre-drops and phases reach the engine | Visual creativity |
| E | Live mode: breakdown commit delay, resumed grooves, a tension input that keeps climbing | Reliability, visual creativity |
| F | Breakdown, build and break scene candidates for the owner to pick | Visual creativity |

Owner decisions after Batch A (2026-11-11): B is parked; C runs after E and D; E and D are authorised, E first; the drop rule gets its own batch after C, from the owner's description (a drop mainly after a build, sometimes after a breakdown, one bar to two phrases long) and a study of the labelled drops; the proposed targets are working targets until then.

Not in the batches, and why:
- **Kicks off the constant-tempo grid on edited tracks** (Alien Army: a kick on 41% of beats in labelled grooves). It limits every score above on such tracks. A pickup rule was tried and reverted (it hurt 14 of 61 tracks); it needs its own measurement and plan.
- **An impact flash on the first returning kick, live.** Not viable as it stands: on the 20 DJ sets, 212 two-kick and 96 three-kick fragments sit inside the 181 drop gaps. The owner's call.
- **The interface findings U1 to U9** of the visual review, including the line that says why the show chose a scene.

## Questions for the owner (answered above, 2026-10-19)
1. Will you label the sections of the four reference tracks with the new editor? It gives the ground truth for step 3's tuning.
2. Sections: file mode first and live after, or both together?
3. Should sections get an energy level (low, mid, high) shown in the strip and usable by scenes?
4. Any effect families to drop or add (for example text, which needs a matrix of at least about 16 rows)?
