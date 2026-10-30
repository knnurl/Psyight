# Psyight operator guide

For whoever runs the lights: at home or at an event, from prepared files or from a live DJ set. It assumes the rig and node are built and working (see `firmware/node-f767/README.md` for the node).

## Start

A project (a `.psyproj` file) holds the rig, patch, output, limiters, scenes, palettes and MIDI bindings. Psyight opens the last project you used; File has New project (Ctrl+N), Open project (Ctrl+Shift+O), Open recent project and Save project as (Ctrl+Shift+S), and the Rig tab has the same buttons beside the project's name. The window title shows the project and "(unsaved)" while it has changes.

Files change only when you save. Ctrl+S (File > Save) saves whatever has changes: the project and the loaded track's labels. Before anything would be lost (a new or another project, another track, quitting) Psyight asks: Save, Don't save or Cancel. Unsaved changes are also kept in a recovery copy beside the file (`show.psyproj.autosave`, `track.mp3.labels.json.autosave`) once a minute; if Psyight closes without saving them (a crash, a power cut), it offers them back the next time the project or track opens. Interface preferences (theme, depth, plot heights, volume) save by themselves.

The top bar has three parts:

- **The state ladder:** Preflight, Preview, Ambient, React, Show. Only Ambient, React and Show send to the lights.
- **The depth:** Operate, Tune or Engineer. It sets how many controls you see. Operate is enough during a show.
- **The safety controls**, always at the right:
  - Brightness: the brightness cap as a percentage (the same as Show > Limiters > Brightness): drag up or down (Shift: fine), double-click to type, right-click to reset or MIDI-learn it.
  - Blackout (B): black at once, and the way back up still counts against the flash limit.
  - Hold (H): freezes the current look.

The app always starts in Preview, so nothing reaches the lights until you step up the ladder.

The **status bar** along the bottom shows, at a glance: the worst preflight problem in red (or the number of warnings in amber; click it to open Preflight), the tempo or the live lock, the audio device, where the detections come from, whether the show is sending (and at what frame rate), how many nodes answered, and REC while recording.

The window: the transport (play, loop, half speed, time, volume, the track's waveform) is at the top left, above the analysis and the bass roll. The rig preview is pinned at the top right; its button folds it away and the button at the far right of the top bar (or View > Rig preview) brings it back. Below it are the tabs Log, Preflight, Rig and Show (live input, MIDI, scenes, limiters, output), then scoring and the note inspector. View > Reset layout puts everything back. In the analysis, drag the thin line between two plots (spectrogram, band energy, flux, residual) to give one of them more height; double-click it for the default heights. The heights are kept with your settings.

**Show mode:** React and Show switch the app to the dark Show theme and hide the editing panels; the rig is then drawn large where the bass roll was. Leaving them restores the theme and panels. To look at the analysis without leaving React or Show, press Analysis (F7) in the top bar; press it again for the stage. File > Settings turns either change off.

## Setting up the rig

**Setup assistant** (File > Setup assistant; it opens by itself the first time, with no project file): five steps, in any order: the rig from a template, the output and the node, test patterns on the real strips, the latency from the speaker distance, and what preflight still flags.


Hover the rig preview to see each strip's name and an arrow at its first pixel (where its data comes in); the strip under the mouse is outlined, and its row lights up in the Rig tab (and hovering a row there outlines the strip).


Rig tab (Engineer depth) > New rig starts the rig from one of four sources, side by side as tabs. **Templates** build it from a few numbers: **Pillars** (how many, height, spacing or a circle, density, serpentine wiring) or a **Matrix panel** (rows, width, row spacing, density, zigzag wiring). **Presets** are 18 shapes, listed under Stage builds, Hive, Sacred geometry and Organic (hover a name for its size and groups). Each has its own settings under the list, **Overall width** first: it resizes the whole shape (every length below it changes with it; its height is shown beside it). Then counts (the pixel wall's rows, the lotus's petals, the sunburst's rays, the tree's splits), lengths and switches: each length shows its real size and changes only itself (and so the overall width), so a hexagon's Outer radius of 0.150 m is 15 cm from the middle to a corner, along the strip's centre line. Then **Density** in pixels per metre (60 is the common WS2812 strip; a preset with strips at two densities shows the other one beside it). The summary line shows the metres of strip the rig needs. Defaults is the preset as described. **My rigs** are the ones you saved: name the current rig at the bottom of that tab and press "Save to my rigs" to start any project from it later; Delete removes one. **From drawing** makes the rig from an SVG or DXF drawing (Inkscape, or Fusion: right-click a sketch, Save As DXF; LibreCAD, KiCad; in real units such as mm): every visible path, circle, rect or line becomes a strip, its pixels running the way the path was drawn (Path > Reverse turns one round), its groups the names of the layers holding it. Hidden layers are left out, so a photo or the frame can sit in one to draw over. Width scales the whole drawing; Bottom above the floor and Density set the rest. docs/examples/hive_stage_140x75cm.svg is a starting point. In a DXF, depth comes in too (3D lines, 3D polylines, splines, and circles and arcs at their elevation), so a 3D sculpture drawn in Rhino or Fusion keeps its shape; layers are the groups, off and frozen layers are left out, and since DXF stores arcs and circles anticlockwise, those on the left of the middle are turned round (and circles start at their point nearest the middle), so mirrored halves run from the middle outwards; lines and polylines run the way they were drawn. **Current rig** (beside New rig) holds the rig as it is: when a preset made it, that preset's settings, which rebuild the rig when changed (each strip keeps its colour order and wiring place by name; points moved by hand are lost), unless **Lock** is ticked, which keeps the strips exactly as they are; then the strips table and the patch.

**Editing on the Stage or Preview:** the view list picks Front (as the audience sees it), Top (from above, the audience at the bottom), Side (from the right) or Orbit (drag to turn the rig; far pixels are dimmer). The mouse wheel zooms, a drag on empty space pans, Fit view (or a double-click) shows the whole rig. Tick **Edit strips** to edit: click a strip to select it (Ctrl adds), and its facts (pixels, length, density, groups, node line and pin) show above; drag its square handles to move points, or the strip itself to move the selection (Shift snaps to 1 cm); Q and E rotate the selection by 15 degrees; right-click a strip to reverse it, split it there (a ring starts there instead), add or remove a point, or delete it; Delete removes the selection; Draw strip adds one by clicking points (double-click or Enter ends). A strip that filled its path keeps filling it. Every change is undoable. Editing works in the Front view.

**Export drawing...** (next to the project buttons) writes the current rig as an SVG or DXF in millimetres to build from, each on a layer of its own: centre lines (where the LEDs sit), channels (each path offset either side by the width you set, to cut grooves or a mount), pixels (a dot at every LED) and wiring (an arrow at each strip's data-in end, with its place on the data line, name, line and pin). Each layer has a colour you choose, which laser software (LightBurn, RDWorks) maps to an operation; by default channels are black (cut), pixel dots red and filled (engrave), centre lines blue and wiring green. To import an export back, hide all layers but the centre lines. Each strip's place on the data line is also drawn as a number in lines on the strip itself, just past its arrow and running along it (kept upright), which CAD and laser software keep.

**Build kit** (in the same dialog) writes four files to cut a layered panel, back to front: the back panel (outline, bolt holes, a wire hole at every strip end; engraved: where each spacer island goes, with its number), the spacer (the islands between the channels, the channels' walls, numbered, with bolt holes), one acrylic lattice over every channel (the channel plus the overlap each side), and the front mask (narrower windows that clamp the acrylic, bolt holes). Set the window width, acrylic overlap, panel margin and hole sizes there; it uses the channel width and colours above. Use opal or black LED acrylic rather than coloured: coloured acrylic filters the LEDs' colours. Glue each spacer island onto its engraved place, lay the acrylic, then bolt the front through everything. Style **Open lattice** drops the board: the back and front follow the channels (each channel plus the wall width each side), the openings between rings and spokes stay open so the wall shows through, and the spacer is the walls, each a frame round an opening. A wall too thin for a bolt (under about 9.5 mm) gets none: glue the walls to the back, the acrylic onto the walls and the front onto the acrylic. Cut 5 mm walls in MDF or good birch ply; the node and power supply then live in a separate box. In both styles, bolts stay clear of the acrylic. **Build kit, one DXF** puts all four parts in a single file, each on its own layers named with the heights it spans (1_back_z0-6mm_cut, 2_spacer_z6-15mm_cut and so on; 4.5 mm is written 4p5, as DXF layer names allow no dot), stacked at its height and as thick as its sheet (set the sheet thicknesses there), with the rig export (centre lines, channels, pixels, wiring, as ticked) on the back panel's top face where the strips sit: the assembled panel in a 3D viewer such as Rhino. For Fusion, the script tools/fusion/PsyightBuildKit (Utilities > Add-Ins > Scripts and Add-Ins, + to add the folder, then Run, and pick the DXF) builds it from the layer names: a component per part with its body extruded through its sheet at its height (holes open, wood or translucent plastic look), the engraving as a sketch on each part's top, and the rig's layers as sketches at the LEDs' height. Centre lines of solid rings makes a ring drawn as two outlines (a cut part's inner and outer edge, up to the width you set, 3 cm by default) into one strip along its middle. The line under them shows strips, pixels, universes and the current at full white. "Replace the rig..." swaps the strips after a confirmation; scenes, palettes and settings stay. The strips get group tags (left and right, top and bottom, even and odd) that layers can be limited to. The Hexagon mandala's **Centre Y** switch adds three spokes inside the inner hexagon, from the middle to alternate corners, tagged only `centre_y`: with them lit the rings read as nested cubes seen corner-on. To switch between the two looks live, limit the scene's layers to the `hex` group (everything but the centre Y) and add a layer limited to `centre_y`: its power button, or a macro on its opacity (MIDI-learnable), is the switch. Each strip can still be edited by hand below. **Joints:** LED strip can't cross another strip or bend sideways round a corner, so Psyight lays pixels out the way you will build it: a strip stops 5 mm short of a strip its end meets, 5 mm either side of its own sharp corners (a hexagon's), and where two strips cross one is cut 5 mm either side. A spoke ending on a ring's corner runs into the gap the corner leaves and puts its last pixel on the corner (a spoke runs through a ring; the ring is cut and joined with a wire across). Counts follow: a strip fills what is left of its path after any change, unless you typed its count in the strips table (type the number that fits, shown in the count's tooltip, to make it follow the path again). A typed count below what fits is spread evenly over the strip's pieces, each centred: a hexagon built with 26 pixels a side where 27 fit takes 156. Strips under 20 px/m (single pixels on wires, such as the Hive stage's gems) have no joints.

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

**Node setup** (Preflight) works out the node's data lines from the rig: strips chained in patch order, each line short enough for 60 fps when the eight lines allow it, or with **One strip per data line**, each strip on its own pin (rigs of up to 8 strips). It shows each line's pin and header position (PD0 to PD7, all on the Zio headers; see docs/NODE_WIRING_GUIDE.pdf), what the node does when the show's signal stops (hold, fade, fallback show or blackout), then Set up the node applies it at once and Save on the node keeps it. The Rig tab's Patch by strip shows each strip's line and pin too. The strips' order in the Rig tab is the order on the data line, each from its first point: wire them that way, or change the order to match the wiring with the arrows in the Wiring column, or with Order for wiring..., which keeps your order except where moving a strip, or turning it round, saves 5 cm or more of jumper (it says when nothing needs changing, and shows the jumpers' total before and after). Show output follows the network port where the node was found.

## Two ways to run

**From files** (prepared tracks, best accuracy): drop a WAV, FLAC or MP3 onto the window and press Space to play. Started without a file, Psyight opens the last track you opened (the first in File > Open recent track that still exists), paused. The whole track is analysed first, from its Demucs stems when they exist, so events are known ahead and nothing lags.

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
- **Phrases and grid:** P makes the nearest bar a phrase start; choose 8 or 16 bars. Until you do, phrases count from bar 1 when the detection is unsure of them. D makes the nearest beat a downbeat when the grid's "one" is wrong. When the bar lines sit a little before or after the kicks, shift the grid: the Shift buttons (1 ms, 10 ms, a beat), the , and . keys (10 ms; Shift: 1 ms), or Ctrl and a drag on the waveform to slide the bar lines onto the kicks by eye. Your sections and phrase start move with it unless Sections follow is off. To number the bars as the music counts them (a track whose first bars are cut), put the playhead on a bar line, type its number and press Number the bar here; bar 1 may then lie before the start of the file. Shifting the grid keeps every bar's number. Use detected phrases drops the labelled phrase grid.
- **Done:** tick Done (Enter) once the whole track's sections and phrases are right; only done tracks count as ground truth. Save (Ctrl+S) writes `<track>.labels.json` beside the audio; Save and next saves and opens the next unfinished track. Esc leaves the mode.

## Latency: make the lights land on the beat

The lights should flash when the audience hears the kick, not when the computer does. Set the latency offset (Show tab, in ms) one of three ways:

- **By distance:** about 2.9 ms for every metre from the speakers to the audience.
- **By the tap test** (live mode, once locked): stand where the audience stands, and tap "Tap (T)", or the T key with the Preview focused, on the kick at least 8 times. "Use as latency offset" applies the result. The network and node add a little more, so finish by eye.
- **By eye:** adjust until the kick flash and the kick sound feel together.

## During the set

**Perform view** (View > Perform view, or F11; Esc leaves): one full-screen page for the set: the rig large with the section, the beat (four dots) and the tempo or lock; big numbered scene buttons and Auto; the 8 macros as big knobs; Blackout and Hold. The top bar stays above it.


- **Show:** the scenes follow the music's sections (groove, breakdown, build, drop) on their own.
- **React:** the scene stays as you choose it from the scene buttons (or keys 1 to 9, on the number row or the keypad; 0 goes back to the sections); the effects still follow the music. A scene you pick waits for the next bar (or phrase, or none: the menu beside Auto), and its button pulses until it takes over.
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

- **Show packs:** a whole show style in one go: scenes, palettes, automation and macro names. Built-in: Psyight default, Dark minimal, Full colour, Live macros, Psy club, Dark forest and Hexagon (for hexagonal rigs such as the Hexagon mandala: hex tunnels and a radar sweep in grooves, comet rain at high energy, hexagons imploding through builds, a blooming drop, a nebula in breakdowns and deep space at the ends). The hexagon effects (Hex pulse, Cube, Sectors, Interference) and the Hex rings and Around mappings work on any rig, seen as a hexagon about its centre; set Corners if the rig's shape guesses the hexagon's orientation wrong. Loading asks first; macro values, the rig, limiters and output stay. "Save this show as a pack" keeps yours.
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
