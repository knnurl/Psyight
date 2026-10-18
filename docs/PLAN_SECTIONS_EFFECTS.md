# Plan: stable sections, and more effects

Owner request 2026-10-18. Two work streams, sections first (the show's structure depends on them). Claims about other software are from general knowledge and marked [Verify] where unsure.

## What mature tools do

**Structure-aware lighting (DJ software).**
- rekordbox "phrase analysis" labels each track's structure on bar boundaries: Intro, Up, Down, Chorus, Bridge, Verse, Outro, with a mood (High, Mid, Low) [Verify the exact set]. Its lighting mode and SoundSwitch's autoscripting pick looks per phrase type and change them on phrase boundaries, never mid-phrase.
- Lesson: boundaries live on the phrase grid (8 or 16 bars in psytrance), and an energy level per section matters as much as its name.

**Music-structure research.**
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

## 1. Stable sections

**Problem.** On Hall of Purity and Hagios the kick state per bar flips every few bars, so sections, phrases and scenes change every 3 to 5 seconds. OculusDrift is fine.

**Step 0: measure before tuning (CLAUDE.md rule).**
- A fragmentation report per track and over the training corpus (180 to 441 local tracks): sections per minute, median section length in bars, the share of boundaries on the 8-bar phrase grid, the number of sections under 4 bars. `psyight_analyse --sections-report` over a folder; the corpus result becomes a golden metric.
- Ground truth: the owner labels sections on the four reference tracks with the new editor. Metric: boundary hit rate at ±1 bar, label accuracy per bar.

**Step 1: bar features.** One feature vector per bar (most exist in `section_bars`): kick presence, bass presence and gate, hats level, residual flatness and centroid, overall energy.

**Step 2: kick state with hysteresis.** Replace the per-bar majority vote with a two-state Viterbi (kick, no kick) whose switching penalty makes a one-bar dropout or a stray kick too costly to switch on. Fills stay in the groove; real breakdowns survive.

**Step 3: boundaries on the phrase grid.**
- Candidate boundaries come from step 2 and from a bar-level novelty curve (Foote, on the bar features).
- Each candidate snaps to the nearest phrase start (8 bars, the detected phrase offset) when within 2 bars. Off-grid boundaries are kept only when the evidence is strong (a clean kick return after 8 bars or more: drops land exactly).
- Minimum lengths: breakdown 8 bars unless confident, build 2, drop 8 (as now), groove 8.

**Step 4: energy per section.** Low, mid or high from bar energy and density, like rekordbox's mood. It gives a groove that steps up (a new bassline) its own look without a new label.

**Step 5: live (causal).**
- The same state machine with a dwell time: a breakdown commits after 2 bars without kicks (as now), but section changes prefer the predicted next phrase boundary (the beat clock already predicts it).
- A section that would last under a phrase is held back.

**Step 6: the automation side.**
- Scene changes on phrase boundaries only, except drops (the drop burst lands at once).
- A minimum scene dwell of one phrase.
- The palette moves on phrase starts (as now), but never within 8 bars of a scene change.

**Done when:**
- The fragmentation report shows at most about 1 section per 8 bars on the corpus median, with no sections shorter than 4 bars except builds.
- OculusDrift's sections are unchanged.
- Boundary hit rate at ±1 bar meets a target set once the owner's labels exist.

## 2. More effects

Built as a small engine plus parameters (the console lesson), not as many separate effects.

**Infrastructure first.**
1. **Spatial mapping.** Every new effect reads a coordinate u from 0 to 1 per pixel, chosen by a mapping: along the strip, across x, across y, radial from the centre, by strip index, or random. Plus wings (mirror into 2 to 4 segments) and direction (forward, back, bounce).
2. **Spread.** A phase offset across the mapping, so one waveform becomes a chase, a wave or a fan.
3. **Time bases.** Free Hz, or synced to the 16th, beat, bar or phrase (the grid is already in the engine inputs).
4. **Triggers with envelopes.** Kick, hat, bass note, section start, drop, each with attack and decay, as inputs to any effect.
5. **Layer post-effects.**
   - trails (afterglow: the previous frame decays, a memory like the modulation's, cleared on a seek);
   - mirror;
   - strobe gate (within the flash limiter);
   - 16th gating from the bass gate.

**New effect families, psytrance first.**

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
- A build sequence ramps tension (speed and brightness), goes dark for the last beat, then the drop burst and a short strobe accent, then the groove scene.
- Breakdown: slow textures, low energy, colour drift by phrase.
- A per-section-type template the automation follows, so a set has an arc and not just scene swaps (SoundSwitch's lesson).

**Presets.** Ten or more new scene presets using the new families, and two show packs ("Psy club", "Dark forest").

**Order.**
1. Spatial mapping, spread, time bases, triggers.
2. Chase and comet with trails.
3. Wave.
4. Strobe accents and stutter.
5. Noise.
6. Particles.
7. Fire.
8. Spiral.
9. Bass bars.
10. Choreography templates.
11. Presets.

Each step: a unit test (determinism; known pixels at known times), a capture, a frame cost measurement (budget: the whole scene under 2 ms for 1,500 pixels).

## Questions for the owner
1. Will you label the sections of the four reference tracks with the new editor? It gives the ground truth for step 3's tuning.
2. Sections: file mode first and live after, or both together?
3. Should sections get an energy level (low, mid, high) shown in the strip and usable by scenes?
4. Any effect families to drop or add (for example text, which needs a matrix of at least about 16 rows)?
