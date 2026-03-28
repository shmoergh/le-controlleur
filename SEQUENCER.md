# Sequencer Mode

Sequencer mode turns **Le Controlleur** into a self-contained dual-pitch probabilistic sequencer with a single shared gate output. It is best for generative lines, evolving motifs, and hands-on modulation using the three pots plus shift layer.

Use this mode when you want the Brain module to drive your patch rhythmically on its own, or when you want to lock it to external clock while still shaping scale, swing, randomness, and transpose in real time.

## Overview

`Sequencer` is a 16th-note step sequencer with:

- one shared `Gate Out`
- two CV pitch lanes (`CV A` and `CV B`)
- internal and external sync
- live transpose editing

Default startup values:

- Tempo: `120 BPM`
- Sequence length: `8`
- Range: `3 octaves`
- Quantization: `Minor`
- Swing: `0%`
- Randomness: `0`
- Transpose: `+2 octaves`, `+C`

## Transport and Mode Controls

- `Button A` press: Play/Pause
- Hold `A+B` > ~1.5 s: switch app mode (Sequencer <-> MIDI2CV)
- Hold `Button B`: Shift layer (alternate pot mappings)
- `Button B` double-tap + hold: Transpose Edit mode

## Pot Mappings

## Normal layer (no shift)

- `Pot 1`: Tempo
  - `60..240 BPM`
  - Fully minimum enables external sync (`EXT`)
- `Pot 2`: Octave range (`0..6`)
- `Pot 3`: Randomness (`0..1`)

## Shift layer (hold Button B)

- `Pot 1`: Swing (`0..50%`)
- `Pot 2`: Quantization mode
  - `Unquantized`
  - `Chromatic`
  - `Major`
  - `Minor`
  - `Pentatonic`
  - `Extra`
- `Pot 3`: Sequence length (`2..32`) with soft snapping to:
  - `2, 4, 8, 16, 32`

## Playback Model

- Step resolution is fixed to 16th notes.
- Gate pulse width is fixed (`20 ms`).
- `CV A` and `CV B` run from separate pitch lanes.
- Gate is driven from lane A step gates (single hardware gate output).
- If a step gate is low, outputs hold previous voltages (no new trigger).

## External Sync

External sync is enabled when `Pot 1` is fully minimum.

Sources:

- `Pulse In` (priority source)
- `MIDI Clock` (fallback when pulse is inactive)

MIDI clock behavior:

- `0xF8` clock: `24 PPQN`, sequencer advances every `6` clocks (16th notes)
- `0xFA` Start: reset to step 0 and run
- `0xFB` Continue: resume
- `0xFC` Stop: stop and gate low

Notes:

- If paused locally, incoming MIDI clock does not queue bursts.
- Swing is applied in both internal and external sync paths.

## Transpose Edit (Button B Double-Tap + Hold)

While held:

- `Pot 2`: octave transpose (`+0..+5`)
- `Pot 3`: note transpose (`C..B`, 12 semitone bins)
- MIDI Note On (on selected MIDI channel) can also set note transpose

On release:

- transpose remains active for the current runtime

Important behavior:

- Quantization is applied first in `C`
- then transpose is added (`octave + semitone`)

## LED Feedback

- Normal sequencer view: 6-LED gate history FIFO.
- Pot movement overlay (temporary): shows value of moved parameter.
- Sequence length (shift + Pot 3): custom full/dim visualization for power-of-two targets.
- Transpose note overlay: naturals full brightness, sharps dim.
- Octave transpose overlay: bar graph (`1..6 LEDs`).
- Button A LED blinks on quarter notes while running.

## Console Status

Live status prints include:

- mode
- tempo or external sync source
- swing
- randomness
- sequence length
- range
- quantization
- transpose
- transport state in MIDI clock mode
- raw/quantized voltage telemetry

## Persistence

- Last app mode is persisted.
- MIDI channel (used for transpose-note MIDI filtering) is shared with MIDI2CV setting.
- Note transpose semitone always boots at `C` (no semitone transpose).

## Technical Notes

- Hardware output coupling is fixed to `DC`.
- “AC coupling” behavior (if introduced in UX later) is software-level voltage offset logic, not physical relay/switching.
