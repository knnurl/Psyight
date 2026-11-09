# Psyight tutorial

From a rig on the wall to lights that follow the bassline, in the order you set up a show: the window, the rig, the node, the music, the look, the performance.

**Two depths.** Each lesson's main text is the essentials: enough to set up and run a show. Open the **In depth** blocks for every control, the measurements behind live mode, and the test tools. Lessons 10 and 11 are in depth only. The full reference is the [operator guide](OPERATOR_GUIDE.md).

Nothing reaches the lights until you say so. The top bar's ladder decides: **Preflight, Preview, Ambient, React, Show**. Only Ambient, React and Show send to the lights, and Psyight always starts in Preview.

**Lessons**

1. [The window](#1-the-window)
2. [Your rig](#2-your-rig)
3. [The node and Preflight](#3-the-node-and-preflight)
4. [Running from files](#4-running-from-files)
5. [Running live](#5-running-live)
6. [Building the show](#6-building-the-show)
7. [Performing](#7-performing)
8. [Safety limits](#8-safety-limits)
9. [When something goes wrong](#9-when-something-goes-wrong)
10. [Labelling and scoring](#10-labelling-and-scoring) (in depth)
11. [Recording and test tools](#11-recording-and-test-tools) (in depth)

---

## 1. The window

*Everywhere: the top bar, the status bar, the panels.*

The top bar has three parts. The **ladder** decides what reaches the lights. The **depth** (Operate, Tune, Engineer) decides how many controls you see: Operate is enough during a show. The **safety controls** sit at the right and work in every state:

- **Brightness:** the brightness cap as a percentage.
- **Blackout** (<kbd>B</kbd>): black at once.
- **Hold** (<kbd>H</kbd>): freezes the current look.

The panels: the transport and the track analysis (spectrogram and bass roll) on the left, the rig preview pinned at the top right, and below it the tabs **Log**, **Preflight**, **Rig** and **Show**. The status bar along the bottom shows the worst problem in red, the tempo or live lock, the audio device, whether the show is sending and how many nodes answered.

Files change only when you save: <kbd>Ctrl</kbd>+<kbd>S</kbd> saves the project and the track's labels. A recovery copy is written every minute, so a crash or power cut loses at most a minute.

<details>
<summary><b>In depth</b></summary>

- **Show mode:** React and Show switch to the dark Show theme and hide the editing panels; the rig is drawn large where the bass roll was. <kbd>F7</kbd> (Analysis) flips back to the analysis without leaving the state. File > Settings turns either change off.
- **Knobs:** drag up or down (<kbd>Shift</kbd> for fine), double-click to type a value and press <kbd>Enter</kbd>, right-click for Reset, Modulate with, Map to macro and Learn MIDI.
- **Undo:** <kbd>Ctrl</kbd>+<kbd>Z</kbd> and <kbd>Ctrl</kbd>+<kbd>Y</kbd> for show edits while the Show, Rig or Preview panel has the focus, and for label edits elsewhere. A whole knob drag undoes in one step.
- **Layout:** drag the thin line between two analysis plots to resize them (double-click for the defaults). View > Reset layout puts every panel back. View > Browser (<kbd>Ctrl</kbd>+<kbd>B</kbd>) searches every preset and auditions it on the preview while the lights keep the show.

</details>

## 2. Your rig

*File > Setup assistant, or the Rig tab (Engineer depth).*

The rig is the shape of your strips: where every pixel sits. The show maps effects onto it, so a hexagon gets rings and spokes, a matrix gets rows.

1. Open **File > Setup assistant**. It walks through the rig, the output, test patterns, latency and the remaining checks. It opens by itself the first time.
2. Or open the **Rig** tab, set the depth to Engineer, and open **New rig**. Pick a source: **Templates** (pillars or a matrix panel), **Presets** (18 shapes, the Hexagon mandala among them), **My rigs** (rigs you saved), or **From drawing** (an SVG or DXF).
3. Check the summary line (strips, pixels, metres, current at full white), then press **Replace the rig...**. Scenes, palettes and settings stay.
4. In **Current rig**, the strips table is the order on the data line: wire the strips in that order, from each strip's first point.

Hover the rig preview: each strip shows its name and an arrow at its first pixel, where the data comes in.

<details>
<summary><b>In depth</b></summary>

**Presets and their settings.** Each preset has **Overall width** first (it scales the whole shape), then counts, lengths and switches, each showing its real size, then **Density** in pixels per metre (60 is common WS2812 strip). While a preset made the current rig, changing its settings rebuilds the strips, keeping each one's colour order and wiring place by name. Tick **Lock** to keep hand edits.

**Editing on the Stage.** Tick **Edit strips** in the Front view. Click to select (<kbd>Ctrl</kbd> adds), drag the square handles to move points, drag the strip to move it (<kbd>Shift</kbd> snaps to 1 cm), <kbd>Q</kbd> and <kbd>E</kbd> rotate by 15 degrees, right-click to reverse, split, add or remove a point. **Draw strip** adds one point by point.

**Joints, groups and building it.**
- Strips stop 5 mm short where they meet, and 5 mm either side of sharp corners, so the layout is buildable. A typed count below what fits is spread evenly.
- Strips get group tags (left, right, top, bottom, even, odd, and the preset's own, such as `hex`, `spokes`, `centre_y`). Layers and brightness caps can be limited to a group.
- **Export drawing...** writes the rig as an SVG or DXF in millimetres: centre lines, channels, pixel dots, wiring arrows. **Build kit** writes the back panel, spacer, acrylic and front mask to laser-cut, or one stacked DXF for Fusion.
- **Save to my rigs** (bottom of the My rigs tab) keeps the current rig to start any project from.

</details>

## 3. The node and Preflight

*Preflight tab, and Show > Output.*

The node turns network packets into the strips' data signal. Psyight sends sACN, Art-Net or DDP to it.

1. In **Show > Output**, choose the protocol, then open **Nodes** and tick the nodes that answered discovery: one, or several at once (each gets every frame and shows the universes it is set up for). **Other address** adds a node that does not answer discovery.
2. Open **Preflight**. Run the test patterns with **Send to the lights**: **Chase** (each strip should start at the end you expect), **Colour order** (red, green, blue in turn; a wrong colour means that strip's Order needs changing in the Rig tab), **Identify universe**, **All on** (mind the supply).
3. Read every check line. All should be green; read each amber one: audio, analysis, rig and patch, power, limiter, timing, network, nodes.
4. Open **Node setup**, press **Set up the node**, then **Save on the node** so it keeps the settings after a restart.

> If the node's console shows its address but Preflight finds no node, Windows Firewall is probably blocking Psyight. Allow `psyight.exe` on public networks; a direct cable counts as one.

<details>
<summary><b>In depth</b></summary>

**Data lines and what the node does alone.** Node setup works out the data lines from the rig: strips chained in patch order, each line short enough for 60 fps when the eight lines allow it, or **One strip per data line** for rigs of up to 8 strips (each strip on its own pin, PD0 to PD7 on the Zio headers; see [NODE_WIRING_GUIDE.pdf](NODE_WIRING_GUIDE.pdf)). **Order for wiring...** reorders strips only where it saves 5 cm or more of jumper.

When the show's signal stops for 2 s, the node runs its own loss policy: hold, fade, blackout, or a fallback show (by default a dim aurora). It returns to the show as soon as sACN, Art-Net or DDP data arrives again.

**The node's console.** On its USB serial port (115200 baud) the node prints its address, frame rate, packets and losses every 5 s. See [the node's README](../firmware/node-f767/README.md).

</details>

## 4. Running from files

*Drop a track on the window, or the Library.*

From files is the most accurate mode: the whole track is analysed before it plays, so every kick, bass note, build and drop is known in advance and nothing lags.

1. Drop a WAV, FLAC or MP3 onto the window and press <kbd>Space</kbd>.
2. Watch the analysis: the spectrogram with the section strip along its top, and the bass roll with every detected note.
3. Step the ladder up to **Show**. The scenes follow the sections by themselves.
4. If a section is wrong, right-click the section strip, choose **Edit the sections by hand**, and drag the boundary (it snaps to bars; <kbd>Alt</kbd> for free).

<details>
<summary><b>In depth</b></summary>

**Library and Queue.** **Library** (on the menu bar) holds your music folders, a rekordbox XML export and imported playlists (M3U8 is the most reliable). Double-click to play, drag to the **Queue**. While a track plays, the next is analysed in the background, so the music runs straight on and the lights follow the new track at once.

**Reading the bass roll.** Each detected note shows its onset, length, gate and pitch on a 16th-note grid. Click a note for the inspector: onset, offset, gate ratio, pitch in MIDI and cents, decay, slides, velocity and confidence. With labels, the Scoring panel measures the detection: onset F at plus or minus 20 ms, offsets, pitch within 50 cents, gate error.

**Where accuracy comes from.** When Demucs stems exist for a track, the analysis runs on them; otherwise on the mix. The M4 student model (a small network trained on labelled tracks) can replace the DSP bass tracker: compare them in the bass roll.

</details>

## 5. Running live

*Show tab > Live input.*

Live mode listens to whatever is playing, a DJ set or unknown music, and analyses it as it arrives. It cannot look ahead, so it is less exact than files, but it locks to the beat within a few seconds of the first kicks.

1. Open the **Show** tab and its **Live input** section.
2. Choose the **Source**:
   - **Loopback:** whatever this computer plays through its default output.
   - **An input device:** a mixer's output into an audio interface, a DJ mixer over USB, or a virtual cable.
   - **The loaded track, as if live:** plays nothing; for rehearsing.
3. For a device with many channels, such as a DJ mixer over USB, choose **Channels**. Start with **All, mixed**, look at the channel levels to see which pair carries the master, stop, and choose that pair.
4. Tick **Record** to save what the analysis hears to `data/recordings/`.
5. Press **Start**. The status goes from **Listening for a steady kick** to **Acquiring** to **Locked**.
6. Set the latency once: stand where the audience stands and tap **Tap** (<kbd>T</kbd>) on the kick at least 8 times, then **Use as latency offset**.

While locked: **Downbeat now** (<kbd>D</kbd>) on the "one" you hear fixes the bar position, **-10 ms** and **+10 ms** nudge the beat, **Relock** measures the tempo again. When the music stops for 1.5 s the lights fade out over 2 s, and the next track's kicks lock again.

<details>
<summary><b>In depth</b></summary>

**Getting the audio in.**
- **DJ software with a controller (Serato, for example):** the software drives the controller through ASIO, which Psyight cannot read and loopback does not hear. In Serato, tick **Make Audio Available to Other Applications** in the Audio settings and choose **Line 1 (Virtual Audio Cable)** as the source.
- **Pioneer DJM-900NXS2 over USB:** install the DJM driver, then in its Settings Utility, MIXER OUTPUT tab, set a USB pair to `MIX (REC OUT)`. If Windows lists the mixer under Sound > Recording, Psyight can use it; then pick that pair under Channels.
- **An audio interface:** the mixer's master, booth or record output into a class-compliant interface. Run the laptop on battery to avoid hum.

**What live mode can and cannot know.**
- The beat is predicted, so it has no lag once locked. Decisions about kicks come about 16 ms after the audio, bass onsets about 27 ms; the second status line shows the measured delay and load.
- Measured on 20 recorded DJ sets (33.8 hours): locked at 99.6% of kicks inside a run, and within 20 ms at 98.6% of those.
- A drop counts only after 4 kicks in a row, so it is known about 1.2 s after it lands, and about a third of drops are missed. Effects timed to a build's last beat (the stutter gap, the build roll) only fire in file mode, because live mode cannot know when a build ends.
- Which beat is "one" comes from kicks returning after a break, so it can be a beat out until the first break: press <kbd>D</kbd>.

**The recording.** A recording is a mono 32-bit float WAV of exactly what the analysis heard, with a `.clock.csv` beside it (the live clock beside the system's steady clock and UTC, once a second). Replay it with `psyight_live_check` to reproduce the run, or line it up with a Pro DJ Link log.

</details>

## 6. Building the show

*Show tab (Tune depth).*

A show is a set of scenes. Each scene is tagged with the sections it suits (groove, breakdown, build, drop, intro, outro), and the automation picks one as the music moves.

1. Start from a **show pack**: Psyight default, Dark minimal, Full colour, Live macros, Psy club, Dark forest or Hexagon (made for hexagonal rigs: tunnels and a radar sweep in grooves, a nebula in breakdowns, Hex pressure through builds, a blooming drop).
2. Open a scene to see its **layers**, bottom first, like devices in a rack: a power button, opacity, the effect's knobs, a blend mode and a strip group.
3. Name the 8 **macros** and route them: right-click a layer knob, **Map to macro**. One macro knob, or a MIDI fader bound to it, can then sweep it live.
4. Save what you like: **Save this scene as a preset**, or **Save this show as a pack**.

<details>
<summary><b>In depth</b></summary>

**Modulation.** Each route is a card: a source (Kick, Hat, Bass note on, Bass velocity, Bass gate ratio, Bass pitch, Riser, Intensity, Section progress, Section energy, beat, bar and phrase phases and LFOs, Random per bar, Residual level and brightness, or one of the 8 macros), a curve, a target (opacity, hue shift, the scene's brightness, or any setting of a layer), Hold (free, or sampled per 16th, beat or bar), and Amount, Attack and Release. A blue ring on a knob shows where modulation moves it right now.

**Automation.**
- **Crossfade** into intros, breakdowns, builds and outros; grooves and drops cut in. **Palette every** so many phrases.
- **Next scene every** so many phrases in long sections, cut or faded over **Change fade** beats.
- **Phrase-end fill** over a phrase's last beats: Gap, Stutter, Sweep, Implode, Pulse, or Mix.
- **Vary every** so many phrases: Reverse, Remap, Retime, Mirror, Mute a layer.
- **Follow the energy:** **Dim when quieter** and **Energy picks the scene each phrase**.

**Palettes.** None of the library palettes use white: it is kept for kicks and drops. The automation shifts the palette by one every few phrases.

</details>

## 7. Performing

*View > Perform view (<kbd>F11</kbd>).*

Perform view is one full-screen page for the set: the rig large, the section, four beat dots, the tempo or lock, big scene buttons and Auto, the 8 macros as big knobs, Blackout and Hold. <kbd>Esc</kbd> leaves it.

- **Show:** scenes follow the sections by themselves.
- **React:** the scene stays as you pick it (scene buttons, or <kbd>1</kbd> to <kbd>9</kbd>; <kbd>0</kbd> goes back to the sections). The effects still follow the music. A picked scene waits for the next bar and its button pulses until it takes over.
- **Ambient:** a slow aurora at half the brightness cap, ignoring the music. Use it between sets or whenever the analysis looks wrong.

If the lights clearly fight the music for more than a few bars, go to React or Ambient.

<details>
<summary><b>In depth</b></summary>

**MIDI controller.** Show tab > MIDI: choose the controller, press Learn, move the control. Bindable: blackout, hold, back to Show, each scene, the brightness cap, the latency offset, the 8 macros, the tap test and Downbeat now. Bindings are saved with the project and the controller reconnects at the next start.

**Scene launch timing.** The menu beside Auto sets when a picked scene takes over: next bar, next phrase, or at once. **Every scene...** lists the show's scenes and every preset by section; picking a preset adds it to the show.

**Full brightness** (top right of the rig preview) shows the show undimmed on screen while the lights stay dimmed.

</details>

## 8. Safety limits

*Show tab > Limiters, and the top bar.*

- **Brightness:** every pixel at most this. Also in the top bar; bind it to a fader.
- **Power** (Tune depth): the supply budget in amps, up to 20 A. Each frame is scaled to stay under it; 0 turns it off. Set it to what your 5 V supply can deliver, with some margin.
- **Flash limiter:** at most 3 flashes a second, on by default.
- **Blackout** (<kbd>B</kbd>) and **Hold** (<kbd>H</kbd>): in every state.

> Turning the flash limiter off (Engineer depth only) risks triggering photosensitive epilepsy: rolling 16ths at 160 BPM are a 10.7 Hz strobe. A red banner stays up while it is off.

<details>
<summary><b>In depth</b></summary>

Each strip group can have its own brightness cap (the knobs after Power, one per group tag); the lowest cap wins. The power estimate uses about 20 mA per channel at full (WS2812B), summed over the rig, so check it against a real measurement with **All on**.

If Psyight crashes or the laptop dies, the lights do not go black at once: after 2 s without data the node runs its loss policy.

</details>

## 9. When something goes wrong

| Symptom | Check |
|---|---|
| No lights | The ladder is on Ambient, React or Show; Preflight's Network and Nodes lines; the node's console shows packets arriving. |
| Node not found | Windows Firewall: allow `psyight.exe` on public networks. |
| Live mode never locks | The input level on the status line. Loopback hears only the default output device; ASIO-only devices are invisible (use the DJ software's virtual cable output). |
| Lights a beat off the "one" | Press <kbd>D</kbd> on the downbeat. |
| Lights late or early | The latency offset: tap test, or about 2.9 ms per metre from the speakers. |
| Too bright or too much flashing | Lower the brightness cap; keep the flash limiter on. |
| Crash or power cut | Restart: it reports the unclean exit and offers the recovery copy. Crash details are in `logs/`. |

## 10. Labelling and scoring

*In depth. View > Label sections (<kbd>F9</kbd>), and the bass roll's labelling tools.*

Labels are the ground truth that makes detection measurable. Bass notes are labelled in the bass roll (create, move, resize, snap to 1/16, set pitch). Sections are labelled in the full-window mode:

- **Add folder...** lists tracks with their status (grey, amber, green). <kbd>N</kbd> saves and opens the next unfinished one.
- Press <kbd>1</kbd> intro, <kbd>2</kbd> groove, <kbd>3</kbd> breakdown, <kbd>4</kbd> build, <kbd>5</kbd> drop, <kbd>6</kbd> outro as the music changes; the boundary snaps to the nearest bar (<kbd>Shift</kbd>: phrase, <kbd>Alt</kbd>: exact).
- <kbd>P</kbd> marks a phrase start, <kbd>D</kbd> a downbeat; <kbd>,</kbd> and <kbd>.</kbd> shift the grid by 10 ms.
- Tick **Done** (<kbd>Enter</kbd>) when the whole track is right: only done tracks count. Labels save as `<track>.labels.json` beside the audio.

`psyight_analyse --sections-eval <folder>` scores the section detection against every done track.

## 11. Recording and test tools

*In depth. The top bar's Record, and the command-line tools in `app\build\windows-msvc\release\`.*

| Tool | What it does |
|---|---|
| **Record** (top bar) | Writes the session (frames, parameter changes, events) to `recordings/<date and time>/`. |
| `psyight_replay` | Replays a session and checks that every frame reproduces exactly. |
| `psyight_live_check` | Runs live mode over a file as if live and scores it against file mode and the labels. `--set` for a DJ set (scored against its own kicks in hindsight), `--show <project>` for a timeline of the show it would render, `--trace a b` for the beat state at every kick, `--gain-db` to test a quieter input. |
| `psyight_show_check` | Renders a whole track as a show offline: flashes before and after the limiter, current, dark share, time per scene. |
| `tools/python/djlink_log.py` | Logs every Pro DJ Link packet from the CDJs and the mixer, listening only. `--tcnet` logs PRO DJ LINK Bridge's TCNet output instead. Quit rekordbox and AnyDesk first: they hold the ports. |

Commands are run from the repository folder, for example `app\build\windows-msvc\release\psyight_live_check.exe "set.mp3" --set`.
