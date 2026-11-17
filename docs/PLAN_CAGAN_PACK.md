# Implementation spec: packs for the Cagan piece

Status: specified 2026-11-17 from an interview with the owner. Nothing built. Builds on
`sections-2` as it stands (all hexagon effects plus the break, fill, pre-drop and tension inputs).
Priority that drives this work: visual creativity (2), without touching detection (1).

## 0. Owner decisions (interview, 2026-11-17)

| Question | Answer |
|---|---|
| LEDs against the saved rig "Cagan 3D" | The same strips: 12 omurgalar, 18 kenar cizgiler, base ring, pistil |
| Shape | Flat layers now, not the 25 cm cone |
| Light path | Arms (omurgalar) are seen as pixels. Everything else is backlit glow through the white layer |
| Zones that can be addressed | Six ribbed arms, three honeycomb domes, zigzag panels and rim, centre and back halo |
| Domes | New strips, not in the rig file: two rings, about 36 pixels a dome, addressable |
| Music map | Bass on the arms, kick on the domes; glow zones carry mood and sections |
| Character | All of: organic and creature-like, tight to the music, dark and sparse, lush glow. Which one leads is a matter of the pack |
| Colour | Warm reds and ambers, dark forest, bioluminescent, and palettes changing by section. Again by pack |
| Variants | One shared scene set, shipped as several packs that differ in palette and arc |
| Scope | Full show pack for file mode: intro, groove, build, drop, breakdown, break, outro |
| Judging | Contact sheets first, then the owner picks. No pack is built before the picks |

## 1. Ground rules

- The compatibility contract of PLAN_HEX_VISUALS.md 1.1 holds: add, do not change. Enum values
  are appended, new parameters default to today's behaviour, every existing golden and assertion
  stays untouched. If one would have to change, stop that item and report.
- No existing pack, scene, palette or rig file is edited. `presets\rigs\Cagan 3D.json` is read,
  never written.
- Protected zones: the flash limiter and the power budget are not changed. Report each
  candidate's estimated draw and flash count; do not tune the limits to fit.
- Offline only: no audio aloud, no window input, nothing sent to a node.
- Rendered output goes to `captures\review\cagan\` in the owner's checkout (gitignored).
- British English, no em dashes, sentence case, `[Verify]` on unverified claims.

## 2. The rig: "Cagan flat"

Measured from "Cagan 3D" (centre at x 0, y 1.602 m; angles from straight up, clockwise positive):

- **Arms, sharp:** 12 omurgalar. Six curved ones of 35 pixels leave the centre and end at radius
  0.55 m, at 46, 74, 166, -166, -74 and -46 degrees. Six of 43 pixels run from radius 0.66 m in
  to the centre, at 11, 109, 131, -131, -109 and -11 degrees. Half of them are wired outwards and
  half inwards.
- **Glow loops:** six closed loops of 106 pixels at radius about 0.78 m (the outer lobes), three
  of 64 pixels at about 0.55 m.
- **Glow wedges:** nine open strips (26, 15, 26 pixels) forming three wedges about 0, 120 and
  -120 degrees, from radius 0.27 to 0.65 m.
- **Base ring:** 161 pixels at 0.86 m, 30 pixels a metre. Taken as the back halo.
- **Pistil:** 18 pixels standing 30 cm out from the tip.

Batch CA writes a generator, `tools/python/packs/cagan_rig.py`, that reads "Cagan 3D" and writes
"Cagan flat" beside it (rig presets are not tracked in git; the script is).

Key decisions:

1. **Flatten, keep the layer order.** z is scaled so the piece is 5 cm deep instead of 25 cm
   `[Verify: real layer depth]`. Effects that use depth still see centre in front of rim.
2. **Each dome is one strip of 36 pixels:** an inner ring of 12 at 4 cm radius, then an outer ring
   of 24 at 9 cm `[Verify: counts, radii, wiring order]`, 60 pixels a metre. One strip a dome
   matches one data run and lets a per-strip mapping (section 3) treat a dome as a unit. The three
   domes sit at the middle of the three wedges `[Verify against the piece: which lobes hold the
   domes and which the zigzag panels; the pictures show domes at the top and lower left and
   right]`. Placeholder until the domes are wired.
3. **Groups by voice and by light path**, added to the existing ones:
   - `arms`, `arms_inner` (the six from the centre), `arms_outer` (the six to the rim), sharp;
   - `domes`, `dome_1` to `dome_3`;
   - `panels` (wedges and the 64-pixel loops), `rim` (the 106-pixel loops);
   - `halo` (base ring), `centre` (pistil);
   - `sharp` (arms) and `glow` (everything else), for the sheet tool and for scenes that treat
     the two light paths differently.
4. **The pistil stays in the rig** in group `centre` until the owner says whether the flat piece
   has one (open point 1).

Total: 35 strips, 1784 pixels.

## 3. What the engine needs (additions only)

Most of the pack is existing effects on groups: `wave`, `plasma`, `noise`, `polar_noise`, `fire`,
`chase`, `sparkle`, `shimmer`, `stutter`, `phrase_fill`, `shockwave`, afterglow, with the `radial`,
`around`, `by_strip` and `along_length` mappings. The hexagon effects that need closed hexagon
rings (`pitch_clock`, `note_arcs`, `ring_automaton`, `sectors` on a ring) do not fit this rig and
are not used.

Three things are missing. Each is an addition, off or unused by default:

**C1. Mapping "Radial, per strip"** (appended to `Mapping`). u is the pixel's distance from its
own strip's centroid, 0 at the centroid and 1 at that strip's farthest pixel. On a dome this is
0.44 on the inner ring and 1 on the outer, so `wave` or `shockwave` on group `domes` with the kick
clock gives a thump from each dome's middle to its edge. No new effect for the kick.

**C2. Effect `arm_notes`** (Bass). The bassline drawn on the arms as an instrument.
- Arms are ordered by their turn round the centre. A note picks its arm by pitch: option
  "Pitch class" (12 arms, 12 semitones; with another arm count, pitch class modulo the count),
  "Scale step" (the notes heard in the last phrase ranked low to high, spread over the arms) or
  "All arms".
- Light runs from the centre end of the arm outwards regardless of wiring direction (by distance
  from the rig's centre, not by pixel index).
- While the note sounds its head travels out: reach is `gate_ratio` of the arm, or the note's
  length against a 16th. After it ends the arm fades with the note's `decay_tau_ms` (scaled by a
  `release` setting).
- A slide cross-fades from the previous note's arm to the new one over the slide.
- `trail`: how many notes of the history stay drawn, dimmer by age (uses `EngineInputs::notes`).
- `confidence` floor, `velocity` to level, colour by pitch across the palette, as the hexagon bass
  effects have them.
- Stateless: a pure function of the inputs and the rig.
- Unit tests on the synthetic generator: known pitch gives the expected arm, known gate ratio the
  expected reach within one pixel, a slide lights two arms, a rest leaves the arms dark, inward
  and outward wired arms give the same picture.

**C3. `wave`: `strip_offset`** (default 0: today's output). A phase offset per strip in strip
order, in cycles. With the radial mapping on `arms` it makes the arms pulse one after another
round the piece, which is the tentacle or opening-flower motion. If `wave` already has an
equivalent (check `spread` with `by_strip` first), use that and skip C3.

Not added: pack settings in the app. The owner chose several packs over one pack with settings.

## 4. Seeing the diffuser on a contact sheet

The engine renders pixels; the piece shows glows. Batch CA extends the batch F sheet tool
(`captures\review\sections\batch_f\candidates.py`, moved into
`tools/python/packs/cagan_sheets.py` without its private paths) so that a frame is drawn as the
piece would look:

- pixels in `glow` are drawn and then blurred with a Gaussian of 4 cm `[Verify against a photo of
  the lit piece]`, on a dark wood-coloured ground;
- pixels in `sharp` are drawn on top as dots, unblurred;
- a second, plain version of each sheet (no blur) is kept beside it, so the pixel content can be
  checked.

This is a picture for judging, not a measurement. Figures (dark share, draw, flashes) come from
`psyight_show_check` as before.

## 5. Scene candidates (batch CC)

Candidates as data in `tools/python/packs/cagan_candidates.py`, in the form of `hex3_spec.py`.
Rules for all of them: bass on the arms and kick on the domes unless the scene says otherwise;
glow zones move slowly (bar or phrase clocks), arms move fast; no white; a quiet scene never goes
fully dark; grooves stay under a quarter of the rig lit above byte 20 so the drop has room.

| Section | Candidate | What it does |
|---|---|---|
| Intro, outro | Dormant | Halo breathing once a phrase, a faint drift in the panels, hats as single sparks on the arm tips |
| Intro, outro | First light | Arms lighting one after another round the piece through the phrase (`strip_offset`), domes dark |
| Groove | Nerve | `arm_notes` by pitch class, domes thumping on the kick, everything else dark |
| Groove | Tide | `arm_notes` with all arms, a slow radial wave in the panels a bar long, kick on the domes, rim dark |
| Groove | Spine | `arm_notes` with a long trail (4 notes), kick stepping round the three domes one at a time, halo dim |
| Groove | Lure | Arms dark but for hats at their tips; the bass colours the panels by pitch; kick on the domes. The glow-led alternative |
| Build | Inhale | Light drawn from the rim into the centre on each beat, faster and brighter with `tension`; arms fill from the tips inwards with `build_progress`; dark over the pre-drop |
| Build | Swarm | Sparkle on the arms growing denser with `tension`, domes brightening, halo rising; dark over the pre-drop |
| Drop | Bloom | Kick as a shockwave from the centre across the whole piece, `arm_notes` at full level, panels and rim in full colour |
| Drop | Strike | Kick on the domes and the rim together, all twelve arms firing outwards on each bass note, panels dark for contrast |
| Breakdown | Drift | Polar smoke in the panels and rim, the three domes breathing in turn once a bar, arms as a slow outward ripple |
| Breakdown | Abyss | Near dark: halo only, single slow pulses travelling out along one arm at a time, afterglow |
| Break | Held breath | The domes hold a dim glow, arms dark, bass notes as short flicks at the arm roots |
| Break | Afterimage | The last frame decays (afterglow) while hats spark on the arm tips |

Sheets: every candidate on Alien Army and OculusDrift, at the times batch F used for each section
kind, plus groove and drop times chosen from the labels. One fix round after looking at the first
sheets, as in batch F, then stop.

## 6. The packs (batch CD, after the picks)

One scene set, three packs generated from it by `gen_cpp.py`. They differ in palette lists and in
arc, not in mapping:

| Pack | Colour | Arc |
|---|---|---|
| Cagan ember | Warm reds and ambers with small cool accents, as in the owner's lit picture | By section: lush intros and breakdowns, sparse tight grooves, full drops |
| Cagan forest | Deep greens, teals and violets, rare hot accents | Dark and sparse throughout: glow zones capped low, the arms carry the show |
| Cagan abyss | Cyan, magenta and UV blues on black | By layer: arms always tight, glow always slow and organic, in every section |

Palette changes by section inside each pack come from the scenes' palette lists and the existing
phrase rotation. New palettes are additions; existing ones are reused where they fit (Ember, Ocean
UV, UV violet, Mono violet, Toxic, the Dark forest set).

Each pack tags its scenes for every section kind including `break`, so the Batch D options work
with it when the owner turns them on.

## 7. Batches

| Batch | Work | Ends with |
|---|---|---|
| CA | `cagan_rig.py` and "Cagan flat"; `cagan_sheets.py` with the diffuser view; a sheet of the bare rig with each group lit in turn | The group sheet, for the owner to check zones against the piece |
| CB | C1, C2 and (if needed) C3, with unit tests; goldens untouched | Tests pass, default output byte-identical |
| CC | The candidates as data, sheets and figures, one fix round | Stop: the owner picks |
| CD | The three packs from the picks, generated; a show golden for one of them on the synthetic track | After the picks only |
| CE | Operator guide and SPEC 7 entries, PROGRESS | Docs current |

CA, CB and CC run without the owner. CD waits for the picks.

## 8. Verification

- ctest and the doctest suite pass after every code commit; show, inputs and presets goldens
  untouched; `psyight_replay` on the four recordings in `data\recordings` stays frame-identical.
- `psyight_show_check` on the owner's project without the new scenes is byte-identical to the
  binary before this work.
- For each candidate, held over each track: dark share, share of frames with over a quarter of
  the rig above byte 20, estimated draw, flashes after the limiters (limiter on).
- Render cost of each candidate against the engine's frame budget, measured alone, not in
  parallel.

## 9. Stop conditions

- An existing golden, assertion or default would have to change.
- The rig needs a change the generator cannot make from "Cagan 3D" (missing strips beyond the
  domes).
- A candidate cannot stay inside the power budget at the owner's limits: report it, do not change
  the limits.

## 10. Open points for the owner

1. Does the flat piece still have a pistil, or is the centre lit another way?
2. Real dome wiring: pixel counts, ring radii, order, and which three lobes hold the domes.
3. Is the base ring the back halo, and is it seen directly or only on the wall?
4. The depth of the flat stack, and how blurred the glow really is (a photo of the lit piece in
   the dark would set the sheet tool's blur).
5. Which node and outputs drive the three dome runs (outside this spec).
