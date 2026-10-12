# Melodee Synth

A synth-only version of Melodee for the M-VAVE FM-1, developed on the `synth-only` branch.
Melodee is based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
(@kurogedelic), Hügelton Instruments. This branch is a separate development version.

## Instruments and effects

One playable stereo track with **PROPHET, FM6, CZ-1 and SID**. The native sound editors,
factory libraries, modulation matrix, voice modes, envelopes, LFOs, glide and tuning stay.
Factory and user sounds retain their existing engine numbers and preset namespaces.

The effects from the earlier Melodee versions are restored: drive, chorus, digital/tape
and ping-pong delay, ROOM/SPRING/HALL reverb, SLICER and the held FX layer (repeat, reverse,
tape stop, freeze, filters, octave shifting, flanger and phaser). Their stereo loop buffer
keeps its full 32 KiB capacity, independent of the number of tracks.

ARP, ARP HOLD, one-finger chords, the scale picker, MPC key mapping and microtonal scales stay.
The step sequencer, song arrangement, patterns, motion recording, note editing, drum engine,
project saving and NEW SONG workflow are removed.

## Playing and editing

- **HOME** opens Stage: one sound card, scale/chord/ARP status, level and effect sends,
  with the live waveform and played notes below. KNOB 1–4 edit the engine's main controls.
- **PRESETS** browses factory sounds, native user sounds, general user slots, favourites,
  recent sounds and categories. **SELECT** moves through the pages of the open section.
- **EDIT, ENV, LFO, SCL, ARP, FX and GLO** open the sound, playing and effect pages.
  EDIT, SCL and FX retain their held shortcuts. GLO opens its pages directly.
- **PLAY** toggles ARP HOLD. Its light shows the latch. With ARP off it sets the latch for
  when ARP is enabled; it does not start a sequencer. **REC**, **SEQ** and the **ALGORITHM**
  encoder have no action in this version.
- **MASTER** sets the output level. On Stage, **OCT−/OCT+** shift the keyboard octave;
  elsewhere they go back or confirm. **SAVE** opens the sound library; holding it undoes
  sound loads. User sound naming, import/export and bulk library actions remain available.
- **BPM**, clock source, tuning and concert pitch are on **GLO > GLOBAL**. USB or TRS Start,
  Continue and Stop can synchronize the ARP and effects. Clock pulses alone do not take over.
- All MIDI channels play the single synth. Pitch bend, sustain, expression, modulation,
  aftertouch, controller reset and panic handling stay. The panel keys send on channel 1.

## USB audio

**Melodee In** records a stereo pair at **44.1 kHz**, with 16-bit and 24-bit PCM formats.
It captures the synth after level, pan, all effects, MASTER, output filtering and limiting.

**Melodee Out** plays the computer through the FM-1. Its audio is mixed after the capture
point, so monitoring a recording through Melodee Out cannot feed it back into Melodee In.
The HOME-held menu can disable either stream independently.

## Sounds and startup state

The native user pools remain: 64 FM6 slots, 128 CZ-1 slots and 128 Prophet slots, plus
64 general sound slots. DX7, CZ-1 and Prophet SysEx imports and full native patch data stay.

At startup the synth restores its last sound, native patches, sound parameters, effects,
BPM, scale, ARP/chord settings and octave. It saves changes after three seconds of inactivity
once notes and latched ARP input have stopped. Unchanged state causes no flash write;
failed writes retry at a later idle interval. A failed early boot skips the last state.

Last state uses its own A/B flash sectors at `0x9D000` and `0x9E000`. Existing user libraries,
device settings, main-firmware project slots and the main-firmware template keep their
original storage locations. Settings updates preserve the template bytes.

The local web editor recognizes this version: one sound, native editors and libraries,
with no Sequencer, Tracks or Projects tabs. **Settings** contains full backup/restore,
including the current synth state and the saved projects belonging to the main firmware.
A synth archive containing its last state must be restored with synth firmware.

## Build and validation

See [BUILDING.md](BUILDING.md) for the toolchain and SDK setup. In this workspace:

```sh
PYTHON=build/venv/bin/python ./build.sh
PATH="$PWD/build/venv/bin:$PATH" tests/run_tests.sh
```

The installable development package is **`build/melodee-synth.fwsc`**, identified on the
device and in the editor as **synth-dev**. `MELODEE_DUAL_CORE=0` builds a single-core version;
`MELODEE_RAM_AUDIO=1` enables the resident-audio profile. Development packages retain
identity `FM-1_900`. This branch has not been published as a release.

The branch's host suite covers single-track playing and controls, native and general
user slots, last-state recovery, flash write failures, preservation of foreign storage,
stereo USB PCM/clock drift/endpoint recovery, all visible UI pages and palettes, synth DSP,
FX signal/stability/cost checks, dual-core sample equality and target DSP budgets.
The sequencer/drum/project-runtime test fixtures from the main branch are not applicable.
Host checks and builds do not establish physical audio, USB enumeration or panel operation;
those require testing on an FM-1.

## License

GPL-3.0-only. See [LICENSE](LICENSE), [LICENSING.md](LICENSING.md) and [LICENSES](LICENSES)
for the original authors, third-party components and asset licences.
