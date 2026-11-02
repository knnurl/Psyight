# Standalone box: plan for later (2026-11-02)

Psyight without a laptop at shows. Not started: Linux builds are a SPEC non-goal and the mixer
input is marked "later", so starting this needs a decision-log entry in SPEC section 16 first.

## Decided by the owner

- Audio comes from the mixer's master output, through a USB audio interface (balanced inputs for
  XLR or TRS outputs, an isolator for RCA). Not the CDJ outputs: a CDJ plays the track being cued
  whether or not it is faded in.
- Check first: the master output follows the master level knob, and app/src/audio has no input
  level normalisation. Play OculusDrift through the live check at 0, -6 and -12 dB and compare
  the scores; if they move, the detectors need a level-independent front end.

## Hardware: N100 class mini PC, Linux (recommended) or Raspberry Pi 5

| | Fanless N100/N150 mini PC | Raspberry Pi 5 |
|---|---|---|
| CPU | 4 x86-64 cores to 3.4 GHz (N150 3.6) | 4 Cortex-A76 at 2.4 GHz |
| Geekbench 6 single / multi | about 1,200 / 3,100 to 3,300 | about 790 to 900 / 1,500 to 2,150 |
| Graphics | Desktop OpenGL 4.6 (Mesa): GUI unchanged | OpenGL ES 3.1: the GUI needs a GLES path (only ImGui's shader) |
| ISA | Same as the dev machine | ARM64: its own golden checks |
| Ethernet | Two 2.5 GbE on many fanless models | One; a second needs USB |
| Storage | NVMe or SATA | SD by default (avoid); NVMe via a HAT |
| Cooling | Fanless models (MINIX Z100-0dB, MeLE Quieter) | Active cooler under load |
| Power | 12 or 19 V; about 6 to 8 W idle, 25 to 30 W full | 5 V 5 A USB-C; about 3 W idle, 10 W full |
| Extras | USB | GPIO (blackout button, status LEDs), DSI touchscreen |
| Price (2026-11) | about $180 to 250 complete | 8 GB board alone about $175 to 180, plus case, cooler, PSU, NVMe |
| Availability | Models change yearly | Production promised to at least 2036 [Verify] |

N100 because: two ports keep the DJ Link network apart from the LED network, the GUI runs
unchanged, same ISA, no fan to clog with dust. The Pi 5 wins for battery power, size or GPIO.

Live analysis measured 1.3% of a laptop core (psyight_live_check, OculusDrift); on either box
expected under 10% [Verify by measuring on the box].

## Software

- Linux: a minimal Debian, read-only root, one systemd service that restarts on a crash. No
  Yocto or Buildroot needed.
- Port: all nine dependencies are cross-platform and permissively licensed. 11 of 233 source
  files touch Windows, mostly GUI (file dialogs, crash guard, frame pacing). A headless show
  binary needs only the UDP socket, the precise sleep and paths; psyight_soak already runs
  capture, live analysis and the show without a window.
- Operation: prep on the laptop (rig, scenes, project), copy the project to the box; a USB MIDI
  controller for the show (midi_input and midi_map exist); a screen optional. The node's fallback
  show covers a box failure.

## Pro DJ Link (from Deep Symmetry's protocol analysis, djl-analysis.deepsymmetry.org)

Level 1, passive, no risk to the booth:
- Beat packets, broadcast on UDP 50001 on each beat by playing CDJs (rekordbox-analysed tracks
  only): BPM x100, pitch, beat within the bar (1 to 4), ms to the next beat, 2nd beat, next bar,
  4th beat, 2nd bar and 8th beat (at 0% pitch, scale them). No tempo master flag.
- On-air flags, broadcast on 50001 by the DJM, per channel (off when the fader, crossfader, trim,
  filter or master level takes it out).
- CDJ-3000 position packets about every 30 ms: playhead ms, pitch, BPM. Passive receipt not
  confirmed [Verify on hardware].

Level 2, a virtual CDJ (keep-alives on 50000, status on 50002): play state, tempo master, track
ID. Track queries need an unused device number from 1 to 4, impossible with four CDJs. A wrong
keep-alive byte can make CDJ-3000s on 5 and 6 drop off the network: off by default.

Level 3, the track's analysis: the full beat grid (PQTZ: every beat's time and tempo) and phrase
analysis (PSSI, rekordbox 6+: mood high, mid or low; Intro, Up, Down, Chorus, Outro; XOR-masked).
Through the dbserver (needs a free 1 to 4 number) or the analysis files on the player's NFS share
(works with four CDJs).

Fit: packets stamped on arrival with the capture's sample position; the constant audio delay
(CDJ, mixer, interface, buffer) measured continuously against detected kicks; packets recorded
with the session for replay. DJ Link is an extra input the audio tracker cross-checks: on
disagreement audio wins and the GUI flags it.

Gains: tempo, beat and bar from the first beat (the live tracker took 20.8 s to lock on
OculusDrift), downbeats from the DJ's grid, clean mix handovers, sections from phrases. Bass is
unchanged (still audio).

Fails on: wrong or missing grids, unanalysed tracks, XDJ-RX (no protocol), Opus Quad (cannot
join), XDJ-AZ (2 of 4 decks), gear newer than the CDJ-3000 (undocumented), unlinked booths,
firmware changes. Audio alone stays the base.

Licence: write our own from the protocol description. beat-link is EPL-2.0 (not on the allowed
list); the docs state no licence, so do not copy their text or tables. Reverse-engineering
question to check before any commercial use [Verify].

Phases: 1 passive listener, recorded and drawn on the beat view, offset and jitter measured
against kicks; 2 seed the live beat PLL with an audio cross-check; 3 beat grid and phrases from
the analysis files; 4 virtual CDJ only if a real gap remains. A passive Wireshark capture of a
few minutes of linked CDJs and a DJM is enough to build and test the parser offline.

Open questions for the owner: which CDJ and mixer models, usually networked or not; a screen on
the box or MIDI only.
