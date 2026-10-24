# M-Audio Oxygen Pro 61 for Melodee

Play the WaveLoop build of Melodee (branch `waveloop/main` of this fork) from an Oxygen Pro 61. Same controls as the
MiniLab preset (`MiniLab-3.md`), programmed on the keyboard itself. No computer and no Preset Editor needed.

## Connect

- **MIDI:** the Oxygen's 5-pin MIDI OUT to the FM-1's TRS MIDI IN, with the right 5-pin-to-TRS adapter.
- **Power:** the Oxygen runs on USB power only. Plug its USB into a phone charger or powered hub.
- **Send controls out of the 5-pin port:** hold SHIFT, press MODE/GLOBAL, scroll to **MIDI OUT**, press the encoder,
  pick **KEYS**, press BACK.
- **Preset mode:** press DAW/PRESET until it is dim.

The keyboard's channel picks the FM-1 track: channels 1–4 play tracks 1–4, and **channel 16 plays whichever track is
selected on the FM-1** (the WaveLoop build). Set it in GLOBAL (SHIFT + MODE/GLOBAL).

## Load the ready preset (easiest)

1. Plug the Oxygen into the computer by USB and open M-Audio's Oxygen Pro 61 Preset Editor.
2. File > Load Preset > `Melodee.OxygenPro61UserPreset` (in this folder).
3. File > Send Preset, pick a preset slot, Send.
4. On the keyboard: hold PRESET, pick MELODEE with the encoder, press it.

The file was made in the Preset Editor (bank 1) and differs from its default preset only in the settings below.

## Or program it on the keyboard

Hold SHIFT and press PRESET to edit the current preset. Touch a control to pick it, turn the encoder to move between
its settings, press the encoder to change one. Program everything below, then press PRESET and choose **Save As**
(name it MELODEE).

| Control | How to pick it | Set |
| --- | --- | --- |
| Knob 1 | turn it | Channel Global, CC 74: tone (SID, FM6, CZ-1, Prophet); DRUM: the picked drum's pitch |
| Knob 2 | turn it | Channel Global, CC 71: character (SID, Prophet); DRUM: the picked drum's tone |
| Knob 3 | turn it | Channel Global, CC 5: glide; DRUM: the picked drum's decay |
| Knob 4 | turn it | Channel Global, CC 10: pan; DRUM: the picked drum's pan |
| Knob 5 | turn it | Channel Global, CC 76: LFO rate; DRUM: the picked drum's FX send |
| Knob 6 | turn it | Channel Global, CC 77: LFO to filter (wobble); DRUM: the picked drum's level |
| Knob 7 | turn it | Channel Global, CC 78: LFO to level (tremolo) |
| Knob 8 | turn it | Channel Global, CC 70: LFO shape (SIN TRI SAW SQR S&H) |
| Faders 1–4 | move it | Channel 1, 2, 3, 4 (one each), CC 7: track levels |
| Faders 5–8 | move it | Channel Global, CC 73 / 75 / 79 / 72: attack, decay, sustain (not on Prophet, FM6, CZ-1), release |
| Fader 9 | move it | Channel Global, CC 8: what you hear: down = the computer only, middle = both at full, up = the FM-1 only (USB connected) |
| Encoder turn | SHIFT + turn the encoder | Channel Global, Message **CC RELTV**, CC 114, Type **Right 65, Left 63**: next / previous sound on the FM-1's selected track |
| Encoder press | SHIFT + press the encoder | Channel Global, Assign CC, CC 115, Press 127, Release 0: next track |
| Fader button 1 | press it | Channel Global, Mode CC, CC 112, Press 63, Release 64: previous engine |
| Fader button 2 | press it | Channel Global, Mode CC, CC 112, Press 65, Release 64: next engine |
| Pads 1–16 | press it | Channel 4, Mode NOTE, notes C1–D#2 (36–51), Latch Off: track 4 (put the drums there) |
| Mod wheel | move it | Channel Global, CC 1 |
| Pitch wheel | move it | Channel Global |

The other buttons stay as they are.

## What you see on the FM-1

A knob turn shows a card with the track, the parameter and its value for under a second. On a DRUM track
the knobs shape the drum you played last on it (a pad, a key, a note): the card names it, T4 KICK PITCH. Fader 9 shows MIX with
how loud the computer and the FM-1 each are. The line under the BPM shows
how far the sound is through its engine's sounds as the encoder walks them.

## Not yet tried

The preset file was built in the Preset Editor; the steps follow M-Audio's Oxygen Pro Series User Guide v1.1 (Preset
Edit Mode, pages 65–84). Neither has been sent to an Oxygen Pro 61 and played yet.
