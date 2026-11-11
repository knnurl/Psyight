# Studio checklist: DJM-A9 and CDJs

Things to check next time the laptop is with the mixer and the players. Written 2026-11-11. Each item says what to do and what it settles. Tick them off here, and put results in `docs/PROGRESS.txt`.

Scripts used below: `tools/studio/meter.ps1` (reads an audio input's level for a few seconds) and `tools/studio/midilisten.ps1` (lists MIDI messages received). Both only receive: nothing is played or sent. Run them from PowerShell:
`powershell -ExecutionPolicy Bypass -File tools\studio\meter.ps1 "DJM-A9" 3`

## Before starting

- [ ] Laptop on mains power (not battery).
- [ ] DJM-A9 on its own USB port, no hub.
- [ ] rekordbox and AnyDesk quit, including from the system tray: they hold the Pro DJ Link ports.
- [ ] If Windows asks whether Python or Psyight may use the network: allow (the network is often "Public" here).

## DJM-A9: audio into Psyight

Known so far (2026-11-10): with AlphaTheta's DJM-A9 driver 1.100.2.0 the mixer is the Windows input "Microphone (2- DJM-A9)", stereo, 48 kHz. USB1/2 carries the master (MIX, REC OUT) and is what Windows records. The level does not follow the master knob.

- [ ] **USB1/2 without the microphone.** In the DJM-A9 Setting Utility, MIXER OUTPUT, open the USB1/2 dropdown and pick a MIX option without MIC if there is one. Otherwise keep the mic down during shows. *Settles: speech no longer reaches the detectors.*
- [ ] **USB Output Level back to 0 dB.** At 0 dB the master peaked around -10 dBFS, which is enough. On 2026-11-10, minutes 31 to 39 of the recording reached full scale. *Settles: no clipping.*
- [ ] **The audio dropouts.** On 2026-11-10, 54 minutes into a recording, the input began losing about one 1 ms USB packet in ten until the end, heard as a break every second. Record 30 minutes or more with Psyight (Show > Live input, Record ticked) and keep notes of anything opened or changed on the laptop (especially rekordbox or the setting utility's ASIO tab). Afterwards, compare the recording's length with the time it ran (its `.clock.csv`). *Settles: whether an ASIO program, the USB port or the laptop caused it.*
  - [ ] If it happens again: note the time, then try another USB port, then the laptop's power plan.
- [ ] **Psyight live mode on the mixer.** Show > Live input, "Microphone (2- DJM-A9)", start. *Settles: the level meter moves, and the show follows the decks by ear.*

## DJM-A9: MIDI (faders tell which deck the room hears)

Not checked yet. The mixer's MIDI shows in Windows as "DJM-A9" (through Windows MIDI Services).

- [ ] Switch MIDI output on at the mixer (its MIDI on/off button).
- [ ] Run `powershell -ExecutionPolicy Bypass -File tools\studio\midilisten.ps1 "DJM-A9" 30` and, during those 30 s, move in this order: channel faders 1 to 4, the crossfader end to end, each channel's trim, the master knob.
- [ ] Note for each control the MIDI channel, the control number and the value range. *Settles: the fader map follow mode needs to pick the audible deck.*
- [ ] If the script lists no MIDI inputs: check whether Psyight is open (it may hold the port), and the mixer's MIDI setting.

## CDJs: what the link sends to a listener

Not checked yet. Psyight's follow-mode code reads these packets already, but the byte offsets are from documentation and marked [Verify] until a capture confirms them.

- [ ] Note the players' models and firmware, and the mixer's link settings.
- [ ] Laptop on the same network switch as the players (and the mixer, if it has a LAN cable).
- [ ] **Pro DJ Link capture, 5 minutes.** `python tools/python/djlink_log.py`, and at the same time Psyight recording the mixer (live input, Record ticked). During those minutes:
  - [ ] load and play a track on one deck; load another on a second deck;
  - [ ] change the pitch on one deck;
  - [ ] pause, cue back, play again;
  - [ ] blend from one deck to the other with the channel faders, then with the crossfader;
  - [ ] write down which track was on which deck, and roughly when.
- [ ] Check the logger's console during the capture: each player listed with a name and number, beats arriving, and "position (CDJ-3000)" packets arriving. *Settles: whether a passive listener gets absolute positions (the basis of follow mode).*
- [ ] Check whether "on air" packets arrive from the mixer. *Settles: whether the link can tell which deck is audible (else MIDI faders).*
- [ ] Keep the log (`data/djlink/`) and the recording (`data/recordings/`) together: they share one clock.

## CDJs through PRO DJ LINK Bridge (TCNet)

- [ ] Start the Bridge (rekordbox closed), then `python tools/python/djlink_log.py --tcnet` while playing a track on two decks.
- [ ] Check that time packets arrive without opting in, which layer is which player, and whether the playheads advance and stop with play and pause. *Settles: whether the Bridge is a usable source.*
- [ ] Note whether anything carries track names (metadata).

## Tracks

- [ ] Play tracks whose files are also on this laptop (in the Psyight media library), and list which ones. *Settles: matching each deck to its file by length and BPM, and the audio delay against the file.*
- [ ] Note one track the laptop does not have. *Settles: what follow mode does when it cannot match.*

## Afterwards (Claude)

- Check the decoders' offsets against the capture and remove the [Verify] marks that hold.
- Measure the delay between a deck's reported position and the music heard (kick alignment against the recording).
- Map the MIDI faders, and decide how the lead deck is chosen from them.
