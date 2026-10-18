Psyight
=======

Turns music into LED pixel shows: it analyses tracks (or live audio) for kicks, beats, bars,
sections and the bassline, and drives LED strips over sACN or Art-Net.

Requirements
- Windows 10 (version 1803 or later) or Windows 11, 64-bit.
- A graphics driver with OpenGL 3.3 (any GPU from about the last ten years, Intel included).
- Nothing else to install: the Visual C++ runtime DLLs are in this folder.

Start
- Run psyight.exe. Use File > Open track (or drag a WAV, FLAC or MP3 onto the window).
- Settings are saved in the folder you start Psyight from (psyight.psyproj, imgui.ini,
  psyight.recent.json, logs\). Start it from a folder you can write to, for example this one.
- OPERATOR_GUIDE.md explains running a show: the state ladder, live input, latency, MIDI.

Optional files (in the folder you start Psyight from)
- psyight.psyproj: your rig, patch, output, scenes and MIDI bindings. Without it: a default rig.
- data\m4\student.weights.json and student.weights.bin: the trained bass and kick model, needed
  for live mode. Not included: it is trained on private music.
- Next to a track: <track>.labels.json (your labels) and stems\htdemucs\<track name>\ (Demucs
  stems, for better file-mode analysis).
- theme.json next to psyight.exe: colour overrides (see the theme section in the source).

Network
- The first time Psyight sends to the lights, Windows Firewall may ask about network access.
  Allow it on private networks.

Licences
- licenses\: the notices of the libraries built into psyight.exe.
- assets\fonts\: Inter and JetBrains Mono (SIL Open Font License 1.1), Lucide icons (ISC).
- The Visual C++ runtime DLLs are Microsoft's redistributable files.
