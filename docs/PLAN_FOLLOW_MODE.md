# Plan: follow mode (the show from the decks' own tracks)

Written 2026-11-11 at the owner's request: "do all the infrastructure that might be needed so we're ready when we have access to CDJs". Branch `follow-mode` (from `hex-visuals`). Background: `docs/STANDALONE_PLAN.md` (Pro DJ Link section), `tools/python/djlink_log.py`.

## Goal

At a set on linked CDJs, the show runs as if each playing track were loaded in Psyight: the whole analysis of the track is known ahead, so builds, drops and section changes land on time, and the operator sees what is coming (the analysis view's waveform, sections and bass roll scrolling with the deck). Live audio analysis stays as the fallback and the cross-check.

Not in scope: joining the link as a player (a virtual CDJ), sending anything to the CDJs or the mixer, reading the players' USB or NFS shares. Those are levels 2 and 3 of the standalone plan, later and only if a real gap remains.

## What passive listening gives (and does not)

| Source | Gives | Not |
|---|---|---|
| Pro DJ Link, UDP 50001, broadcast | beats (BPM, pitch, beat in bar, time to the next beat and bar), the mixer's on-air flags per channel, CDJ-3000 absolute position (playhead ms, track length, pitch, BPM, about every 30 ms) | which track is loaded (the track ID is only in status packets, sent to devices that join) |
| Pro DJ Link, UDP 50000 | device names, numbers and types (keep-alives) | |
| TCNet from PRO DJ LINK Bridge, UDP 60000 to 60002 | each layer's playhead and total time, its state (playing, paused...) | metadata (title, artist) needs an opt-in and requests to the Bridge (localhost only; a later step) |
| The mixer's MIDI (USB), once MIDI output is on at the mixer | channel faders, crossfader, trims as control changes: how much of each deck the room hears | anything about the tracks |

Players without absolute positions (the CDJ-2000NXS2 and older) send beats but not where they are in the track, so a passive listener cannot follow them: with those players, follow mode needs a later level (joining the link) and the show stays on live audio.

Byte offsets come from Deep Symmetry's protocol analysis and the TCNet V3.5.1B specification. Every offset not yet seen in a capture is marked [Verify] in the code, and one capture with the hardware settles them.

## Key decisions

1. **Passive only.** Sockets bind and listen; nothing is sent. A booth is never disturbed.
2. **Protocol-agnostic core.** Decoders turn packets into deck updates; everything after (deck state, track matching, following, the show) works on deck updates, so the two protocols and a simulator feed the same path.
3. **Identify the track by length and BPM, confirm by audio, override by hand.** Candidates from the media library (or a rekordbox XML collection): track length within 1.5 s, BPM within 0.5 when both are known. With live input on, the kicks heard are compared with each candidate file's kicks at the reported position; the candidate whose kicks line up wins and the median difference is the audio delay. The operator can pin a file to a deck.
4. **One clock.** Packets are stamped on arrival with the steady clock (the clock live recordings already log). A deck's position at time t is its last reported playhead plus (t - stamp) times its speed (1 + pitch) while playing. A constant offset (the CDJ to mixer to room delay against the packets) is measured from the audio when possible, and trimmed by hand.
5. **The lead deck.** The show follows one deck, among those playing. What the room hears comes, best first, from the mixer's MIDI faders (each channel's fader level times its crossfader side, so a blend is a proportion), else the link's on-air flags, else the deck that started playing last. With two decks audible (a blend), the current lead is kept until it goes quiet (fader down, off air) or stops, unless the other reaches a groove or drop while the lead is in its outro or intro (the incoming drop is what must land). The operator can force a deck. The mixer channel to player mapping is taken as channel n to player n, and can be set by hand.
6. **Follow mode reuses file mode.** The lead deck's matched file is analysed ahead (as the media player preloads), and the show renders from that analysis at the deck's position: section ends, builds and drops known in advance (EngineInputs::section_end_known). Decks that load a track are analysed while they cue.
7. **Recorded and replayable.** Link packets are logged as JSON lines in the format `djlink_log.py` writes (steady clock ns, port, source, hex), alongside live recordings, and `psyight_follow_check` replays a log (or a simulated session) offline.

## Phases (each a commit, each tested)

1. Packets and decoders: the packet log format (read and write), Pro DJ Link (keep-alive, beat, on-air, absolute position) and TCNet (header, time packet) decoders, and matching encoders for the simulator and tests.
2. Deck state: deck updates applied per device, position extrapolated, stale decks timed out.
3. Track matching: candidates from a library by length and BPM; confirmation by kick alignment, which also measures the audio delay.
4. Follow engine: the lead deck (from MIDI fader levels, on-air flags or the last started), handovers in a blend, the track time to render, what comes next (the next section and drop, and the time to them).
5. Simulator: a scripted DJ session as packets in both protocols (decks load, play, pitch, blend, stop).
6. Listener: a passive UDP listener thread per protocol, snapshots for other threads, the packet log written as it runs.
7. `psyight_follow_check`: a link log or a simulated session, plus a music folder or rekordbox XML, gives the timeline (lead deck, matched track, position, sections ahead), and optionally renders the show from the matched files.
8. App: Show > Follow (source: off, Pro DJ Link, TCNet, simulator, a log; decks; matches with a manual override; the lead; the offset), the mixer's MIDI faders read through the existing MIDI input (the control map learnt by moving each fader, saved with the project), the show driven from the lead deck's file, the analysis view following the deck.

## On the hardware day (owner)

Detailed in `docs/STUDIO_CHECKLIST.md`.

1. Laptop on the CDJs' switch; quit rekordbox and AnyDesk.
2. `python tools/python/djlink_log.py` for a few minutes: two tracks, one blend, a pitch change, with live input recording from the DJM-A9 at the same time.
3. The mixer's MIDI: `tools/studio/midilisten.ps1` while moving each channel fader and the crossfader, for the control map.
4. `psyight_follow_check` on that log: the decoders' [Verify] offsets checked, the delay measured (if the tool is not built yet, the log and recording are kept for it). Then the same with the Bridge and `--tcnet`.
5. Follow mode in the app.

## Open points

- Whether the DJM-A9 and the CDJ models at hand send on-air flags and CDJ-3000 absolute positions to a passive listener [Verify].
- Whether the CDJ-3000 position packet's BPM is the track's or the tempo heard (with the pitch applied) [Verify].
- The DJM-A9's MIDI control map for faders and crossfader, and whether it sends them continuously [Verify].
- Whether the Bridge sends time packets without an opt-in [Verify]; its manual says some information needs a licensed application.
- Reverse engineering for commercial use: see the standalone plan's licence note [Verify before any commercial use].
