# Task: Restyle Psyight UI (visual and layout only)

## Ground rules
- Do NOT change behaviour: audio playback, analysis, detection, scoring,
  LED/DMX output timing, keyboard shortcuts, file formats (*.labels.json),
  or mode semantics.
- Work in five phases (0 to 4). After each phase: build, run, commit, then
  STOP and report. Do not start the next phase until I approve.
- After every meaningful change, capture the UI with the --capture mode
  (Phase 0) and inspect it yourself. Fix obvious issues (clipped text,
  overlaps, unreadable contrast, misaligned elements) before reporting.
- Visual taste is my call. Flag anything you are unsure looks right instead
  of deciding for me.
- All colours and style values live in one theme module. No hard-coded
  colours anywhere else in the codebase after Phase 1.
- If a choice would affect behaviour, ask instead of guessing.
- Report frame time before and after each phase. Any regression over 0.5 ms
  must be flagged.
- Re-read this file at the start of each phase.

## Phase 0: Recon and deterministic capture
Recon (no styling changes). Report back:
- Language, Dear ImGui version, docking branch or not, ImPlot present or not,
  rendering backend.
- Where style and fonts are currently set up.
- How the spectrogram colourmap is generated.
- Where the bass roll, waveform and band energy panels are drawn.
- How modes (Preflight, Preview, Ambient, React, Show) are switched.
- Whether imgui_freetype is available.
- If the stack differs from the assumptions in this file (C++, Dear ImGui,
  ImPlot), say so and propose how to adapt before Phase 1.

Capture mode:
- Add a CLI mode: --capture <out.png> --theme <desk|show>
  --audio <file> --time <seconds> --frames 60
  It loads the file, seeks to the time, renders the given number of frames,
  saves the framebuffer to PNG and exits.
- Render at a fixed window size of 1920x1080 and a DPI scale of 1.0, so
  captures are comparable.
- The --theme flag is a no-op until Phase 1 exists; accept it anyway.
- Capture a baseline now. Every later capture uses identical arguments
  (same audio file, same time position).

## Phase 1: Fonts and theme
Fonts (add to /assets/fonts with their OFL licence files):
- Inter Regular and Medium for UI text, base 16px, scaled by DPI.
- JetBrains Mono for numeric readouts: timecode, BPM, Hz, dB, ms, counts.
- Merge an icon font (Lucide or Material Symbols) into the UI font with
  correct glyph ranges.
- If imgui_freetype is available, enable it with light hinting.

Theme:
- Create ApplyPsyightTheme(ThemeMode mode) with modes Desk and Show.
- Define colour tokens in one struct and derive every ImGuiCol from them.
- Desk tokens: bg #1E222A, panel #282C34, frame #31363F,
  frame_hover #3A404B, text #D7DAE0, text_dim #8B919C, accent #F2A33A.
- Show tokens: bg #121316, panel #1A1C20, frame #24272C,
  frame_hover #2C3036, text #B4B8C0, text_dim #6E737C, accent #C98A33.
- The accent colour is used ONLY for: active mode, playhead, selection,
  primary buttons, active toggles. Headers, tabs, checkboxes and ordinary
  buttons stay neutral.
- Optional overrides from theme.json next to the executable; fall back to
  built-in defaults if missing or invalid.
- Style: WindowPadding 10x10, FramePadding 8x5, ItemSpacing 8x6,
  FrameRounding 4, GrabRounding 4, TabRounding 4, WindowRounding 6,
  ChildRounding 6, FrameBorderSize 0, WindowBorderSize 0, ScrollbarSize 10.
- Section containers: child windows with ChildRounding 6, 8 to 12px inner
  padding, separated by background contrast. No borders by default; add a
  theme flag to enable 1px borders.
- ImPlot (if present): match the ImGui theme, grid lines at about 10% alpha,
  smaller axis label font.
- Add a dev-only toggle (F12) that opens ImGui::ShowStyleEditor().

## Phase 2: Data panels
- Spectrogram: default colourmap magma, precomputed LUT. Keep viridis and
  inferno selectable.
- Bass roll:
  - Background slightly darker than panel colour.
  - Lane stripes: black-key rows darker than white-key rows.
  - C rows marked with a slightly brighter line.
  - Beat grid lines at low alpha, bar lines stronger.
  - User notes filled with the accent colour.
  - Detected notes in neutral colour at about 40% alpha.
  - Selected notes get a bright outline.
  - Playhead: accent colour, 2px.
- Waveform: neutral light colour; played portion slightly brighter than
  unplayed.
- Band energy: keep the colours distinct, but desaturate them to sit on the
  neutral background.

## Phase 3: Controls
- Add helpers: PrimaryButton, SecondaryButton, ToggleButton, HelpMarker.
- Primary (filled accent): Play, Save. Everything else is secondary
  (neutral). Active toggles use ToggleButton.
- Re-analyse: check whether it can overwrite or invalidate labels. If yes,
  add a confirm modal. Report what you found either way.
- Replace number fields with -/+ buttons by DragFloat or DragInt with a unit
  suffix (for example "%.1f dB"). Keep the existing ranges, clamps and
  double-click-to-type behaviour.
- Move long instructional text (bass roll hints, selection help) into
  HelpMarker tooltips. Show keyboard shortcuts in both tooltips and menu
  items.
- Use the mono font for every numeric readout.

## Phase 4: Layout (on a separate git branch)
- Move to the Dear ImGui docking branch if not already on it.
- Build a default layout with DockBuilder:
  - Transport header bar across the top.
  - Analysis and bass roll in the centre.
  - Right column as TWO stacked dock nodes: Note inspector/Scoring on top,
    Log/Preview/Rig/Preflight below. Both must stay visible at once.
- Persist the layout and add a "Reset layout" command.
- Transport header: icon buttons for play, loop, record, half speed; large
  mono timecode; volume as a DragFloat.
- Replace the mode radio buttons with a segmented control.
- Entering Show mode applies the Show theme and hides the editing panels
  (Analysis, Bass roll, Note inspector), configurable in settings. Leaving
  Show mode restores the previous layout and theme.
- The capture mode must still work with the docked layout (use the default
  layout for captures, not the persisted one).

## Report format after each phase
- Summary of changes.
- Files changed.
- Frame time before and after.
- Before and after captures, Desk and Show.
- Issues you spotted in the captures and fixed.
- Anything you are unsure looks right.
- Open questions.

## Out of scope for the agent
- Final judgement of the Show theme. I will test it at low brightness in a
  dark room; captures viewed on a normal monitor are not enough.
