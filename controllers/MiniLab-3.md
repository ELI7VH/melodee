# MiniLab 3 for Melodee

Play Melodee on the FM-1 from an Arturia MiniLab 3, over USB or the TRS MIDI cable.

## Install the preset

1. In Arturia MIDI Control Center, Import `Melodee.minilab3`.
2. Store it to an unused User slot.
3. On the MiniLab, hold Shift and tap Pad 3 (Prog) to pick it.

## What the controls do

The keyboard's MIDI channel picks the track: channels 1–4 play tracks 1–4. The knobs follow that channel, so they
change the track you are playing.

| MiniLab | MIDI | Melodee |
| --- | --- | --- |
| Top knob 1 | CC74, keyboard channel | Tone (brightness). SID, FM6, CZ-1, Prophet and DRUM |
| Top knob 2 | CC71, keyboard channel | Character (resonance). SID and Prophet |
| Top knob 3 | CC5, keyboard channel | Glide |
| Top knob 4 | CC10, keyboard channel | Pan |
| Bottom knob 5 | CC73, keyboard channel | Attack |
| Bottom knob 6 | CC75, keyboard channel | Decay |
| Bottom knob 7 | CC72, keyboard channel | Release |
| Bottom knob 8 | CC1, keyboard channel | Mod (vibrato), same as the mod strip |
| Faders 1–4 | CC7, channels 1–4 | Track 1–4 level, whatever channel the keys are on |
| Pads, both banks | Notes 36–51, channel 10 | The first DRUM track (GLO > SYSTEM > DRUM, channel 10 by default) |
| Mod strip | CC1, keyboard channel | Mod (vibrato) |
| Pitch strip | Pitch bend, keyboard channel | Pitch bend |
| Main encoder, turn | CC114, relative | Next / previous sound on the track selected on the FM-1. Past an engine's last sound it goes on into the next engine's, as PRESETS does |
| Shift + main encoder | CC112, relative | Jump to the next / previous engine's first sound |
| Main encoder, press | CC115 | Select the next track on the FM-1 (after the last: track 1). The encoder then browses that track |

Engines that do not list a knob ignore it.

Every knob turn shows a card on the FM-1 for about a second: the track, the parameter and its value, with a bar.
The line under the BPM and engine name at the top of the screen shows how far the selected sound is through its
engine's factory sounds.

The encoder rows need the WaveLoop build of Melodee (branch `waveloop/main` of this fork); stock Melodee ignores CC112–115.

Melodee 1.2.1 has no effects, so there is nothing for CC91 / CC93 to do.

## Tested

Encoder turn and Shift + turn: sent from a computer over USB to an FM-1 running this build, sounds and engines read
back. Encoder press: not yet (the project had one track). The preset itself has not been loaded onto a MiniLab yet.
