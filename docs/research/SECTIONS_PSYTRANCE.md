# Sections and phrases in psytrance and hi-tech: research notes

Written 2026-10-24 for tuning the section detector (SPEC 6.5, 6.6) and planning the labelling work. Sources are listed at the end. Psytrance has no annotated structure dataset that we could find, so the genre-specific claims below come from production guides and are marked [Verify] where they rest on a single weak source. The measured results come from general EDM studies.

## Bottom line

1. **Phrases are 8 and 16 bars, and boundaries land on phrase lines, but not always.**
   - EDM in general: a DJ cue-point study of 4,710 tracks found 16-bar phrasing most common, then 8. Irregular sections exist, and DJs put cues at their starts.
   - Psytrance guides say the same: builds of 8, 16 or 32 bars, and drops made of 16-bar blocks.
2. **Expect one bar of slack.**
   - Producers add or cut a bar to surprise (Rocha et al.).
   - Psytrance guides put a one-bar (four-beat) pause before a drop.
   - A measurement of 555 club records found section lengths peaking at 7, 8, 15 and 16 bars [Verify: not peer-reviewed; the page attributes the spread to its one-bar detector resolution].
   - So a boundary one bar early or late is normal, and one bar of slack should not break the phrase grid.
3. **Novelty comes before the boundary.**
   - Fills, risers and stop-cuts sit just before the downbeat where the new section starts.
   - Rocha et al. moved novelty peaks forward onto the 8th or 16th bar line, pulling harder when the peak was early than when it was late. This added about 10 points of boundary F-measure at ±0.5 s on EDM (41.6 to 51.7).
4. **Psytrance sections are told apart by energy and texture, not harmony.**
   - Elements enter and leave: kick, bass, hats, leads.
   - That is what our rules already use (kick state, the high band, riser flatness, level steps).
   - The 2026 EDM-specific model (EDMFormer) confirms energy, rhythm and timbre are the cues, and that pop-trained models miss drops and builds.
5. **The subgenres differ in how they break, which changes what the detector must do.**
   - Full-on uses classic breakdowns.
   - Darkpsy and forest barely break: the structure lives in texture changes at phrase lines.
   - Hi-tech is fast and full of short stop-cuts.

## The shape of a psytrance track

From the production guides (Bonedo, Myloops). The bar counts are typical, not rules.

| Part | Typical length | What happens | Psyight label |
|---|---|---|---|
| Intro | 16 to 32 bars | Sparse, for mixing: atmosphere, then hats, then a filtered kick; often no bass | intro |
| Build-up | 8, 16 or 32 bars | Risers, snare rolls getting faster, filter and pitch automation; often ends in a one-bar pause | build |
| Pre-drop pause | 4 beats (1 bar) | Silence, a fill or a synth stab | build (already: up to 4 quiet bars stay in it) |
| Drop | 64 bars as four 16-bar blocks | Full groove; blocks separated by short pauses (about 4 beats); percussion returns in stages | drop, then groove |
| Short break | up to 16 bars | Kick and bass rest; links two drops | breakdown (see below) |
| Main breakdown | up to 64 bars, about two thirds in | Melodic or vocal; builds back into the next drop | breakdown, build |
| Peak | 64 bars | The full groove plus one new layer | groove (high energy) |
| Outro | 32 to 64 bars | Stripped every 8 bars down to kick and one loop | outro |

- **Length and drops:** a track is 7 to 9 minutes, with two or three drops (Bonedo).
- **Example timings:** the Myloops blueprint is 320 bars, 9:10 at 140 BPM. At our 150 to 160 BPM a bar lasts 1.5 to 1.6 s, so a 16-bar block is 24 to 26 s. At 180 BPM a bar is 1.33 s and 16 bars are 21 s.

## Subgenres

| | Tempo | Structure | Reliability |
|---|---|---|---|
| Full-on | 140 to 150 | Melodic, festival form: clear breakdowns and builds | Genre guides |
| Dark psy | 140 to 155 | Long-form, minimal breakdowns, incremental modulation | Genre guides [Verify] |
| Forest | 145 to 160 | "Almost no breakdowns and beat interruptions"; texture-led | One guide [Verify] |
| Hi-tech | 170 to 200+ (sources range from 150 to 230) | Episodes of 16 to 32 bars, frequent micro-breaks and stop-cuts, the least predictable | Genre guides [Verify] |
| Psycore | about 170+ | Short 8 to 16 bar blocks, frequent switch-ups | One guide [Verify] |

The tempo ranges disagree between sources by 5 to 10 BPM.

## What the literature measured

- **Rocha, Bogaards and Honingh (SMC 2013):** 35 EDM tracks.
  - Method: timbral novelty on beat-aligned frames, anchored at the first bass-drum downbeat (EDM intros are often beatless), then "musically informed rules" that pull boundaries to the 8th and 16th bar, asymmetrically.
  - Results: F 51.7 at ±0.5 s with the rules, 41.6 without. At ±3 s, 69 to 74.
  - Lesson: getting the grid right is worth more than better features at fine tolerance.
- **Argüello, Lanzendörfer and Wattenhofer (ISMIR 2024), EDM-CUE:** 4,710 tracks and 21k DJ cue points.
  - Cues fall on multiples of 8 and 16 bars; 16-bar phrasing is the majority.
  - Their detector's raw peaks recur every 4 bars, and enforcing at least 8 or 16 bars between predictions raised precision.
- **Sajeer et al. (2026), EDMFormer, EDM-98:** 98 annotated tracks in an EDM taxonomy (intro, build-up, drop, breakdown, outro).
  - Results: hit rate 0.616 at 0.5 s and 0.635 at 3 s, against 0.569 and 0.608 for a pop-trained model.
  - The dataset covers trance and progressive but has no psytrance.
- **Mixgraph (2026, a commercial tool page):** 555 club records.
  - 60.9% of sections are within a bar of a whole number of 8-bar phrases, against 39.5% for random boundaries; only 6.5% are 4 bars off.
  - [Verify]: not peer-reviewed.

## What this means for Psyight

In order of expected benefit. Each is a hypothesis to check against the owner's labels before tuning (CLAUDE.md: measure first).

1. **Snap all section boundaries to the phrase grid, not only build starts.**
   - Today only a build start within 1 bar of a 4-bar line moves.
   - Rocha's result suggests a pull towards 8 and 16 bar lines for every boundary, stronger for a boundary found early, since fills come before the line.
   - Kick returns are already exact, so this matters most for energy-step splits and build starts.
2. **Re-anchor the phrase grid locally.**
   - The beat tracker has one `phrase_bar_offset` for the whole track. An added bar, or a 7-bar build, shifts every later phrase line by one.
   - The cue-point data says irregular sections are common enough that DJs mark them.
   - Option: let a strong boundary (a kick return after a breakdown) restart the phrase count from there.
3. **Separate short breaks from breakdowns.**
   - The guides distinguish breaks of up to 16 bars between drops (kick and bass rest) from the main breakdown (up to 64 bars, melodic, about two thirds in).
   - For the lights they want different scenes: a break is tension, a breakdown is the ambient scene.
   - A length threshold (around 16 bars) or a "short break" label would carry that. Needs the owner's view.
4. **Treat hi-tech stop-cuts as events, not sections.**
   - One- and two-bar kick dropouts are frequent there. The Viterbi already keeps them as grooves (a breakdown needs four kickless bars).
   - They are strong lighting moments, though, so expose them as a "stop" event (blackout or strobe hit) rather than lose them.
5. **Darkpsy and forest need texture changes inside grooves.**
   - With almost no breakdowns, most of the track is groove. Its structure is level and timbre steps at 16 and 32 bar lines.
   - `energy_step_db` only sees level. A timbre-change test at phrase lines (band balance or residual centroid, Rocha-style novelty) would split those grooves.
6. **Tempo range for hi-tech.**
   - File mode's beat tracker allows 120 to 190 BPM (`beat.min_bpm`, `beat.max_bpm`); live mode allows 120 to 200.
   - Hi-tech above 190 would be clamped or tracked wrongly. Widen the range per genre, or for hi-tech tracks set it by hand.
   - Untested here: we have no hi-tech track in the reference set.
7. **Score like the literature.**
   - Report boundary precision, recall and F at ±0.5 s and ±3 s, plus "on the right bar", in `--sections-eval`, so our numbers compare with Rocha (51.7 at ±0.5 s) and EDMFormer.

## Open questions

- No psytrance or hi-tech annotated dataset was found; our own labels will be the first.
- The guides' bar counts describe how producers intend to arrange, not measurements.
- After about 10 labelled tracks, check:
  - how often our boundaries sit on 8 and 16 bar lines;
  - how common one-bar irregularities are;
  - how long breaks and breakdowns run in darkpsy against full-on.

## Sources

- Bonedo, "Crashkurs Psytrance produzieren #4: Vocals, Arrangement, Mixing" (German): https://www.bonedo.de/artikel/crashkurs-psytrance-produzieren-4-vocals-arrangement-mixing
- Myloops, "How to Arrange a Psytrance Track: A Bar-by-Bar Blueprint": https://www.myloops.net/arranging-a-psytrance-track-from-start-to-finish
- Rocha, Bogaards, Honingh, "Segmentation and Timbre Similarity in Electronic Dance Music", SMC 2013: https://zenodo.org/record/850377
- Argüello, Lanzendörfer, Wattenhofer, "Cue Point Estimation using Object Detection", ISMIR 2024: https://arxiv.org/abs/2407.06823
- Sajeer et al., "EDMFormer: Genre-Specific Self-Supervised Learning for Music Structure Segmentation", 2026: https://arxiv.org/abs/2603.08759
- Mixgraph phrase counter (555 club records, unreviewed): https://www.mixgraph.io/tools/phrase-counter
- Trance Nexus, "What is Psytrance?": https://trance-nexus.com/blog/what-is-psytrance
- Psytrance Guide (psytranceguide.com, copy on Scribd): https://www.scribd.com/document/429008459/Psytranceguide-com-Psytrance-Guide
- Melodigging genre pages (hi-tech, dark psytrance, psycore): https://www.melodigging.com/genre/dark-psytrance
- Microgenre Music, "What is Forest Psytrance?": https://microgenremusic.com/genres/trance/what-is-forest-psytrance/
- Butler, *Unlocking the Groove* (2006), on EDM form as buildups, cores and breakdowns: https://search.worldcat.org/title/61162077
