# M-Audio Oxygen Pro 61 for Melodee

Same controls as the MiniLab preset (`MiniLab-3.md`), set up by hand in M-Audio's Oxygen Pro Preset Editor
(install it from M-Audio Software Manager with the advanced download option). There is no ready-made preset file
yet: the editor's file format is not documented.

The keyboard's MIDI channel picks the track: channels 1–4 play tracks 1–4. Set the knobs to the keyboard's
channel so they change the track you play.

| Oxygen Pro 61 | Send | Melodee |
| --- | --- | --- |
| Knob 1 | CC74, keyboard channel | Tone (brightness): SID, FM6, CZ-1, Prophet, DRUM |
| Knob 2 | CC71, keyboard channel | Character (resonance): SID and Prophet |
| Knob 3 | CC5, keyboard channel | Glide |
| Knob 4 | CC10, keyboard channel | Pan |
| Knob 5 | CC73, keyboard channel | Attack |
| Knob 6 | CC75, keyboard channel | Decay |
| Knob 7 | CC72, keyboard channel | Release |
| Knob 8 | CC1, keyboard channel | Mod (vibrato) |
| Faders 1–4 | CC7, channels 1–4 | Track 1–4 level |
| Faders 5–9 | — | Unused |
| Pads 1–16 | Notes 36–51, channel 10 | The first DRUM track |
| A button, momentary | CC114, on 65, off 64 | Next sound on the FM-1's selected track (WaveLoop build) |
| A button, momentary | CC114, on 63, off 64 | Previous sound (WaveLoop build) |
| A button, momentary | CC112, on 65, off 64 | Next engine (WaveLoop build) |
| A button, momentary | CC112, on 63, off 64 | Previous engine (WaveLoop build) |
| A button, momentary | CC115, on 127, off 0 | Next track (WaveLoop build) |
| Mod wheel | CC1, keyboard channel | Mod (vibrato) |
| Pitch wheel | Pitch bend | Pitch bend |

A button sends its "on" value when pressed; 64 means "no move", so each press is one step.

## Not yet tried

Nothing here has been programmed into an Oxygen Pro 61 and played. Send the editor's saved preset file and it can
be made into a ready-to-load preset.
