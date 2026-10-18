# Psyight operator guide

For whoever runs the lights: at home or at an event, from prepared files or from a live DJ set. It assumes the rig and node are built and working (see `firmware/node-f767/README.md` for the node).

## Start

Run Psyight from the folder that holds `psyight.psyproj` (the project file: rig, patch, output, limiters, scenes, palettes, MIDI bindings). Settings save by themselves, a minute after any change and on exit.

The top bar has three parts:

- **The state ladder:** Preflight, Preview, Ambient, React, Show. Only Ambient, React and Show send to the lights.
- **The depth:** Operate, Tune or Engineer. It sets how many controls you see. Operate is enough during a show.
- **The safety controls**, always at the right:
  - Blackout (B): black at once, and the way back up still counts against the flash limit.
  - Hold (H): freezes the current look.

The app always starts in Preview, so nothing reaches the lights until you step up the ladder.

The window: the transport (play, loop, half speed, time, volume, the track's waveform) is at the top left, above the analysis and the bass roll. The rig preview is pinned at the top right; its button folds it away and the button at the far right of the top bar (or View > Rig preview) brings it back. Below it are the note inspector and scoring, then the Show tab (live input, MIDI, scenes, limiters, output) with the log, rig and preflight. View > Reset layout puts everything back.

**Show mode:** React and Show switch the app to the dark Show theme and hide the editing panels; the rig is then drawn large where the bass roll was. Leaving them restores the theme and panels. File > Settings turns either change off.

## Setting up the rig

Rig tab (Engineer depth) > Templates builds a rig from a few numbers: **Pillars** (how many, height, spacing or a circle, density, serpentine wiring) or a **Matrix panel** (rows, width, row spacing, density, zigzag wiring). The line under them shows strips, pixels, universes and the current at full white. "Replace the rig..." swaps the strips after a confirmation; scenes, palettes and settings stay. The strips get group tags (left and right, top and bottom, even and odd) that layers can be limited to. Each strip can still be edited by hand below.

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
- **Nodes:** press "Discover Art-Net nodes" once on the show network; your node should be listed.

"Copy report" puts the whole list on the clipboard, which helps when asking for help.

## Two ways to run

**From files** (prepared tracks, best accuracy): drop a WAV, FLAC or MP3 onto the window and press Space to play. The whole track is analysed first, from its Demucs stems when they exist, so events are known ahead and nothing lags.

**Live** (a DJ set, unknown music): in the Show tab, open "Live input", choose a source and press Start.

- **Loopback:** whatever this computer plays (DJ software on the same laptop).
- **An input device:** the mixer's booth output into an audio interface. A virtual cable (for example "Line 1 (Virtual Audio Cable)") also works: route the DJ software to it.
- **The loaded track, as if live (silent):** plays nothing. Use it to rehearse live mode, or to check the show without sound.

Live mode needs the trained model in `data/m4/` and pauses the file. Its status line reads:

- "Listening for a steady kick": no tempo yet.
- "Acquiring": the tempo is measured and the beat loop is settling.
- "Locked": the beat is predicted. Normally within a few seconds of the first kicks.

The second line shows the analysis delay (about 15 ms), the load, the input level and any dropped samples. Dropped samples mean the computer is overloaded: close other programs.

## Latency: make the lights land on the beat

The lights should flash when the audience hears the kick, not when the computer does. Set the latency offset (Show tab, in ms) one of three ways:

- **By distance:** about 2.9 ms for every metre from the speakers to the audience.
- **By the tap test** (live mode, once locked): stand where the audience stands, and tap "Tap (T)", or the T key with the Preview focused, on the kick at least 8 times. "Use as latency offset" applies the result. The network and node add a little more, so finish by eye.
- **By eye:** adjust until the kick flash and the kick sound feel together.

## During the set

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

Your presets are JSON files in the `presets` folder beside the project (`scenes`, `palettes`, `shows`), easy to copy to another machine.

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
