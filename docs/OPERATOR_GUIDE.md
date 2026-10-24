# Psyight operator guide

For whoever runs the lights: at home or at an event, from prepared files or from a live DJ set. It assumes the rig and node are built and working (see `firmware/node-f767/README.md` for the node).

## Start

A project (a `.psyproj` file) holds the rig, patch, output, limiters, scenes, palettes and MIDI bindings. Psyight opens the last project you used; File has New project (Ctrl+N), Open project (Ctrl+Shift+O), Open recent project and Save project as (Ctrl+Shift+S), and the Rig tab has the same buttons beside the project's name. The window title shows the project and "(unsaved)" while it has changes.

Files change only when you save. Ctrl+S (File > Save) saves whatever has changes: the project and the loaded track's labels. Before anything would be lost (a new or another project, another track, quitting) Psyight asks: Save, Don't save or Cancel. Unsaved changes are also kept in a recovery copy beside the file (`show.psyproj.autosave`, `track.mp3.labels.json.autosave`) once a minute; if Psyight closes without saving them (a crash, a power cut), it offers them back the next time the project or track opens. Interface preferences (theme, depth, plot heights, volume) save by themselves.

The top bar has three parts:

- **The state ladder:** Preflight, Preview, Ambient, React, Show. Only Ambient, React and Show send to the lights.
- **The depth:** Operate, Tune or Engineer. It sets how many controls you see. Operate is enough during a show.
- **The safety controls**, always at the right:
  - Blackout (B): black at once, and the way back up still counts against the flash limit.
  - Hold (H): freezes the current look.

The app always starts in Preview, so nothing reaches the lights until you step up the ladder.

The **status bar** along the bottom shows, at a glance: the worst preflight problem in red (or the number of warnings in amber; click it to open Preflight), the tempo or the live lock, the audio device, where the detections come from, whether the show is sending (and at what frame rate), how many nodes answered, and REC while recording.

The window: the transport (play, loop, half speed, time, volume, the track's waveform) is at the top left, above the analysis and the bass roll. The rig preview is pinned at the top right; its button folds it away and the button at the far right of the top bar (or View > Rig preview) brings it back. Below it are the tabs Log, Preflight, Rig and Show (live input, MIDI, scenes, limiters, output), then scoring and the note inspector. View > Reset layout puts everything back. In the analysis, drag the thin line between two plots (spectrogram, band energy, flux, residual) to give one of them more height; double-click it for the default heights. The heights are kept with your settings.

**Show mode:** React and Show switch the app to the dark Show theme and hide the editing panels; the rig is then drawn large where the bass roll was. Leaving them restores the theme and panels. To look at the analysis without leaving React or Show, press Analysis (F7) in the top bar; press it again for the stage. File > Settings turns either change off.

## Setting up the rig

**Setup assistant** (File > Setup assistant; it opens by itself the first time, with no project file): five steps, in any order: the rig from a template, the output and the node, test patterns on the real strips, the latency from the speaker distance, and what preflight still flags.


Hover the rig preview to see each strip's name and an arrow at its first pixel (where its data comes in); the strip under the mouse is outlined, and its row lights up in the Rig tab (and hovering a row there outlines the strip).


Rig tab (Engineer depth) > New rig starts the rig from one of four sources, side by side as tabs. **Templates** build it from a few numbers: **Pillars** (how many, height, spacing or a circle, density, serpentine wiring) or a **Matrix panel** (rows, width, row spacing, density, zigzag wiring). **Presets** are 18 shapes, listed under Stage builds, Hive, Sacred geometry and Organic (hover a name for its size and groups). Each has its own settings under the list: counts (the pixel wall's rows, the lotus's petals, the sunburst's rays, the tree's splits), sizes and switches, plus **Overall width** (the whole shape scaled; its height is shown beside it) and **Density**. Defaults is the preset as described. **My rigs** are the ones you saved: name the current rig at the bottom of that tab and press "Save to my rigs" to start any project from it later; Delete removes one. **From SVG** makes the rig from a drawing (Inkscape or any vector editor, in real units such as mm): every visible path, circle, rect or line becomes a strip, its pixels running the way the path was drawn (Path > Reverse turns one round), its groups the names of the layers holding it. Hidden layers are left out, so a photo or the frame can sit in one to draw over. Width scales the whole drawing; Bottom above the floor and Density set the rest. docs/examples/hive_stage_140x75cm.svg is a starting point. The line under them shows strips, pixels, universes and the current at full white. "Replace the rig..." swaps the strips after a confirmation; scenes, palettes and settings stay. The strips get group tags (left and right, top and bottom, even and odd) that layers can be limited to. Each strip can still be edited by hand below.

## Before the show: Preflight

Open the Preflight tab. **Test patterns** check the wiring before anything else (in Preflight or Preview; "Send to the lights" puts them on the node):

- **Chase:** a short segment runs along every strip, in that strip's colour. Each should start at the end you expect.
- **Colour order:** the whole rig shows red, green, blue in turn, and the panel names the colour. A strip showing another colour needs its Order changed in the Rig tab.
- **Identify universe:** only one universe's pixels light.
- **All on:** full white through the limiters, with the current it draws. Mind the supply.

Every check line should be green; read every amber line:

- **Audio:** device, sample rate and buffer size.
- **Analysis:** the track, or live input, is analysed.
- **Rig and patch:** pixel and universe counts.
- **Power:** the supply budget against full white.
- **Limiter:** the flash limiter is on (3 flashes a second).
- **Timing:** the latency offset is set.
- **Network:** the protocol, the destination and which interface sends.
- **Nodes:** found by themselves at start, when the network changes, and every 5 s until one answers ("Discover Art-Net nodes" polls at once). If none answers although the node's console shows its address, Windows Firewall may be blocking Psyight: allow psyight.exe on public networks (a direct cable counts as one).

"Copy report" puts the whole list on the clipboard, which helps when asking for help.

**Node setup** (Preflight) works out the node's data lines from the rig: strips chained in patch order, each line short enough for 60 fps when the eight lines allow it, or with **One strip per data line**, each strip on its own pin (rigs of up to 8 strips). It shows each line's pin and header position (PD0 to PD7, all on the Zio headers; see docs/NODE_WIRING_GUIDE.pdf), what the node does when the show's signal stops (hold, fade, fallback show or blackout), then Set up the node applies it at once and Save on the node keeps it. The Rig tab's Patch by strip shows each strip's line and pin too. Show output follows the network port where the node was found.

## Two ways to run

**From files** (prepared tracks, best accuracy): drop a WAV, FLAC or MP3 onto the window and press Space to play. The whole track is analysed first, from its Demucs stems when they exist, so events are known ahead and nothing lags.

**Live** (a DJ set, unknown music): in the Show tab, open "Live input", choose a source and press Start. When the music stops (1.5 s of silence), the beat lets go and the lights fade out over 2 s; the next track's kicks lock the beat again.

- **Loopback:** whatever this computer plays (DJ software on the same laptop).
- **An input device:** the mixer's booth output into an audio interface. A virtual cable (for example "Line 1 (Virtual Audio Cable)") also works: route the DJ software to it.
- **The loaded track, as if live (silent):** plays nothing. Use it to rehearse live mode, or to check the show without sound.

Live mode needs the trained model in `data/m4/` and pauses the file. Its status line reads:

- "Listening for a steady kick": no tempo yet.
- "Acquiring": the tempo is measured and the beat loop is settling.
- "Locked": the beat is predicted. Normally within a few seconds of the first kicks.

The second line shows the analysis delay (about 15 ms), the load, the input level and any dropped samples. Dropped samples mean the computer is overloaded: close other programs.

## Correcting sections

The show picks scenes by section (groove, breakdown, build, drop). When the detection gets one wrong on a prepared track, right-click the section strip along the top of the spectrogram and choose "Edit the sections by hand". Then drag a boundary to move it (it snaps to bars; hold Alt for free), and right-click a section to change its label, split it at a bar, or merge it with the one before. The show follows your sections from then on; "Follow the detection again" switches back. The edits are saved with the track's labels and undo with Ctrl+Z.

## Labelling sections (View > Label sections, F9)

A full-window mode for labelling many tracks' sections and phrases quickly. Labelled tracks make the section detection measurable and tunable (`psyight_analyse --sections-eval <folder>` scores the detection against every track marked done).

- **Tracks** (left): Add folder... adds a music folder (subfolders included; remembered). Each track shows its status: grey not started, amber in progress, green done. Search and filter the list; click a track to load it. Next unfinished (N) saves and moves on.
- **The views** (right): the whole track, and a zoomed view that follows the playhead (mouse wheel or the slider: 8 to 128 bars). Each shows your sections (the wide strip), the detection (the thin strip under it), and the bar and phrase grid (bright lines start phrases). The zoomed view adds a waveform coloured by band (red low: kick and bass; green mid; blue high: hats and risers), then a row per bar for the kick (bright when kicked), the loudness, and the riser through builds. Click or drag to move the playhead; in the zoomed view, drag the waveform to pan (the content follows the mouse) and Shift with the wheel pans too.
- **Starting point:** a track starts from the detection (shown faded). The first edit copies it into your labels; Back to the detection drops them.
- **Keys:** Space plays or pauses. Press (or click the coloured buttons under the zoomed view) 1 intro, 2 groove, 3 breakdown, 4 build, 5 drop or 6 outro as the music changes: the boundary goes to the nearest bar (Shift: nearest phrase start; Alt: exactly at the playhead), and a boundary within half a bar is relabelled instead. Delete removes the nearest boundary. Left and Right step a bar (Shift: a phrase); [ and ] jump between boundaries. Drag a boundary to move it, or right-click a section to relabel, split or remove it.
- **Phrases and grid:** P makes the nearest bar a phrase start; choose 8 or 16 bars. D makes the nearest beat a downbeat when the grid's "one" is wrong. When the bar lines sit a little before or after the kicks, shift the grid: the Shift buttons (1 ms, 10 ms, a beat), the , and . keys (10 ms; Shift: 1 ms), or Ctrl and a drag on the waveform to slide the bar lines onto the kicks by eye. Your sections and phrase start move with it unless Sections follow is off. Use detected phrases drops the labelled phrase grid.
- **Done:** tick Done (Enter) once the whole track's sections and phrases are right; only done tracks count as ground truth. Save (Ctrl+S) writes `<track>.labels.json` beside the audio; Save and next saves and opens the next unfinished track. Esc leaves the mode.

## Latency: make the lights land on the beat

The lights should flash when the audience hears the kick, not when the computer does. Set the latency offset (Show tab, in ms) one of three ways:

- **By distance:** about 2.9 ms for every metre from the speakers to the audience.
- **By the tap test** (live mode, once locked): stand where the audience stands, and tap "Tap (T)", or the T key with the Preview focused, on the kick at least 8 times. "Use as latency offset" applies the result. The network and node add a little more, so finish by eye.
- **By eye:** adjust until the kick flash and the kick sound feel together.

## During the set

**Perform view** (View > Perform view, or F11; Esc leaves): one full-screen page for the set: the rig large with the section, the beat (four dots) and the tempo or lock; big numbered scene buttons and Auto; the 8 macros as big knobs; Blackout and Hold. The top bar stays above it.


- **Show:** the scenes follow the music's sections (groove, breakdown, build, drop) on their own.
- **React:** the scene stays as you choose it from the scene buttons (or keys 1 to 9; 0 goes back to the sections); the effects still follow the music. A scene you pick waits for the next bar (or phrase, or none: the menu beside Auto), and its button pulses until it takes over.
- **Ambient:** the calm ambient scene only. Use it between sets, during talks, or whenever the analysis looks wrong.
- **Blackout and Hold** work in every state.

Watch the live status: if "Locked" turns to "Acquiring" (a new track at another tempo), the beat keeps running and settles within a few kicks. Long breakdowns are fine: the beat keeps time until the kick returns. If the lights clearly fight the music for more than a few bars, go to React or Ambient.

The bar position (which beat is "one") comes from kicks returning after a break. Until the first break of a set it can be a beat out. Effects that change on the downbeat may look off by a beat until then.

Live controls (Live input section, once locked):

- **Downbeat now (D):** press on the "one" you hear. The nearest beat becomes the downbeat, so pressing a little late is fine.
- **-10 ms and +10 ms:** nudge the predicted beat. The loop then pulls it back towards the kicks it hears.
- **Relock:** measure the tempo again from the next kicks, for example after a tempo change the loop has not caught.

## Macros, layers and modulation

The Show tab works like a rack in Ableton Live:

- **Macros:** 8 knobs, saved with the project. They do nothing alone; modulation routes use them as sources, so one knob (or a MIDI knob bound to it) can fade a layer or shift its hue live. The pencil names them.
- **Layers** (Tune depth, under "Scene"): each layer is a device, bottom layer first. The power button switches it, the knobs set opacity and the effect's settings, the menus its blend mode and strip group. Drag a device's name to reorder; right-click it to move or remove. "+" adds a layer.
- **Modulation:** each route is a card: source, curve, target (opacity, hue shift, the scene's brightness, or any of a layer's settings, such as Drift or Speed) and layer, then Hold (free, or sampled once per 16th, beat or bar) and the Amount, Attack and Release knobs. A blue ring on a knob shows where modulation is moving it right now (only for the scene that is playing).

Knobs: drag up or down (Shift for fine), double-click to type. Right-click a knob for its menu: Reset to default; on layer knobs, Modulate with (pick a source: a route appears), Map to macro (that macro then sweeps the knob from where it is to the top) and Remove modulation; on the macros and the brightness cap, Learn MIDI (then move a control on the controller) and Forget.

**Presets** (Tune depth):

- **Show packs:** a whole show style in one go: scenes, palettes, automation and macro names. Built-in: Psyight default, Dark minimal, Full colour, Live macros. Loading asks first; macro values, the rig, limiters and output stay. "Save this show as a pack" keeps yours.
- **Scene presets** (the scene's Presets button): a look with its layers, modulation and palette. Replace the scene you are editing, or add it as a new one. "Save this scene as a preset" keeps yours.
- **Palettes:** "Add from the library" for more palettes (none use white: it is kept for kicks and drops); "Save to the library" keeps one.

**Undo:** Ctrl+Z and Ctrl+Y undo and redo show edits (scenes, layers, modulation, macros, palettes, the rig, settings) while the Show, Rig or Preview panel has the focus, and label edits elsewhere. A knob drag undoes in one step. The Edit menu lists both.

**Browser** (View > Browser, or Ctrl+B): search every preset; hover one (or click to select it) to see it on the rig preview while the lights keep the show, then load it from the bottom of the browser.

Your presets are JSON files in the `presets` folder in Psyight's folder (`scenes`, `palettes`, `shows`, and `rigs` for your rigs), easy to copy to another machine.

## MIDI controller

In the Show tab, open "MIDI" and choose the controller. For each control you want, press Learn, then move the knob or press the pad. You can bind:

- blackout, hold, automation (back to Show) and each scene, to buttons or pads
- the brightness cap and the latency offset, to knobs or faders
- the 8 macros, to knobs or faders
- the tap test, and "downbeat now" (a pad for the "one")

Bindings are saved with the project, and the controller reconnects on the next start.

## If something goes wrong

- **No lights:** check that the ladder is on Ambient, React or Show, then the Preflight tab (Network and Nodes lines). The node's console shows whether packets arrive.
- **The app crashed or the power went:** the lights do not go black at once. After 2 s without data the node runs its own loss policy (by default a dim violet fallback show). Restart Psyight: it reports the unclean exit in the Log tab. The labels and project are at most a minute old. The crash details are in `logs/` (`psyight.log` and any `crash-*.dmp`).
- **Live mode never locks:** check the input level on the status line. Loopback captures only what plays through the default output device. With an interface, check the input gain.
- **Too bright, or flashing too much:** the flash limiter caps flashes at 3 a second (it can only be turned off in Engineer depth). Lower the brightness cap in the Limiters section, or bind it to a fader.

## Recording a show

Record (top bar) writes the session to `recordings/<date and time>/`: frames, parameter changes and events. `psyight_replay` replays it to check that the show reproduces exactly.
