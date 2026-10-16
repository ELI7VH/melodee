# M-Audio Oxygen Pro 61 for Melodee

Play the WaveLoop build of Melodee (branch `waveloop/main` of this fork) from an Oxygen Pro 61. Same controls as the
MiniLab preset (`MiniLab-3.md`), programmed on the keyboard itself. No computer and no Preset Editor needed.

## Connect

- **MIDI:** the Oxygen's 5-pin MIDI OUT to the FM-1's TRS MIDI IN, with the right 5-pin-to-TRS adapter.
- **Power:** the Oxygen runs on USB power only. Plug its USB into a phone charger or powered hub.
- **Send controls out of the 5-pin port:** hold SHIFT, press MODE/GLOBAL, scroll to **MIDI OUT**, press the encoder,
  pick **KEYS**, press BACK.
- **Preset mode:** press DAW/PRESET until it is dim.

The keyboard's channel picks the FM-1 track: channels 1–4 play tracks 1–4. Set it in GLOBAL (SHIFT + MODE/GLOBAL).

## Program it

Hold SHIFT and press PRESET to edit the current preset. Touch a control to pick it, turn the encoder to move between
its settings, press the encoder to change one. Program everything below, then press PRESET and choose **Save As**
(name it MELODEE).

| Control | How to pick it | Set |
| --- | --- | --- |
| Knob 1 | turn it | Channel Global, CC 74: tone (SID, FM6, CZ-1, Prophet, DRUM) |
| Knob 2 | turn it | Channel Global, CC 71: character (SID, Prophet) |
| Knob 3 | turn it | Channel Global, CC 5: glide |
| Knob 4 | turn it | Channel Global, CC 10: pan |
| Knob 5 | turn it | Channel Global, CC 73: attack |
| Knob 6 | turn it | Channel Global, CC 75: decay |
| Knob 7 | turn it | Channel Global, CC 72: release |
| Knob 8 | turn it | Channel Global, CC 1: mod (vibrato) |
| Faders 1–4 | move it | Channel 1, 2, 3, 4 (one each), CC 7: track levels |
| Encoder turn | SHIFT + turn the encoder | Channel Global, Message **CC RELTV**, CC 114, Type **Right 65, Left 63**: next / previous sound on the FM-1's selected track |
| Encoder press | SHIFT + press the encoder | Channel Global, Assign CC, CC 115, Press 127, Release 0: next track |
| Fader button 1 | press it | Channel Global, Mode CC, CC 112, Press 63, Release 64: previous engine |
| Fader button 2 | press it | Channel Global, Mode CC, CC 112, Press 65, Release 64: next engine |
| Pads 1–16 | press it | Channel 10, Mode NOTE, notes C1–D#2 (36–51), Latch Off: the DRUM track |
| Mod wheel | move it | Channel Global, CC 1 |
| Pitch wheel | move it | Channel Global |

Faders 5–9 and the other buttons stay as they are; Melodee ignores what they send.

## What you see on the FM-1

A knob turn shows a card with the track, the parameter and its value for under a second. The line under the BPM shows
how far the sound is through its engine's sounds as the encoder walks them.

## Not yet tried

Written from M-Audio's Oxygen Pro Series User Guide v1.1 (Preset Edit Mode, pages 65–84) and the firmware's MIDI code.
It has not been programmed into an Oxygen Pro 61 and played.
