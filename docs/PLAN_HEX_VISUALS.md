# Implementation spec: hexagon visuals (findings L1 to L12 and eight new effects)

Written 2026-11-10 for the agent that implements it (Claude Opus, working in this repository). It turns `docs/research/UX_VISUAL_REVIEW.md` sections "LED visual findings" and "New effect ideas" into work. Read `CLAUDE.md` and that review first; this file does not repeat their reasoning. The UI findings (U1 to U9) are out of scope.

Line numbers are as of commit `e63d608`. Re-locate by symbol if they have moved.

## 0. Owner decisions (2026-11-10)

1. **Add, do not change.** No existing effect, preset, pack, mapping, option value or file key is removed, renamed or given a new meaning. New behaviour arrives as new settings, new option values appended at the end of their lists, new effect types appended at the end of `EffectType`, new fields and new files.
2. **Four fixes are on by default** (the owner: "they should be opted in as default, they are safe"): L1 (power estimate), L2 (kick hexagon start), L3 (held-section rule), L6 (rig-relative sizes, beat-relative decays). Each is a new setting whose default is the new behaviour and whose other value reproduces today's output bit for bit.
3. **Everything else is off by default** or only reachable through new effects and the new pack: peak hold, dithering, the palette policy, the kick fallback trigger, the new mapping.
4. **Frame memory and note history are in scope**, as the last two engine phases.
5. **A new built-in show pack, "Hexagon 2"**, carries the new looks. The "Hexagon" pack and every existing scene preset stay byte-identical in `builtin_show_packs()` and `builtin_scene_presets()`.

## 1. Ground rules

- **Branch:** `hex-visuals`, from `main`. Do not merge or push to `main`; the owner watches the piece first. One concern per commit, in the phase order below. Each commit builds and passes `ctest --preset windows-msvc-release`.
- **Determinism:** every new effect is a pure function of `(rig, EngineInputs, palette, seed)`. The two stateful items (phase 8) are deterministic for a given sequence of render times, as the limiters and modulation memory are.
- **Real-time rules:** the show thread may allocate as existing effects do (per-render vectors). No locks, file I/O or logging in `render`.
- **Units in names:** `_s`, `_ms`, `_m`, `_hz`, `_beats`. British English, no em dashes, sentence case for labels.
- **Budget:** a new effect costs at most 0.15 ms on 1,500 pixels, Release. Measure each (section 12) and record the figure in `docs/PROGRESS.txt`.
- **Docs in the same commit:** `docs/SPEC.md` (7.3 table, 7.7, 16 decision log), `docs/OPERATOR_GUIDE.md` for anything the operator can set, `docs/PROGRESS.txt`.
- **Protected zone:** phase 2 (the power estimate) changes a safety limiter. It is authorised by decision 2. Do not go beyond what section 3 specifies, and do not touch the flash limiter.
- **Do not edit** `firmware/`, `analysis/live_analysis.*`, the beat or bass trackers, or anything on `wip/live-drop-reentry`.

### 1.1 Compatibility contract (test it)

| Thing | Rule |
|---|---|
| `EffectType`, `EffectParams` | Append only. The variant's alternative index is the enum value (`EffectSpec::type()`), so order is load-bearing |
| Effect parameter ids | Parameters serialise by id (`"params": {"width": 0.12}`). A new field in an existing params struct gets a new id; a missing id loads as the struct default |
| Integer options | Append new names at the end of the `"A|B|C"` string and raise the field's max by one. Never reorder |
| `Mapping`, `Clock`, `ModSource` | Append only; update the count constant and the options string |
| Project file | New keys only. A missing key loads as the struct default (so the four fixes come on for existing projects, as decided) |
| Recordings | A `params.jsonl` line without the new keys must replay as recorded: the session loader sets the legacy value for every key in table 1.2 that is absent. Add `psyight_replay` coverage with a recording made before this branch (make one from `main` first and keep it in `data/`, gitignored) |
| Variations | `variations.cpp` picks a remap by `info.max - info.min + 1`. Raising a mapping field's max would change existing shows' variations: see phase 4, item 4 |
| Existing tests | A test that asserts today's numbers keeps asserting them, with the legacy value set explicitly. Do not loosen a tolerance to make a test pass |

### 1.2 New settings that change defaults

| Setting | Where | Values (file ids) | Default | Legacy |
|---|---|---|---|---|
| `power_model` | `LimiterParams`, project `limiter` | `linear`, `duty` | `duty` | `linear` |
| `quiescent_ma` | `LimiterParams`, project `limiter` | 0 to 5 | 1.0 [Verify] | 0 |
| `hold_rule` | `AutomationParams`, project `automation` | `strict`, `tolerant` | `tolerant` | `strict` |
| `start` | `HexPulseParams` | 0 centre, 1 first pixels | 1 | 0 |
| `size_units` | Shockwave, Bursts, Noise, Plasma, Build, DropBurst, Fire, Interference params | 0 metres, 1 rig | 1 | 0 |
| `time_units` | Shockwave, Bursts, HexPulse, DropBurst params | 0 seconds, 1 beats | 1 | 0 |

## 2. Phase 1: measuring tools (no behaviour change)

Build the view before the change (CLAUDE.md, "How to work").

1. **`psyight_show_check` reads edited sections (L4b).**
   - File: `app/src/show_check_main.cpp`.
   - When the labels have `sections_edited`, build the `SectionTimeline` exactly as the GUI session does (find the call to `section_timeline(...)` under `app/src/gui/`; reuse it, do not copy logic) and pass it to `inputs_at`, `automate` and `ShowConfig::sections`.
   - New flag `--detected-sections` keeps today's behaviour. Print which was used in the first line.
2. **Frame statistics dump.** New flag `--dump <file.csv>`: one row per frame with `t_s, section, scene_to, max_byte, lit_pixels (any channel byte > 0), mean_byte, current_a, power_scale, flash_limited`. Bytes are after `to_bytes`. This replaces the screen-gamma estimates in the review.
3. **Dark share before the cap.** Add a second figure to the existing line: the share of pixels whose largest channel is under 0.05 in the frame before the limiters. Keep the existing figure.
4. **Per-ring summary.** Add `--groups`: for each strip group in the rig, the mean and maximum byte over the track, and the share of frames in which the group is fully dark.
5. **Baseline.** Run the tool on the synthetic set and store the output under `app/tests/golden/` beside the existing golden metrics, so later phases show their effect as a diff. For the owner's private tracks, write to `captures/review/` (gitignored), never to the repo.

Acceptance: on a labelled track the scene shares follow the labels; `--detected-sections` reproduces the old output byte for byte.

## 3. Phase 2: L1, the power estimate

Files: `app/src/engine/limiters.h`, `limiters.cpp`, `app/src/io/project.cpp` (limiter block at about 568 and 705), every caller of `estimated_current_a` (grep; the preflight power check and the GUI's status line are two), `app/tests/limiters_test.cpp`.

```cpp
enum class PowerModel { linear, duty };   // linear: today's estimate.
struct LimiterParams {
    ...
    PowerModel power_model = PowerModel::duty;
    double quiescent_ma = 1.0;            // Per pixel, drawn whatever the colour. [Verify]
};
```

- **Estimate.** Add an overload; keep the existing two-argument function untouched for the `linear` model.

```
current_a(frame, p):
  linear: sum over channels of level * p.current_per_channel_ma * 1e-3            (as now)
  duty:   sum over channels of (byte(level, p.gamma) / 255) * p.current_per_channel_ma * 1e-3
          + pixels * p.quiescent_ma * 1e-3
```

  `byte` must be the same function `to_bytes` uses, `kVisibleFloor` rule included: factor the lambda at `limiters.cpp:178` into a file-local `to_byte(level, gamma)` and call it from both. `to_bytes` output must not change (test).
- **Scale under the budget (duty).** The current is not linear in the scale s. With I the estimate, q the quiescent total and B the budget:
  1. If `I <= B`: s = 1.
  2. If `B <= q`: s = 0 (and the report says so).
  3. First guess `s = pow((B - q) / (I - q), 1 / gamma)`.
  4. Evaluate `I(s)`. While `I(s) > B`, multiply s by 0.98, at most 16 times. (The visible floor and rounding make I(s) step, so the closed form can land just over.)
  5. `report.power_scale = s`.
- **Order of limiters** stays: caps, power, flash. `report.current_a` is the estimate of the final frame under the active model.
- **Project file:** `"power_model": "duty"`, `"quiescent_ma": 1.0`.
- **GUI:** in the Limiters tree (`PreviewView::draw_controls`, about line 884), Tune tier: a combo "Power estimate" with "From duty" and "Linear (older)", and a knob "Idle per pixel" in mA. Tooltip: what each means in one sentence.
- **Tests:**
  - `linear` reproduces the old numbers on the existing cases.
  - `duty`, gamma 2.2, 408 pixels of level 0.4375 white, 20 mA, q = 0: byte 41, 3.94 A within 0.01.
  - With `budget_a = 3.75` on that frame: the output bytes are 39 or 40 on every channel and the estimate is at most 3.75.
  - Monotonic: a brighter frame never estimates lower.
  - `B <= q` gives black.
  - Deterministic: the same frame twice gives the same scale.
- **For the owner, in `docs/PROGRESS.txt`:** the measurement still to make (All on at 25, 44, 70 and 100% against a meter), and that `current_per_channel_ma` and `quiescent_ma` are to be set from it.

## 4. Phase 3: units and the kick's start (L2, L6)

### 4.1 Shared helpers (new file `app/src/engine/shape.h`, `shape.cpp`)

```cpp
// The rig's reach: the largest distance of a pixel from the bounding box's centre, as
// map_pixels' radial mapping uses. At least 1e-3.
double rig_reach_m(const Rig &rig);
inline constexpr double kReferenceReachM = 1.0;   // Sizes in "rig" units are per metre of reach.
inline constexpr double kReferenceBeatS = 0.4;    // 150 BPM: beats-mode times equal seconds there.

double size_in_m(double value_m, int size_units, const Rig &rig);
//   0: value_m.   1: value_m * rig_reach_m(rig) / kReferenceReachM.
double time_in_s(double value_s, int time_units, const EngineInputs &in);
//   0: value_s.   1: value_s * (in.beat_s > 0 ? in.beat_s : kReferenceBeatS) / kReferenceBeatS.

// A hit's envelope: 1 for hold_s, then exp(-(age - hold_s) / decay_s). 0 for age < 0.
double hit_envelope(double age_s, double hold_s, double decay_s);

// easeOutBack on 0..1 (1 beyond): 1 + (s + 1)(x - 1)^3 + s (x - 1)^2, s = 17.0158 * overshoot.
// overshoot 0.1 passes 1 by about 10%; 0 is a cubic ease-out.
double overshoot_ease(double x, double overshoot);
```

Speeds scale as sizes do (`speed_m_per_s` through `size_in_m`).

### 4.2 `size_units` (L6)

For each of `ShockwaveParams`, `BurstsParams`, `NoiseParams`, `PlasmaParams`, `BuildParams`, `DropBurstParams`, `FireParams`, `InterferenceParams`:

- Add `int size_units = 1;` as the last member, and a last field `{"size_units", "Sizes", "", 0, 1, true, "Sizes and speeds in metres, or relative to the rig's reach", "Metres|Rig"}`.
- In `render`, pass every `_m` and `_m_per_s` member through `size_in_m` once, before the pixel loop.
- On a rig with a reach of exactly 1 m the two modes are identical: assert it in a test per effect.

### 4.3 `time_units` (L6)

For `ShockwaveParams::decay_s`, `BurstsParams::decay_s`, `HexPulseParams::decay_s`, `DropBurstParams::decay_s` (and `duration_s`): add `int time_units = 1;` with options `"Seconds|Beats"`, and pass the value through `time_in_s`. At 150 BPM the modes are identical: assert it.

### 4.4 Hex pulse start (L2)

- `HexPulseParams`: add `int start = 1;` (`"Centre|First pixels"`) and `double hold_s = 0.0;` (0 to 0.2; off by default, decision 3).
- In `HexPulse::render` (`app/src/engine/hex_effects.cpp`, the `front` line): with `u_min` the smallest `u[i]`,

```
start == 0:  front = direction == 0 ? travelled : 1 - travelled                     (as now)
start == 1:  front = direction == 0 ? u_min + travelled * (1 - u_min)
                                    : 1 - travelled * (1 - u_min)
fade = hit_envelope(age, hold_s, time_in_s(decay_s, time_units, in))                (hold 0: as now)
```

- Tests: on the mandala at `since_kick_s = 0`, `start = 1` lights the inner ring (at least 0.9 of level on every inner-ring pixel); `start = 0` reproduces the existing test "a kick's hexagon lights a whole ring at once" unchanged.

### 4.5 The new mapping (L9), and variations

- `Mapping::along_length` appended (value 8). `kMappingCount = 9`. Options string gains `|Along, by length`.
- `map_pixels`: `u = ((index + 0.5) * pitch_of_strip) / longest_strip_length`, where a strip's length is `count / pixels_per_m` and the longest is over the rig. So u runs 0 to 1 only on the longest strip; shorter strips end early. Effects that wrap u (Chase, Wave) therefore run at one physical speed on every strip.
- Raise the max of every existing `mapping` field to 8.
- **Keep existing variations stable:** in `variations.cpp`, where a remap picks among `info.max - info.min + 1` values, cap the count at 8 for fields whose id is `mapping` (a named constant `kRemapMappingCount = 8`). Add a test that every built-in pack's variations are identical before and after this phase (compare `all_variations` output against a golden serialisation made on `main`).

## 5. Phase 4: automation (L3, L11)

### 5.1 Held sections (L3)

File: `app/src/engine/automation.cpp` (`held` at 28, `own_section` at 39, `phrase_s` at 272).

```cpp
enum class HoldRule { strict, tolerant };
struct AutomationParams { ... HoldRule hold_rule = HoldRule::tolerant; };
```

- Thread the rule into `held`, `own_section` and `occurrence_of` (they are file-local; add a parameter).
- `tolerant`:
  1. `held(s)` is `label not in {build, drop} && end - start < min_dwell_s - kPhraseToleranceS` (the constant at line 56, 0.05 s).
  2. `own_section` walks back while the section at `index` is held **or** is a build shorter than `min_dwell_s - kPhraseToleranceS` that is not the section the walk started from. A short build still shows the build scene during its own bars; sections held after it inherit from before it.
- `strict`: today's code path, untouched.
- `automate_live` is not changed.
- Tests (`app/tests/automation_test.cpp`): build the section list from these real lengths at 183.9997623150804 BPM: groove 52.2 s, breakdown 1 bar, build 1 bar, groove 10.434796088009065 s, groove 10.434796088009051 s, groove 16 bars. Under `tolerant` the three grooves after the build show a groove scene; under `strict` the first two show the build scene (documenting the old behaviour). Also: a 4-bar groove after a 1-bar build inherits the groove before the build.

### 5.2 Palette policy (L11), off by default

```cpp
enum class PaletteRotation { all, scene_list };       // all: today's shift through every palette.
struct AutomationParams { ... PaletteRotation palette_rotation = PaletteRotation::all; };
struct Scene { ... std::vector<int> palette_list; };   // Empty: only `palette`.
```

- `scene_list`: the palette index for a scene with shift k is `palette_list[k mod size]` when the list is not empty, else `palette`. Slewing between two shifts works as now, on the two resulting indices. Implement in `palette_index` (line 33) by passing the scene.
- Project and pack serialisation: `"palette_rotation": "all" | "scene_list"`, scene `"palette_list": [..]` (omit when empty).
- GUI: Automation tree: combo "Palette rotation" ("Every palette" / "Each scene's own list"). Scene editor: a multi-select of palettes under "Palette (base)". Tune tier.
- Test: with `all`, `render_mix` output is unchanged for every built-in pack at 20 sample times.

## 6. Phase 5: hexagon topology and five stateless effects

### 6.1 Topology helper (add to `engine/space.h`, `space.cpp`)

```cpp
struct HexTopology {
    HexFrame frame;
    double u_min = 0.0;                 // Smallest hex radius of any pixel.
    std::vector<int> ring_of_strip;     // Per strip: 0 innermost ring, 1, ...; -1 for a spoke or other.
    std::vector<int> rings;             // Strip indices of the rings, innermost first.
    std::vector<int> spokes;            // Strip indices of the open strips.
    std::vector<double> radius;         // Per pixel: hex_radius.
    std::vector<double> turn;           // Per pixel: hex_turn, 0 at the top, clockwise.
    std::vector<int> first_pixel;       // Per strip: index of its first pixel in rig.pixels.
};
HexTopology hex_topology(const Rig &rig, int corners = 0);
```

- A strip is a ring when its polyline is closed (first and last points within 1 mm) and the spread of its pixels' hex radius is under 0.1; rings sort by mean radius. Every other strip is a spoke. Group names are not used, so any hexagonal rig works.
- `corner_turn(k) = frac((90 - frame.corner_deg) / 360 + k / 6.0)` for k = 0..5 (check against `hex_turn`'s convention with a test: on the mandala the six outer-ring pixels nearest those turns are the ones nearest the polyline's corners).
- Cost: O(pixels). It is called once per `render`; measure, and if it shows in the cost table add a cache keyed on the `Rig` address and pixel count inside the effect object (effects are immutable and shared, so the cache needs a mutex or `std::call_once` per rig; prefer measuring first).
- On a rig with no rings every effect below renders black, and the Show tab shows nothing special. Do not assert.

### 6.2 Common parameters

Used below without repeating: `level` (0 to 1), `colour_spread` (0 to 2, palette span), `confidence` (0 to 1: level is multiplied by `1 - confidence * (1 - in.bass_confidence)`; default 0.5 on bass effects), `hold_s`, `decay_beats`.

Trigger lists in the new effects include one more value than the old ones:

```
"Kick|Beat|Bar|Bass note|Hat|Kick, else beat"
Kick, else beat: the kick, unless in.section is groove or drop, in.beat_s > 0 and
since_kick_s > 1.25 * beat_s, in which case the beat (index in.beat_index, age beat_phase * beat_s).
```

Put the trigger resolution in `shape.h` as `HitTrigger resolve_trigger(const EngineInputs &, int trigger)` returning `{index, since_s, spacing_s, ages}`; it mirrors the file-local `latest()` in `hex_effects.cpp`, which stays where it is.

### 6.3 `pitch_clock` (Bass; L5)

Twelve pitch classes round the hexagon, the octave picks the ring.

| id | Label | Range | Default | Meaning |
|---|---|---|---|---|
| `low_midi` | Lowest | 0 to 96, int | 23 | Octave 0 starts here (B0) |
| `ring_order` | Low notes | 0 to 1, int, `Inside|Outside` | 0 | Which ring the lowest octave takes |
| `width` | Width | 0.01 to 0.25 turns | 0.04 | Half-width of the marker |
| `top_pitch` | At the top | 0 to 11, int | 0 | Pitch class at the top corner (0 = C) |
| `spokes` | Spokes | 0 to 1, int, `Off|On` | 1 | Light the spoke whose turn is within `width` of the note |
| `release_s` | Release | 0 to 1 s | 0.08 | Fade after the note ends |
| `colours` | Colour | 0 to 1, int, `Pitch hue|Palette` | 0 | As Bass bars |
| `confidence`, `level` | | | 0.5, 1.0 | |

```
if bass_notes_so_far == 0: black
pitch  = bass_midi + bass_cents / 100
angle  = frac((pitch - top_pitch) / 12)
octave = clamp(floor((bass_midi - low_midi) / 12), 0, rings - 1)
ring   = ring_order == 0 ? octave : rings - 1 - octave
env    = bass_active ? bass_velocity * note_decay(in)          # the conveyor's envelope: see below
                     : bass_velocity * exp(-since_bass_offset_s / release_s)
d      = circular distance in turns between turn[i] and angle
v      = level * conf * env * exp(-(d / width)^2)      for pixels of strip rings[ring]
         (and of spokes when spokes == 1, with d measured to the spoke's mean turn)
colour = colours == 0 ? hsv(270 + 28 * pitch, 1, 1) : palette.at(angle)
```

`note_decay`: read how `Conveyor` and `BassBars` derive brightness from `bass_decay_tau_ms` and `bass_note_phase` and use the same expression (copy it into `shape.h` as a function; leave the two effects' code as it is).

Tests: MIDI 35 (B1) with `low_midi` 23 lights only the middle ring, centred on turn 11/12; MIDI 23 lights the inner ring at the same turn; `cents = 50` moves the peak by 1/24 turn; no note yet renders black.

### 6.4 `note_arcs` (Bass; L5)

| id | Label | Range | Default | Meaning |
|---|---|---|---|---|
| `ring_mode` | Rings | 0 to 2, int, `Cycle|By slot|Outer only` | 0 | Which ring a note draws on |
| `arc_per_16th` | Length | 0.02 to 0.5 turns | 0.1667 | Arc drawn per 16th held (1/6: one side) |
| `direction` | Direction | 0 to 2, int, `Clockwise|Anticlockwise|Alternate per bar` | 2 | |
| `release_s` | Release | 0 to 1 s | 0.06 | |
| `breathe` | Long notes | 0 to 1 | 0.3 | Depth of a 2 Hz swell once a note has run 2 slots |
| `history` | Notes shown | 1 to 8, int | 1 | Past notes drawn; above 1 needs phase 9 |
| `colours`, `confidence`, `level` | | | 0, 0.5, 1.0 | |

```
slot_s = beat_s / 4                         (beat_s == 0: 0.1)
n      = bass_notes_so_far - 1
ring   = Cycle: n mod rings | By slot: (16th slot of the onset within its beat, 1..3 -> 0..2, 0 -> 0)
         | Outer only: rings - 1
start  = corner_turn(n mod 6)
held_s = bass_active ? since_bass_onset_s : bass_gate_ratio * slot_s
len    = arc_per_16th * held_s / slot_s     (clamped to 1 turn)
env    = bass_active ? 1 : exp(-since_bass_offset_s / max(release_s, bass_decay_tau_ms / 1000))
swell  = held_s > 2 * slot_s ? 1 + breathe * sin(2 pi * 2 * t_s) * 0.5 : 1
a      = frac((turn[i] - start) * dir)      dir = +1 or -1
v      = a < len ? level * conf * bass_velocity * env * swell * edge(a, len) : 0
edge   : smoothstep over the last pixel pitch at both ends, so a growing arc does not jump
```

The onset's slot for "By slot": `round((t_s - since_bass_onset_s - bar start) / slot_s) mod 4`, from `bar_phase`, `bar_index` and `beat_s`.

Tests: gate 0.5 at a 16th draws half the arc of gate 1.0; a sounding note's arc grows monotonically with `since_bass_onset_s`; at most `len` of one ring is lit (never more than 40% of the rig at the defaults).

### 6.5 `spoke_kick` (Kick; L2)

| id | Label | Range | Default |
|---|---|---|---|
| `trigger` | Trigger | list of 6.2, int | 5 (Kick, else beat) |
| `every` | Every | 1 to 16, int | 1 |
| `travel_beats` | Travel | 0.125 to 4 | 0.5 |
| `overshoot` | Overshoot | 0 to 0.3 | 0.1 |
| `tail` | Tail | 0.05 to 1 (share of the spoke) | 0.35 |
| `hold_s` | Hold | 0 to 0.2 s | 0.04 |
| `decay_beats` | Decay | 0.1 to 4 | 0.75 |
| `rings` | Rings | 0 to 1, int, `Off|Flash as it passes` | 0 |
| `colours` | Colour | 0 to 1, int, `Palette|White` | 0 |
| `level` | | | 1.0 |

```
w      = (radius[i] - u_min) / (1 - u_min)                # 0 at the inner end, 1 at the corner
x      = since_s / (travel_beats * beat_s)
head   = overshoot_ease(min(x, 1), overshoot)
env    = hit_envelope(since_s, hold_s, decay_beats * beat_s)
spokes : v = w <= head ? level * env * max(0, 1 - (head - w) / tail) : 0
         and at since_s < hold_s every spoke pixel with w < 0.1 is at level (the hit is on the kick)
rings  : when rings == 1, ring r lights with level * env * exp(-((w_r - head) / 0.08)^2)
```

Only the latest trigger is drawn. Tests: at `since_s = 0` the spokes' inner ends are lit and nothing beyond `w = 0.1`; at `x = 1` the head is at the corner; for `x` in 0.6 to 0.9 with overshoot 0.1 the lit length exceeds... (it cannot exceed the spoke: assert instead that `head > 1` there and that the outermost pixel is at full tail level).

### 6.6 `corner_sparks` (Hats)

| id | Label | Range | Default |
|---|---|---|---|
| `trigger` | Trigger | list of 6.2, int | 4 (Hat) |
| `span` | Size | 1 to 8 px, int | 3 |
| `count` | Corners | 1 to 6, int | 2 |
| `keep` | Echoes | 1 to 4, int | 3 |
| `decay_s` | Decay | 0.02 to 0.5 s | 0.08 |
| `sites` | Where | 0 to 2, int, `Ring corners|Spoke ends|Both` | 2 |
| `colour_spread`, `level` | | | 1.0, 0.8 |

```
sites  : for each ring, the pixel nearest corner_turn(k), k = 0..5; for each spoke, its outer end.
for the last `keep` events e (ages from hat_ages_s / bass_onset_ages_s, or index and spacing):
    for j in 0..count-1:
        site = floor(hash_unit(seed, e, 211 + j) * sites.size())
        light the pixels within `span` along the strip of that site (rings wrap; spokes clamp)
        v = level * exp(-age / decay_s) * (1 - distance_px / (span + 1));  colour palette.at(site / sites.size() * colour_spread)
combine with max.
```

Tests: lit pixels are all within `span` of a site; two events with the same index give the same sites; a different seed gives different ones.

### 6.7 `polar_noise` (Ambient; L6)

| id | Label | Range | Default |
|---|---|---|---|
| `rings_scale` | Rings | 0.25 to 8 (noise periods centre to edge) | 2.0 |
| `lobes` | Lobes | 1 to 12 (periods round the circle) | 3.0 |
| `clock` | Clock | `kClockOptions`, int | 2 (Beat) |
| `flow` | Flow | -2 to 2 (radii per clock division; positive: outwards) | 0.125 |
| `octaves` | Detail | 1 to 4, int | 2 |
| `contrast` | Contrast | 0.5 to 4 | 1.5 |
| `floor` | Floor | 0 to 0.5 | 0.0 |
| `brightness_colour` | Timbre | 0 to 1 | 0.3 (as Noise) |
| `colour_spread`, `level` | | | 0.8, 0.5 |

```
r = radius[i] * rings_scale - flow * clock_cycles(in, clock, 1.0)
a = 2 pi * turn[i];  R = lobes / (2 pi)
n = fbm(r, R * cos(a), R * sin(a), octaves, seed)        # the fbm in pattern_effects.cpp: move a copy to shape.h
shown = max(floor, clamp(0.5 + (n - 0.5) * contrast, 0, 1))
```

Tests: continuous across turn 0 and 1 (two pixels either side of the top differ by less than 0.05 at `lobes = 3`); with `flow = 0` and a free clock at rate 0 it does not move; at `rings_scale = 2` the three rings never all read 0 at once over 60 s (sample every 0.5 s).

## 7. Phase 6: phrase-scale effects (L7, L12)

Both take their position in the phrase from `in.phrase_phase` and a parameter for the step count, so no new input is needed. Before the first phrase (`phrase_index < 0`) they use `frac(bar_index / 8.0 + bar_phase / 8.0)` clamped at 0.

### 7.1 `phrase_fill`

| id | Label | Range | Default |
|---|---|---|---|
| `order` | Order | 0 to 3, int, `By side|Inner to outer|Spokes, then rings|Random pixels` | 0 |
| `steps` | Steps | 4 to 128, int | 32 |
| `hold_level` | Held | 0 to 1 | 0.25 |
| `head_level` | Newest | 0 to 1 | 1.0 |
| `head_decay_beats` | Newest fades | 0.1 to 4 | 1.0 |
| `clear_beats` | Clear | 0 to 4 | 0.5 |
| `colour_spread`, `level` | | | 1.0, 0.8 |

```
k        = floor(phrase_phase * steps)                         # steps done
units    : By side: each ring's six sides (pixels by turn sector between corners) then each spoke,
                    ordered ring by ring from the inside, clockwise from the top (18 + 6 on the mandala)
           Inner to outer: pixels sorted by radius, then turn
           Spokes, then rings: spoke pixels by radius, then ring pixels by ring and turn
           Random pixels: pixels sorted by hash_unit(seed, i, 97)
lit_count = round(k / steps * units.size())                    # units, or pixels for the pixel orders
a unit u is lit when its rank < lit_count; its age in steps = k - first step at which it lit
v = level * (hold_level + (head_level - hold_level) * exp(-age_steps * step_beats / head_decay_beats))
step_beats = 32.0 / steps                                      # an 8-bar phrase taken as 32 beats
clear: in the last clear_beats / 32 of the phrase, multiply by the remaining fraction (a fade to black on the line)
```

Tests: at phase 0 nothing is lit; lit count never decreases within a phrase; at phase 0.999 with `clear_beats = 0` every unit is lit; the same seed gives the same order.

### 7.2 `ring_automaton`

| id | Label | Range | Default |
|---|---|---|---|
| `rule` | Rule | 0 to 255, int | 90 |
| `generations` | Steps a phrase | 8 to 256, int | 64 |
| `seed_mode` | Seed | 0 to 2, int, `Corners|Random|One cell` | 0 |
| `density` | Density | 0.02 to 0.5 (Random) | 0.15 |
| `spokes` | Spokes | 0 to 1, int, `Off|Carry their corner's cell` | 0 |
| `crossfade` | Crossfade | 0 to 1 (share of a step) | 0.25 |
| `colour_spread`, `level` | | | 0.6, 0.8 |

```
g  = min(floor(phrase_phase * generations), 512);   f = frac(phrase_phase * generations)
per ring (cells = strip.count, cell j = the pixel with index j, periodic):
    seed row: Corners: the cells nearest corner_turn(0..5) alive
              Random:  cell j alive when hash_unit(seed, phrase_index, 1000 * ring + j) < density
              One cell: the cell nearest the top
    run g - 1 generations -> prev, one more -> now:   next[j] = (rule >> (4 * c[j-1] + 2 * c[j] + c[j+1])) & 1
    cell value = crossfade > 0 && f < crossfade ? lerp(prev, now, f / crossfade) : now
v = level * value;  colour = palette.at(colour_spread * ring / rings)
spokes == 1: a spoke takes the value of the cell at its corner on the ring where its inner end lands.
```

Use two `std::vector<std::uint8_t>` buffers per ring, swapped. Worst case 512 x 294 cell updates: measure; if over budget, lower the cap to 256 and say so in the header comment.

Tests: rule 90 from the corner seed on the mandala (156, 96, 42) stays six-fold symmetric for 64 generations (value at cell j equals value at cell j + count / 6); generation 0 is the seed; `phrase_phase` 0.5 with 64 generations is generation 32; deterministic.

### 7.3 Peak hold and dithering (L7), both off by default

- **Peak hold:** `hold_s` already added to Hex pulse (4.4). Add the same field (default 0) to `ShockwaveParams`, `BurstsParams` and `DropBurstParams`, through `hit_envelope`.
- **Spatial dithering:**

```cpp
enum class Dither { off, spatial };
struct LimiterParams { ... Dither dither = Dither::off; };
std::vector<std::uint8_t> to_bytes(const Frame &, double gamma, Dither, std::int64_t frame_index);
```

  `spatial`: `byte = floor(255 * level^gamma + hash_unit(pixel, frame_index, channel))`, clamped to 255, with no visible-floor substitution (the dither carries the fraction). The existing two-argument `to_bytes` stays and is what `off` calls.
  `ShowRenderer::render` passes its `index_`. `psyight_replay` rebuilds the same indices from the recording (it already re-renders in recorded order; check a gap in indices is handled by using the recorded index, not a counter).
  GUI: Limiters tree, Engineer tier, checkbox "Spatial dithering (try on the piece)".
  Tests: the mean over 4,096 pixels of level L equals `255 * L^gamma` within 0.02 byte for L in 0.02 to 0.4; the same frame index gives the same bytes; `off` is byte-identical to today.

## 8. Phase 7: inputs, additions only (L4c, L10)

Files: `app/src/engine/inputs.h`, `inputs.cpp`, `modulation.h`, `modulation.cpp`. Existing fields keep their values (test: for 200 sample times on the synthetic set, every pre-existing field of `EngineInputs` is bit-identical before and after).

```cpp
struct EngineInputs {
    ...
    // A run of consecutive build sections, taken as one (L10). Equal to the section's own
    // values when the neighbours are not builds; 0 outside builds.
    double build_progress = 0.0;     // 0 to 1 through the run.
    double since_build_s = 0.0;
    double build_length_s = 0.0;
    bool build_end_known = false;    // As section_end_known.
};
```

- File mode: walk back and forward over adjacent sections labelled `build` (in the timeline when present). Live mode: copy the section's own values.
- `ModSource::bass_confidence` and `ModSource::build_progress` appended; `kModSources` grows to 30; ids `"bass_confidence"`, `"build_progress"`.
- `StutterParams::mode` gains a fifth option, `"Pre-drop gap, joined builds"`, which is mode 3 computed from the `build_*` fields. Mode 3 is unchanged.
- No existing effect reads the new fields.

## 9. Phase 8: frame memory (afterglow, radial echo)

### 9.1 Structure

```cpp
enum class PostType { afterglow, radial_echo };          // Append only.
struct AfterglowParams  { double decay_beats = 1.0; double amount = 1.0; };
struct RadialEchoParams { double speed = 1.0;        // Reaches per beat; negative: inwards.
                          double decay_beats = 1.0;
                          double feed = 1.0;         // How much of the scene enters the echo.
                          double mix = 0.7; };       // How much of the echo is shown.
struct PostSpec { std::variant<AfterglowParams, RadialEchoParams> params; bool enabled = true; };
struct Scene { ... std::vector<PostSpec> post; };        // Applied in order after the layers.

struct PostState { Frame frame; std::vector<Rgb> bins; double last_t_s = kNever; };
using PostMemory = std::vector<std::vector<PostState>>;  // Per scene, per post-effect.
```

- New files `engine/post.h`, `post.cpp`: `void apply_post(const Scene &, const Rig &, const EngineInputs &, Frame &, std::vector<PostState> *state)`. With `state == nullptr` it does nothing (the pure path: audition, `psyight_show_check --layers`, tests of scenes).
- `render_mix` gains a trailing `PostMemory *post = nullptr`; it calls `apply_post` on each scene's frame before the crossfade mix. `ShowRenderer` owns a `PostMemory post_` and clears it exactly where it clears `modulation_` (the `kSeekResetS` jump). Hold and blackout do not advance it (they skip the render).
- A state is also reset when the rig's pixel count differs from `state.frame.size()`.

### 9.2 Algorithms

```
dt = clamp(in.t_s - state.last_t_s, 0, 0.1)      (first frame, or last_t_s == kNever: dt = 0, state = black)
beat = in.beat_s > 0 ? in.beat_s : kReferenceBeatS

afterglow:
    k = exp(-dt / (decay_beats * beat))
    state.frame[i] = max(frame[i], state.frame[i] * k)          per channel
    frame[i] = max(frame[i], amount * state.frame[i])

radial_echo:   B = 64 bins over hex radius 0..1 (bin b centre (b + 0.5) / B)
    shift = speed * dt / beat * B                               bins, may be fractional or negative
    bins'[b] = lerp(bins, b - shift) * exp(-dt / (decay_beats * beat))      zero outside 0..B-1
    for each bin with pixels: m = mean colour of frame over its pixels;  bins'[b] = max(bins'[b], feed * m)
    frame[i] = max(frame[i], mix * lerp(bins', radius[i] * B - 0.5))
```

- Rendering at 30 and at 60 frames a second over the same 2 s must agree within 0.02 per channel (test): the formulas depend on dt, not on the frame count.
- Serialisation: scene `"post": [{"type": "afterglow", "enabled": true, "params": {...}}]`, omitted when empty. Presets and packs carry it.
- GUI: under the layer rack in the scene editor, a "After the layers" list with "+", a power button and knobs per entry, built like `draw_layer_device`. Tune tier.
- Replay: add a recording with an afterglow scene to the replay test; it must match byte for byte, and a recording with a dropped frame must report the gap as it does for the limiters.

## 10. Phase 9: note history

```cpp
inline constexpr int kNoteHistory = 8;
struct NoteEvent {
    double age_s = kNever;      // Since its onset.
    double length_s = 0.0;      // Sounding: time so far. Ended: its duration.
    bool sounding = false;
    int midi = 0; double cents = 0.0;
    double gate_ratio = 0.0, velocity = 0.0, confidence = 0.0, decay_tau_ms = 0.0;
    bool slide = false; bool long_note = false;
};
struct EngineInputs { ... std::array<NoteEvent, kNoteHistory> notes{}; };   // Newest first.
```

- **File mode:** from the analysis's note list, the last eight with onset at or before `t_s`. Entry 0 agrees with the existing scalar bass fields (test).
- **Live mode:** fill what `LiveState` already holds: entry 0 from the scalar fields, entries 1 to 3 with `age_s` from `recent_bass_onsets_s` and everything else zero, `confidence = 0`. Do not change `live_analysis.*`.
- **`note_arcs` with `history > 1`:** draw entries 0 to `history - 1`, each with its own ring, start corner (from `bass_notes_so_far - 1 - k`), length (`length_s`) and fade `exp(-(age_s - length_s) / release)`; skip entries with `length_s == 0`. Combine with max. With `history = 1` the output is identical to phase 5's (test).
- **`pitch_clock`** gains `trail` (0 to 7, int, default 0): earlier notes as markers at `0.5^k` of the level.

## 11. Phase 10: the "Hexagon 2" pack

File: `app/src/engine/presets.cpp`. Add scene presets after the existing ones and one `ShowPack` after `hexagon`. Group names used: `inner`, `middle`, `outer`, `spokes` (the hexagon mandala template creates them; check the Hive stage's hexagon and add the same tags to that template's strips if it lacks them, as new tags only).

These are starting points for the owner to tune on the piece, not finished looks. Keep every layer's level so that at most a quarter of the rig is above 0.5 at once in grooves (SPEC 7.2), and check with phase 1's dump.

| Scene | Sections | Palette list | Layers, bottom first (group; blend; opacity) |
|---|---|---|---|
| Pitch tunnel | groove | UV violet, Magenta storm | `pitch_clock` (all; add) · `spoke_kick` (spokes; screen) · `corner_sparks` hats (inner; add; 0.6) |
| Note rings | groove | Ocean UV, UV violet | `note_arcs` cycle (all; add) · `spoke_kick` (spokes; screen; 0.8) |
| Radar 2 | groove, high | Toxic | `sectors` 12, bass note (outer; add) · `pitch_clock` (inner, middle; add) · `spoke_kick` (spokes; screen) |
| Automaton | breakdown, low | Mono violet, Ocean UV | `ring_automaton` rule 90, 32 steps (all; add) · `corner_sparks` hats (all; add; 0.4) |
| Smoke | intro, breakdown | Ocean UV | `polar_noise` flow -0.125 (all; alpha) · `hex_pulse` bass note, start first pixels (all; screen; 0.5) |
| Pressure 2 | build | Ember | `phrase_fill` inner to outer (all; add; 0.7) · `hex_pulse` beat, in (all; screen; 0.5) · `spoke_kick` (spokes; screen) · `stutter` joined-builds gap (all; multiply). Route: `build_progress` to brightness, 0.5 |
| Bloom 2 | drop | Magenta storm | `note_arcs` outer only (outer; add) · `spoke_kick` white, rings on (all; screen) · `drop_burst` (all; screen) · `strobe` (all; screen) |
| Embers | outro | Mono violet | `ring_automaton` rule 150, 16 steps (all; add; 0.6). Post: `afterglow` 2 beats |

- Pack automation: defaults, plus `palette_rotation = scene_list`, `hold_rule = tolerant`.
- Macros (named and routed in every scene where the target exists): 1 "Darkness" (scene brightness, amount -1), 2 "Kick punch" (the kick layer's opacity), 3 "Bass size" (`arc_per_16th` or `width`), 4 "Hue" (hue shift, 180 degrees).
- Tests (`presets_test.cpp`): the pack loads; every scene renders on the mandala without NaN and is not black over a bar of the synthetic groove; `builtin_show_packs()` minus the last pack and `builtin_scene_presets()` minus the new ones serialise identically to `main` (golden JSON made on `main` first).

## 11b. The Psy club and Dark forest packs (owner, 2026-11-10)

The review measured only the Hexagon pack. These two packs must be covered as well: measured on the hexagon, checked against every default-on fix, and given counterparts in "Hexagon 2". Their scenes and the effects in them:

| Pack | Scene (sections) | Layers, bottom first |
|---|---|---|
| Psy club | Scanner (groove, mid) | Plasma · Chase across x on the bar · Conveyor · Shockwave |
| Psy club | Comet rain (groove, drop, high) | Chase on bass notes · Bursts · Stutter 16th gate |
| Psy club | Kick pulse (groove, high) | Plasma · Stutter kick duck · Wave radial on the beat · Conveyor |
| Psy club, Dark forest | Nebula (breakdown, low) | Noise · Shimmer · Bursts on hats |
| Psy club | Fire riser (build) | Fire across y · Spiral · Stutter pre-drop gap |
| Psy club | Drop bloom (drop) | Wave radial square on 16ths · Bursts · Drop burst · Strobe |
| Dark forest | Forest floor (groove, low, mid) | Noise · Conveyor · Shockwave |
| Dark forest | Pitch meter (groove) | Wave across y on the phrase · Bass bars · Shockwave |
| Dark forest | Acid gate (groove, high) | Wave ramps along strip on 16ths · Stutter bass gate · Shockwave |
| Dark forest | Strobe roll (build) | Spiral · Strobe at the build's end |
| Dark forest | Drop impact (drop) | Plasma · Conveyor · Shockwave · Drop burst |
| Both | Deep space (intro, outro) | Noise · Spiral |

### 11b.1 Measure them first (add to phase 1)

- `psyight_show_check --pack <name>`: applies a built-in pack to the loaded project in memory (`apply_show_pack`, as the Show tab does) before rendering. The project file is not written.
- Baseline, on the hexagon project, for `Hexagon`, `Psy club` and `Dark forest`: the `--dump` and `--groups` output on the synthetic set (into `app/tests/golden/`) and, for the owner, on Alien Army and OculusDrift (into `captures/review/`). Report per scene: maximum byte, median lit pixels, share of frames with more than a quarter of the rig above byte 20, flashes alone.
- Extend `captures/review/alien/sheets.py` with a pack argument and make the contact sheets for both packs, so each default-on fix has a before and after picture. This is evidence for the owner, not a test.

### 11b.2 What the default-on fixes do to these scenes (verify each, report in PROGRESS)

| Fix | Scenes it reaches | Expected on the hexagon |
|---|---|---|
| `size_units` rig | Scanner, Kick pulse, Drop impact (Plasma 1.0 to 1.6 m); Nebula, Forest floor, Deep space (Noise); Fire riser (Fire 0.25 m, 0.8 m/s); Scanner, Pitch meter, Acid gate, Forest floor, Drop impact (Shockwave); Comet rain, Nebula, Drop bloom (Bursts); Drop bloom, Drop impact (Drop burst) | Fields gain structure across the piece; the kick's Shockwave becomes a ring 5 cm wide crossing in about 170 ms, where it was a two-frame whole-rig flash |
| `time_units` beats | Every Shockwave, Bursts and Drop burst above | Unchanged at 150 BPM; shorter above it |
| `power_model` duty | Drop bloom, Drop impact (white burst), Strobe roll (white strobe), every white Shockwave | The white hits stop being scaled to byte 4 |
| `hold_rule` tolerant | All (automation) | No build scene left on after a one-bar build |
| Hex pulse `start` | None (no Hex pulse in these packs) | |

- Shockwave is white (SPEC 7.5). Once the power model stops crushing it, six scenes get a white ring on every kick at up to byte 41. Check the flash counts in the dump against the limiter's 3 a second with the limiter on, and report them: the owner runs with it off.
- If a scene looks worse with a fix on, do not retune the preset. Report it; the owner can set that layer's `size_units` or `time_units` back per scene.

### 11b.3 Gaps these packs expose, as additions (all off by default)

1. **Conveyor on short strips.** `segment_pixels` is in pixels (12) with one segment lit in five: a 19-pixel spoke shows at most one partial segment. Add `ConveyorParams::segment_units` (`"Pixels|Share of the longest strip"`, default 0) with `segment_share` (0.01 to 0.5, default 0.08), so a segment is the same physical length on every strip.
2. **Bass bars on a hexagon.** Add `BassBarsParams::wrap` (`"Off|Octave"`, default 0): with the Around mapping and wrap on, `at = frac((pitch - low_midi) / 12)`. This is the Pitch meter's hexagon form without a new effect.
3. **Fire has no "up" on a ring.** Add nothing to Fire. For the hexagon, the counterpart below uses Polar noise flowing outwards with the riser.
4. **Strobe and Wave square on 16ths** (Drop bloom, Acid gate) are whole-strip brightness at the 16th rate, 12.3 a second at 184 BPM. No new setting: the counterparts below confine 16th-rate layers to one strip group.

### 11b.4 Counterparts in "Hexagon 2"

Add these six scenes to the table in section 11, built from the same ideas with the new effects. Each keeps its source scene's palette and section tags.

| Scene | From | Layers, bottom first (group; blend; opacity) |
|---|---|---|
| Scanner 2 | Scanner | `polar_noise` level 0.2 (all; alpha) · `chase` Around, bar clock, 1 comet (outer; add) · `conveyor` share units (middle; add; 0.7) · `spoke_kick` (spokes; screen) |
| Kick pulse 2 | Kick pulse | `polar_noise` (all; alpha; 0.4) · `spoke_kick` rings on (all; screen) · `conveyor` share units (outer; add; 0.5) |
| Forest floor 2 | Forest floor | `polar_noise` rings 3, lobes 5, flow 0.06 (all; alpha) · `note_arcs` (outer; add; 0.6) · `corner_sparks` hats (inner; add; 0.4) |
| Pitch meter 2 | Pitch meter | `phrase_fill` inner to outer, held 0.15 (all; add; 0.5) · `pitch_clock` trail 3 (all; add) · `spoke_kick` (spokes; screen; 0.6) |
| Acid gate 2 | Acid gate | `wave` ramps on 16ths (outer; add) · `stutter` bass gate (outer; multiply) · `spoke_kick` (spokes; screen; 0.7). Route: residual brightness to hue, as the source |
| Riser 2 | Fire riser, Strobe roll | `polar_noise` flow routed from `build_progress` (all; alpha) · `ring_automaton` 128 steps (inner, middle; add) · `strobe` build end (spokes; screen) · `stutter` joined-builds gap (all; multiply) |

- The pack's rotation then has four groove scenes at low to mid energy (Pitch tunnel, Note rings, Scanner 2, Forest floor 2), three at high (Radar 2, Kick pulse 2, Acid gate 2), three builds and breakdowns, two drops and two for intro and outro. Tag energies so `energy_scenes` can choose.
- Tests: as section 11, for all fourteen scenes.

## 12. Verification

| Check | How |
|---|---|
| Unit tests per effect | A new `app/tests/hex2_effects_test.cpp`, in the style of `hex_effects_test.cpp` (its `mandala()`, `render()`, `grid_at()` helpers: move them to a shared test header rather than copying). Each effect: the cases listed above, plus determinism (two renders equal) and no channel outside 0 to 1 |
| Cost | A doctest case, skipped by default (`doctest::skip()`), that renders each new effect 2,000 times on a 1,500-pixel rig and prints ms per frame. Run it in Release and paste the table into PROGRESS. Budget in section 1 |
| Golden metrics | The existing regression test must pass untouched for analysis. For the show: phase 1's `show_check` baseline on the synthetic set, diffed after each phase; explain every changed line in the commit message |
| Compatibility | The tests named in 1.1, 4.5, 5.2, 8, 10 and 11 |
| Replay | A recording made on `main` replays identically on the branch; a recording made on the branch with the new pack replays identically |
| The owner's tracks | Not in CI. After phases 2 to 4, run `psyight_show_check --dump` on Alien Army and report in PROGRESS: the drop's first-beat maximum byte (expect about 37 at the 44% cap, was about 3), the scene at 131, 142 and 194 s (expect a groove or breakdown scene, was Hex pressure), and the byte on the inner ring at a kick (expect lit at the kick frame under `start = 1`) |
| On the piece | The owner's. List in PROGRESS what to look at per phase; do not mark a visual item done from a capture alone |

## 13. Commit order

1. Tools (section 2 and 11b.1), with the baselines for all three packs.
2. Power model (3).
3. `shape.h`, `size_units`, `time_units` (4.1 to 4.3).
4. Hex pulse start and hold (4.4).
5. `along_length` mapping and the variations guard (4.5).
6. Hold rule (5.1). 7. Palette rotation (5.2).
8. Hex topology (6.1). Then one commit per effect: 9 `pitch_clock`, 10 `note_arcs`, 11 `spoke_kick`, 12 `corner_sparks`, 13 `polar_noise`, 14 `phrase_fill`, 15 `ring_automaton`.
16. Peak hold on the older hits. 17. Dithering.
18. Inputs additions and modulation sources (8).
19. Post-effects (9). 20. Note history (10).
21. Conveyor share units and Bass bars wrap (11b.3). 21b. The pack, fourteen scenes (11, 11b.4). 22. Docs sweep: SPEC 7.3, 7.4, 7.7, 10 and 16, the operator guide, PROGRESS.

Stop and ask the owner if: a compatibility test in 1.1 cannot be met without changing an existing assertion; the power model's tests disagree with section 3's numbers; an effect cannot meet the cost budget; or a step needs a change under `analysis/` or `firmware/`.

## 14. Open points, marked for the owner

- `quiescent_ma` default 1.0 and `current_per_channel_ma` 20 are unmeasured [Verify with the meter].
- `kReferenceReachM = 1.0` decides how large every older effect becomes on every rig once `size_units` defaults to rig. On the hexagon (reach 0.2 m) a burst's width goes from 0.25 m to 0.05 m. On a stage rig with a 3 m reach it triples. Confirm on the Hive stage preview before merging.
- Whether the Hive stage's hexagon should carry the `inner`, `middle`, `outer` and `spokes` tags.
- Dithering stays off until seen on the piece.
