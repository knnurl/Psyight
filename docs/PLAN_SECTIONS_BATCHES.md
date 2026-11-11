# Implementation spec: sections round 2, in six batches

Written 2026-11-11 for the agent that implements it (Claude Opus, working in this repository). It turns the open work of `docs/PLAN_SECTIONS_EFFECTS.md` into batches. Read first, in this order: `CLAUDE.md`, the plan's "Status" and "Assessment" sections, `docs/research/SECTIONS_PSYTRANCE.md` ("What this means for Psyight"), SPEC 6.5, 6.6, 6.6.1 and 7.6, and `docs/PLAN_HEX_VISUALS.md` section 1.1 (the compatibility contract, which applies here unchanged). This file does not repeat their reasoning.

Symbols and line numbers are as of commit `7871d43`. Re-locate by symbol if they have moved.

## 0. What this is for

The section detector was tuned for stability and now under-segments some tracks (Alien Army: 11 detected sections against 36 labelled, recall 0.29). Its finer detail (breaks, fills, pre-drops, phases) is detected and scored but reaches no light. Live mode changes scene on every section change it declares, with no dwell. The batches fix those in the owner's priority order: detection accuracy first, then visual creativity, then reliability.

One batch is one reviewable unit: it ends with the build clean, `ctest --preset windows-msvc-release` passing, its numbers in `docs/PROGRESS.txt`, and a stop for the owner. Do not start the next batch in the same session unless the owner said so.

| Batch | Changes behaviour by default? | Depends on |
|---|---|---|
| A. Measuring tools | No | Nothing |
| B. Boundary placement | Only if its gate passes | A |
| C. Block novelty | Only if its gate passes | A (B's result if adopted) |
| D. Detail reaches the engine | No (labelled events only, new options off) | Nothing; independent of B and C |
| E. Live mode | No (new options off until the owner switches them) | A (item A5) |
| F. Scene candidates | No (nothing enters a pack before the owner picks) | D and E for the inputs they add |

## 1. Ground rules

- **Branch:** `sections-2`, created from the commit that contains this file and the revised plan. If they exist only in a working tree, stop and ask the owner to commit them first. Work in a separate worktree (`git worktree add ..\Psyight-sections -b sections-2 <commit>`): the main checkout has uncommitted firmware work on `follow-mode`. Do not merge or push.
- **Do not edit:** `firmware/`, `app/src/link/`, the beat tracker, the bass tracker, the student, the flash limiter, the power budget, or anything on `wip/live-drop-reentry`. In `analysis/live_analysis.*` only the additions that Batch E names are allowed: no change to the loop, the run rules or any existing field's value.
- **Add, do not change** (owner decision 2026-11-10, still in force): no existing effect, preset, pack, option value or file key is removed, renamed or given a new meaning. New option values are appended; new parameters default to today's behaviour unless a gate below says otherwise; recordings without a new key replay as recorded.
- **Determinism:** analysis and automation stay pure functions of their inputs. The same input and parameters give bit-identical results.
- **Private data:** audio, labels, stems, recordings and anything that names what a private track contains stay out of the repo. Per-track outputs go to `captures/review/sections/` (gitignored). PROGRESS may carry totals and the track names it already carries.
- **Never play audio aloud and never send input to a window that is not Psyight's.** Every check below is a command-line tool or a unit test.
- **Docs in the same commit:** SPEC (6.5, 6.6, 6.6.1, 7.6 and a decision-log row for every decision), `docs/OPERATOR_GUIDE.md` for anything the operator can set, `docs/PROGRESS.txt`. Tool header comments stay accurate.
- **Writing:** British English, no em dashes, sentence case for labels, units in names (`_bars`, `_s`, `_beats`), `[Verify]` on unmeasured claims.
- **SPEC 18 assumptions these batches rest on:** A9 (the kick returning after 8 bars or more is a drop). Batch A's confusion table is the first measurement of it against labels: report it.

### 1.1 The labelled set and the split

- The labelled tracks are those the labelling mode marks done (`label_status`, `scan_track_folders`); `psyight_analyse --sections-eval <folders>` finds them. PROGRESS records 23, in `data\` and the owner's music folders. Ask the owner for the folder list once and record it in PROGRESS.
- The first ten were tuned on, so no clean held-out set exists. Make one split now and never change it: a track is **held out** when the FNV-1a 32-bit hash of its lower-cased file name (UTF-8, without the folder) is odd, else **tune**. Implement it in the tool (A1), not in a file, so it cannot drift.
- Every table in PROGRESS for batches B and C gives tune and held-out side by side.

### 1.2 The adoption gate (batches B and C)

A new rule ships with its parameter at the value that reproduces today's output. Its default changes only when all of these hold, on the split above:

1. the batch's primary metric rises by at least 0.02 on the tune set;
2. the same metric does not fall by more than 0.01 on the held-out set;
3. bars labelled alike do not fall by more than one point on either set;
4. no track loses more than 0.10 of boundary F within a bar, unless PROGRESS explains that track;
5. on the corpus (`--sections-report` over the folders named in the plan's results), the median count of short sections stays 0;
6. the synthetic golden (`golden metrics`) passes without loosening.

If the gate fails, keep the code and tests with the default off, write the table into PROGRESS and stop. A failed gate is a result, not a reason to try further rules.

When a default changes: a SPEC 16 row with the table's totals, the golden rewritten with the diff explained in the commit message, and every boundary that moved on OculusDrift, Hagios, Hall of Purity and Phase Change listed in PROGRESS.

## 2. Batch A: measuring tools (no behaviour change)

Acceptance criteria targeted: the plan's "Done when (revised)" needs figures that no tool prints yet. Nothing here changes the analysis, the engine or any existing output line.

Files: `app/src/analyse_main.cpp`, `app/src/io/section_marks.*` (scores), `app/src/live_check_main.cpp`, `app/tests/labelling_test.cpp` or `sections_test.cpp` (where `score_sections` is tested today).

**A1. Parameter overrides and the split.**
- `psyight_analyse --param <id>=<value>`, repeatable: sets any registry parameter declared by `declare_section_params` (for example `--param section.texture_step=1.2`). Resolve through the registry so ranges are checked; an unknown id or a value out of range is an error. `--texture-step` stays as it is. Print the overrides on the first line of the output. (Not `--set`: `psyight_live_check` uses that name for DJ sets.)
- `--sections-eval` prints, after its present totals, the same totals for the tune and held-out sets (1.1), and marks each track line `T` or `H` at its end. Present lines stay byte-identical up to that suffix; put the suffix behind `--split` if a script of the owner's parses the lines (check `captures/` and `tools/` for one first).

**A2. More scores in `--sections-eval`.** Add to `SectionScore` (or a sibling struct) and print after the present lines:
- **Right bar:** precision, recall and F counting a hit only when the detected boundary is in the same bar as the labelled one (bar index on the labelled grid, `bar_index_at`). This is the primary metric for Batch B.
- **Seconds:** hit rates at ±0.5 s and ±3 s, matched one to one as the within-a-bar score is (reuse its matching; do not write a second matcher).
- **Signed offsets:** for each labelled boundary, the nearest detected boundary within ±4 bars: a histogram of (detected minus labelled) in whole bars from -4 to +4, and a count of "none".
- **By kind:** a labelled boundary is a **kick change** when the Viterbi kick state of the bar before and the bar after differ (`TrackAnalysis::section_bars`, as `print_boundary_detail`'s `kick_at`), else **kick-continuous**. Recall within a bar, and the offset histogram, for each kind. Recall for kick-continuous boundaries is the primary metric for Batch C.
- **By transition:** recall within a bar per labelled transition (groove to groove, groove to breakdown, build to drop and so on), transitions with fewer than 5 cases pooled as "other".
- **Confusion:** bars by labelled kind against detected kind (6 by 6), over all tracks. Report beside it the share of labelled drop bars detected as groove and the reverse (assumption A9).
- **Count ratio:** detected boundaries over labelled boundaries per track, and the median.
- `--json <file>`: the totals, the per-set totals and one row per track, so two runs can be diffed by a script. Field names are the printed names in snake case.

**A3. `psyight_analyse --labels-stats <folders>`:** from the labels alone, no analysis. It answers the open questions at the end of the research notes.
- Sections per minute per track, and the median.
- Section length in bars per label: a histogram (1, 2, 3, 4, 5 to 7, 8, 9 to 15, 16, 17 to 31, 32 and over).
- For each boundary, the bars since the previous boundary: the share that are a multiple of 8, a multiple of 4 but not 8, one bar off a multiple of 4, and other.
- Breakdown lengths, split by what follows (a drop, or a groove).
- How many tracks carry a labelled phrase grid, phases, roles, events.
- All of it also per tempo band (under 160, 160 to 175, over 175 BPM), as a stand-in for subgenre.

**A4. Unit tests** for every new score on hand-made label lists: exact-bar hits against one-bar-off hits, the offset histogram's sign, the kind split, the confusion counts, a track with no detected boundary, a track with no labelled boundary.

**A5. Live scene dwell.** In `psyight_live_check`, wherever `--show` renders the automation (`automate_live`): count scene changes (a change of `SceneMix::to` that is not a rotation or variation inside one section), and print:
- scene changes per minute;
- dwell of each scene in phrases: under a half, a half to one, one to two, over two;
- for every dwell under one phrase: the time, and the section before, during and after;
- live breakdowns by length in bars (under 4, 4 to 7, 8 to 15, 16 and over), and how many ended in a drop.
If `--show` does not run together with `--set` today, make it. Run it on the 20 DJ sets (the folder is in PROGRESS, 2026-11-09) with the owner's `psyight.psyproj` and the Hexagon 4 pack.

**A6. Baselines.** Run and store under `captures/review/sections/baseline/`: `--sections-eval --detail --json`, `--labels-stats`, `--sections-report` on the corpus folders, and A5 on the sets. Write into PROGRESS: the totals per set, the confusion table, the offset histograms by kind, the label statistics, the live dwell table, and the exact commands.

**Acceptance.** Without the new flags every tool's output is unchanged. The baseline numbers are in PROGRESS. The gates of B, C and E are evaluated there in words ("B's gate: 31 of 140 hits are a bar off, so B proceeds").

**Stop for the owner with:** the proposed targets in the plan's "Done when (revised)", restated against the measured baseline, for confirmation.

## 3. Batch B: boundary placement (file mode)

Acceptance criteria targeted: boundaries on the right bar. Primary metric: right-bar F (A2).

**Gate to start:** in A's baseline, at least 10% of the hits within a bar are one bar off. If fewer, write that into PROGRESS and skip to C.

Files: `app/src/analysis/sections.*`, `app/tests/sections_test.cpp`, the synthetic generator if a test track needs a new arrangement.

**B1. Local phrase anchor.** New parameter `section.phrase_anchor` (0 Track, 1 Kick return; default 0; Engineer tier).
- Today the energy splits, the texture splits and the build snap count their lines from the single `beats.phrase_bar_offset` (`sections.cpp`, the uses of `phrase_offset`).
- With 1: the count restarts at every kick return that ends a kickless run of at least `min_breakdown_bars` bars (the Viterbi state going from no kick to kick), and at the first kick bar of the track. Before the first anchor the track offset applies. A line is then "a multiple of 4 (or of `bars_per_phrase`) bars after the latest anchor at or before this bar".
- Reason: an added or dropped bar shifts every later line under one offset (research notes, point 2); the engine's `phrase_starts` already restarts at sections.

**B2. Asymmetric snap.** New parameters `section.snap_early_bars` and `section.snap_late_bars` (0 to 2, default 0; Engineer tier).
- Applies to boundaries where the kick stops (groove or drop into breakdown or outro). It never moves a kick return: drops land exactly.
- A kick stop `k` bars before a 4-bar line, `0 < k <= snap_early_bars`, moves onto the line: the kickless bars before the line stay in the section they end (a fill or a stop before the line).
- A kick stop `k` bars after a 4-bar line, `0 < k <= snap_late_bars`, moves back onto it only when those `k` bars have at most half their beats with a kick (`SectionBar::kick_beats`).
- Lines are counted by B1's rule in force.
- A move that would leave a section shorter than 2 bars is not made.
- `section.build_snap_bars` keeps its meaning and default.

**B3. Tests** (synthetic, exact):
- a groove whose kick stops one bar before an 8-bar line, then a breakdown: the boundary is a bar early with snap 0 and on the line with `snap_early_bars` 1;
- the same with the kick stopping three bars early: not moved;
- a 9-bar section after a kick return, then a texture change 8 bars later: the texture split lands on the change with `phrase_anchor` 1 and misses or lands a bar off with 0;
- a kick return is at the same bar under every setting;
- defaults reproduce the present sections on the whole synthetic set (compare with the output before the change).

**B4. Sweep and decide.** `phrase_anchor` {0, 1} by `snap_early_bars` {0, 1, 2} by `snap_late_bars` {0, 1}: twelve runs of `--sections-eval --json` with `--param`. Table in PROGRESS: right-bar F, within-a-bar F, bars alike, on tune and held-out. Apply the gate (1.2) to the best tune-set row; prefer the row with fewer non-default values when two are within 0.005.

**Stop for the owner with:** the table and the decision.

## 4. Batch C: block novelty for changes while the kick runs on (file mode)

Acceptance criteria targeted: recall of kick-continuous boundaries, without the fragmentation part 1 cured. Primary metric: within-a-bar F over all boundaries, with kick-continuous recall reported beside it.

**Gate to start:** in A's baseline, kick-continuous recall is under 0.5. If not, record and skip.

Files: `app/src/analysis/sections.*`, `app/tests/sections_test.cpp`.

**C1. The rule.** New parameters `section.novelty_step` (default 0: off) and `section.novelty_bars` (default 4), Engineer tier.
- Today's texture rule compares a bar with the bar before it on 4-bar lines. One odd bar (a fill) passes it, and a change that builds over two bars can fail it.
- For each 4-bar line `b` inside a groove or drop (lines by B1's rule in force), with `L = novelty_bars`: take the mean of the bars' feature vectors over `[b - L, b)` and over `[b, b + L)`, both clipped to the section. The novelty is the distance between the two means, measured by the same function and scaling the texture rule uses, so `novelty_step` and `texture_step` are in the same unit. This is the block-mean form of Foote's checkerboard novelty; no similarity matrix is needed at this size.
- The feature vector is `SectionBar::texture` plus the bar's residual flatness and centroid (log Hz), each scaled over the track's bars as the texture components are. Flatness and centroid join only the novelty vector: the existing texture rule's input does not change. Without a residual they are left out.
- A split is made at `b` when the novelty is at least `novelty_step`, is the largest among the lines within 4 bars either side, and both pieces are at least 4 bars.
- Splits from the texture rule and the novelty rule at the same line are one split. Energy assignment and the finer detail run after, as now.

**C2. Tests** (synthetic):
- a groove where a layer enters on an 8-bar line and stays: one split there with `novelty_step` on, whatever `texture_step` is;
- a groove with a one-bar fill at a phrase end and no lasting change: no split from the novelty rule (and show that the texture rule alone does split there at a low `texture_step`, to pin down the difference);
- a change that ramps over two bars across a line: one split, on the line;
- off (0) reproduces the present sections on the synthetic set.

**C3. Sweep and decide.** `novelty_step` {0, 1.0, 1.25, 1.5, 2.0} by `texture_step` {1.5, off} (off: a value above any distance; check how the registry range allows it), with B's adopted settings. Table as B4, plus kick-continuous recall, the count ratio and precision. Gate 1.2, and one more condition: the median count ratio (A2) must lie between 0.7 and 1.3 on both sets.

**Stop for the owner with:** the table, the decision, and the measured values against the plan's "Done when" targets. If the targets are still missed, say which transitions carry the misses (A2's table); do not add a third rule.

## 5. Batch D: breaks, fills, pre-drops and phases reach the engine (file mode)

Acceptance criteria targeted: SPEC 6.6.1 "Lights": the engine reads the finer detail. Detected events stay off until scored; labelled ones are used. Defaults reproduce today's show (the `golden show` test passes untouched).

Files: `app/src/engine/inputs.*`, `automation.*`, `modulation.*`, `effect_spec.cpp` and the stutter gate, `app/src/io/section_marks.*`, `app/src/gui/session.*`, `app/src/output/show*`, the project and session-record readers, `app/tests/automation_test.cpp`, `inputs_golden_test.cpp`, `show_golden_test.cpp`.

**D1. Events on the timeline.**
- `SectionTimeline` gains `std::vector<DetectedEvent> events`. `section_timeline(...)` built from edited labels carries the labels' events (`Labels::events`), nothing detected. The labelled phase, role and pre-drop already arrive on the sections (`io/section_marks.h`): check with a test, do not rebuild.
- `inputs_at` and `automate` take the events from the timeline when one is given, else from `TrackAnalysis::events` only when `AutomationParams::detected_events` is true (new, default false, project key `automation.detected_events`).
- Recordings: the edited sections are kept in the runtime state today; add the events beside them so a replay is identical.

**D2. Inputs, additions only** (`EngineInputs`; file mode fills them, live mode leaves the defaults):

| Field | Meaning | Default |
|---|---|---|
| `in_break` | Inside a break event | false |
| `since_break_s` | Since the latest break started | `kNever` |
| `break_length_s`, `break_progress` | Of the break in progress | 0 |
| `in_fill`, `fill_progress` | Inside a fill event | false, 0 |
| `in_predrop`, `predrop_progress` | From the section's `predrop_s` to its end | false, 0 |
| `until_drop_s` | To the next drop's start, when the current section is a build or breakdown that a drop follows | `kNever` |
| `section_phase` | `DetectedSection::phase` | -1 |

Modulation sources appended: Break (1 inside a break), Pre-drop (its progress). Update `kModSources` and the options string. The inputs compatibility golden must pass on every older field.

**D3. Automation, each a new option whose default is today's behaviour.**
- `fill_source` (`phrase`, `events`, `both`; default `phrase`): with `events` or `both`, a fill event in a groove or drop plays a phrase-end fill over its own start and length (`place_fill`'s kind selection unchanged, seeded as now). With `both`, a phrase-end fill that overlaps an event fill is dropped.
- `break_fill` (a `PhraseFill`, default `off`): what plays over a break event for its whole length. `gap` is the stop-cut look; `mix` is not allowed here (reject on load, fall back to `off`).
- Stutter gate mode appended: "Pre-drop gap, detected": dark from `predrop_s` to the section's end, falling back to "Pre-drop gap, joined builds" when the section has no pre-drop. Append to the options string; never reorder.
- `scene_by` (`occurrence`, `phase`; default `occurrence`): with `phase`, a groove or drop whose phase is 0 or more takes the candidate at index `phase` (in place of its occurrence count) in `scene_for_label`, so groove A looks the same each time it returns. Sections without a phase count as now. Rotation and variations inside the section are unchanged.
- **Short breakdowns** (a tag, so it is opt-in by construction): a breakdown shorter than `short_breakdown_bars` (new, default 16) that lies between two sections with kicks takes the scenes tagged `break` when any scene carries that tag; otherwise nothing changes. Add "break" to the Show tab's section tag choices (`preview_view.cpp`, the label list near the scene tags). File mode only.

**D4. Tests.**
- Inputs: a hand-made timeline with one break, one fill and one pre-drop; every new field at times before, inside and after each.
- Automation: `fill_source` in its three values against known fill events; `break_fill` gap over a two-bar break; `scene_by` phase on sections A B A (the third equals the first); the `break` tag with and without a tagged scene; each option at its default gives the same `SceneMix` as before on the existing test timelines.
- Show golden: unchanged. Add a second small golden with the options on, over the synthetic track whose generator already has A B A basslines, breaks and fills (SPEC 6.6.1 "Verification first").
- Replay: a recording made with the options on replays identically; a recording from before the batch replays identically.

**D5. Measure, do not tune.** Run `--sections-eval` for the finer detail on whatever detailed labels exist. If none exist, say so in PROGRESS: `detected_events` stays false and that is the batch's result.

**Stop for the owner with:** FOR THE OWNER in PROGRESS: label two or three tracks in detail (Detailed on: phases, B and F for breaks and fills, key 7 for pre-drops) so events can be scored; try `break_fill: gap` and the detected pre-drop gap on Alien Army; decide whether short breakdowns should have their own scenes.

## 6. Batch E: live mode (causal)

Acceptance criteria targeted: fewer scene dwells under a phrase in live mode, no drop later than today, and a build that keeps climbing. Everything is a new option, off by default: live behaviour at an event is the owner's to switch.

**Gate to start:** A5's table. If under 5% of live scene dwells (drops and builds aside) are shorter than a phrase, skip E1 and E2 and record why.

Files: `app/src/engine/automation.*`, `inputs.*`, `modulation.*`; `app/src/analysis/live_analysis.*` for additive state only; `app/src/live_check_main.cpp`; `app/tests/automation_test.cpp`, `live_test.cpp`.

**E1. Breakdown commit delay.** `AutomationParams::live_breakdown_commit_bars` (0 to 8, default 0; project key the same under `automation`).
- Live mode declares a breakdown after two bars without a kick and cannot know its length. With the delay at `n`, `automate_live` keeps showing the look the section before ended on until the breakdown has run `n` bars, then crossfades as it does now. During the wait the kick layers are idle by themselves, which is the stop-cut look.
- A build or a drop declared during the wait takes over at once.
- Choose the value to propose from A5's breakdown lengths (the share under 4 and under 8 bars); do not change the default.

**E2. Resumed groove.** `AutomationParams::live_resume` (bool, default false).
- When a groove starts after a breakdown that lasted under one phrase, with no build or drop between, and the section before that breakdown was a groove or drop: the groove takes that earlier section's look (scene, rotation step and variation), and its rotation continues from the phrases that section had run.
- `LiveState` keeps only the section before the current one. Add, in `LiveBeat::set_section`, the same record for the one before that (label, occurrence, energy, length, as the existing `previous_*` fields) under new names. No existing field's value may change: the tests in `live_test.cpp` pass untouched, and a new test asserts the present fields against their values before the batch on the synthetic kick sequences already there.

**E3. Tension.** A new engine input and modulation source, additions only.
- `EngineInputs::tension`, 0 to 1, 0 outside builds.
- File mode: `max(riser, build_progress)` (the visual review's L10: a quiet last bar holds the tension it built).
- Live mode: `ramp = 1 - 2^(-since_build_s / phrase_s)`; `tension = max(ramp, riser * (0.5 + 0.5 * ramp))`. The riser lifts it fast to a half; the ramp keeps it climbing by half the remaining distance each phrase. Both terms never fall within a build, so neither does the tension.
- Modulation source Tension appended. No existing scene reads it; Batch F's build candidates do.
- Measure on the DJ sets, in `psyight_live_check`: the share of build time within 0.05 of the build's highest tension, beside the same figure for the riser (PROGRESS: the riser sits at its top for 29% of build time).

**E4. Tests.**
- `automate_live` on hand-made `LiveState` sequences: groove, a 3-bar breakdown, groove with the delay at 4 (the scene never changes); the same with a build declared in bar 3 (the build scene at once); groove, 6-bar breakdown, groove with `live_resume` (the first groove's scene and rotation step); a breakdown of two phrases (not resumed).
- Tension: monotone within a build in both modes; 0 outside; file-mode value against a hand-made riser.
- Defaults: `automate_live` output equal to before on the existing tests.

**E5. Measure.** A5 again on the 20 sets with the candidate settings (`live_breakdown_commit_bars` at the proposed value, `live_resume` on): scene changes per minute, short dwells, and the time from each caught drop to the drop scene (must not grow). Table in PROGRESS, old against new.

**Stop for the owner with:** the table and the settings to try at the next studio day. The defaults stay off until the owner has watched them.

## 7. Batch F: breakdown, build and break scene candidates (the owner picks)

This is taste, so it is augmentation, not automation: the output is a set of candidates and contact sheets. Nothing enters a built-in pack before the owner picks.

Acceptance criteria targeted: Hexagon 4 has one breakdown scene (Nebula) and one build scene (Hex pressure); long sets repeat them (PROGRESS, 2026-11-10).

- **Candidates:** four builds, four breakdowns, two breaks (for D3's `break` tag), written as data beside `tools/python/packs/hex3_spec.py` and generated with `gen_cpp.py` into a clearly marked prototype block. No new effect types: use what exists, including Tension (E3), the detected pre-drop gap (D3) and the post-effects.
- **Rules, from what the Hexagon 3 round learned** (PROGRESS): the rings and outlines carry every scene; detail sits on one ring or group, sized to read; every build has a ring base from its first bar and climbs to its end; quiet scenes stay visible (no scene whose brightest byte stays under 16 at the 44% cap); never over a quarter of the rig above byte 20 for a breakdown; no white except the drop's hit.
- **Evidence:** contact sheets on Alien Army and OculusDrift with the owner's project and labelled sections, drawn as `captures/review/packs/sheets.py` does from `psyight_show_check --scene <name> --at <times> --frames-out <file>`: a build at its start, middle, last bar and the first beat after; a breakdown over its first phrase. Per candidate from `psyight_show_check`: median lit pixels, brightest byte, flashes in the worst second with the limiter off, current. Look at the sheets before reporting: Hexagon 2 was measured and never looked at, and was wrong.
- **Cost:** each candidate scene under the budget of `docs/PLAN_HEX_VISUALS.md` section 1, measured as its cost test does.
- **After the owner picks:** a new built-in pack, Hexagon 5: Hexagon 4's scenes unchanged (a test asserts their layers, settings and routes equal Hexagon 4's) plus the picks, appended last in `builtin_show_packs()`. The presets compatibility golden passes on every older key. Unpicked candidates are removed from the generated block.

**Stop for the owner with:** the sheets' paths, the table, and one line per candidate on what it is meant to show.

## 8. Verification, per batch

| Check | How |
|---|---|
| Build and tests | `cmake --build --preset windows-msvc-release`, `ctest --preset windows-msvc-release`, from an x64 VS 2022 developer shell in `app/` |
| Determinism | Every new tool output identical on two runs; every new rule and option has a test that renders or analyses twice and compares |
| No behaviour change at defaults | A: tool outputs without new flags identical. B, C: sections on the synthetic set identical with the parameters at their defaults. D, E: `golden show`, `golden inputs`, `golden presets` pass untouched |
| Replay | `psyight_replay data\recordings\pre-hex-visuals-oculus` and the other recordings named in PROGRESS still replay identically after D and E |
| Golden metrics | `golden metrics` passes untouched unless a gate changed a default; then rewritten with `PSYIGHT_UPDATE_GOLDEN=1` and the diff explained |
| The owner's tracks | Not in CI. The commands and totals go to PROGRESS, the per-track files to `captures/review/sections/` |
| On the piece, by ear | The owner's. List what to look at; do not mark a visual or audible item done from a table |

## 9. Commit order

One concern per commit, each building and passing the tests.

- **A:** 1 `--param`. 2 The split. 3 Scores and their tests. 4 `--json`. 5 `--labels-stats`. 6 Live scene dwell. 7 PROGRESS with the baselines.
- **B:** 1 `phrase_anchor`. 2 The snap parameters. 3 PROGRESS with the sweep; a default change, if the gate passes, as its own commit with SPEC and the golden.
- **C:** 1 The novelty rule. 2 PROGRESS with the sweep; a default change as its own commit.
- **D:** 1 Events on the timeline and in recordings. 2 Inputs and modulation sources. 3 `fill_source` and `break_fill`. 4 The stutter mode. 5 `scene_by`. 6 The `break` tag. 7 Docs.
- **E:** 1 `LiveState` additions. 2 Commit delay. 3 Resume. 4 Tension. 5 The live check's figures. 6 Docs.
- **F:** 1 Candidates and sheets (prototype block). 2 After the picks: the pack, tests, docs.

## 10. Stop and ask the owner when

- a gate's numbers are borderline (within 0.005 of a threshold), or the tune and held-out sets disagree in sign;
- a compatibility test cannot be met without changing an existing assertion;
- a batch needs a change in a file section 1 forbids, or a new dependency;
- a track's labels look wrong (a boundary the detection, the kick state and the level all contradict): list it for the owner, do not edit labels;
- anything would be sent to a node or played aloud.

## 11. Open points, marked for the owner

- The targets in the plan's "Done when (revised)" are proposals until Batch A's baseline is in.
- The split by file-name hash may fall unevenly across subgenres with 23 tracks; A6 reports each set's tempo bands so this is visible.
- Whether a short breakdown between drops should look different from a main breakdown (D3), and the 16-bar threshold.
- `live_breakdown_commit_bars` trades a late breakdown look for fewer blips; its value is the owner's after the studio day.
- The tension formula's half-per-phrase rate and its 0.5 riser share are first guesses [Verify on the DJ sets and by eye].
- Not in these batches: kicks off the constant-tempo grid on edited tracks, the impact flash on a returning kick, and the interface findings of the visual review (reasons in the plan's "Open work").
