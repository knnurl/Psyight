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

## Before the show: Preflight

Open the Preflight tab. Every line should be green; read every amber line:

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
- **React:** the scene stays as you choose it from the scene buttons; the effects still follow the music.
- **Ambient:** the calm ambient scene only. Use it between sets, during talks, or whenever the analysis looks wrong.
- **Blackout and Hold** work in every state.

Watch the live status: if "Locked" turns to "Acquiring" (a new track at another tempo), the beat keeps running and settles within a few kicks. Long breakdowns are fine: the beat keeps time until the kick returns. If the lights clearly fight the music for more than a few bars, go to React or Ambient.

The bar position (which beat is "one") comes from kicks returning after a break. Until the first break of a set it can be a beat out. Effects that change on the downbeat may look off by a beat until then.

Live controls (Live input section, once locked):

- **Downbeat now (D):** press on the "one" you hear. The nearest beat becomes the downbeat, so pressing a little late is fine.
- **-10 ms and +10 ms:** nudge the predicted beat. The loop then pulls it back towards the kicks it hears.
- **Relock:** measure the tempo again from the next kicks, for example after a tempo change the loop has not caught.

## MIDI controller

In the Show tab, open "MIDI" and choose the controller. For each control you want, press Learn, then move the knob or press the pad. You can bind:

- blackout, hold, automation (back to Show) and each scene, to buttons or pads
- the brightness cap and the latency offset, to knobs or faders
- the tap test, and "downbeat now" (a pad for the "one")

Bindings are saved with the project, and the controller reconnects on the next start.

## If something goes wrong

- **No lights:** check that the ladder is on Ambient, React or Show, then the Preflight tab (Network and Nodes lines). The node's console shows whether packets arrive.
- **The app crashed or the power went:** the lights do not go black at once. After 2 s without data the node runs its own loss policy (by default a dim violet fallback show). Restart Psyight: it reports the unclean exit in the Log tab. The labels and project are at most a minute old. The crash details are in `logs/` (`psyight.log` and any `crash-*.dmp`).
- **Live mode never locks:** check the input level on the status line. Loopback captures only what plays through the default output device. With an interface, check the input gain.
- **Too bright, or flashing too much:** the flash limiter caps flashes at 3 a second (it can only be turned off in Engineer depth). Lower the brightness cap in the Limiters section, or bind it to a fader.

## Recording a show

Record (top bar) writes the session to `recordings/<date and time>/`: frames, parameter changes and events. `psyight_replay` replays it to check that the show reproduces exactly.
