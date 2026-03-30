# Le Controlleur

`Le Controlleur` is a dual-mode performance firmware for the Shmøergh Moduleur Brain module.

It combines:
- a clockable `Sequencer` engine for autonomous and generative modular playback
- a hands-on `MIDI to CV` interface for keyboard/controller-driven patches

The goal is fast, playable control with minimal hardware controls: two buttons, three pots, two CV outs, one gate out, and clear LED/console feedback.

## Install

Flash `le-controlleur-pico.uf2` (or `le-controlleur-pico-2.uf2` for Pico 2) to your Brain module by holding the BOOTSEL button while connecting it to your computer, then copy the `.uf2` file to the mounted drive.

## User Guide

There are two distinct modes in this firmware:

1. Sequencer
2. MIDI to CV converter

By default the program is in Sequencer mode. To switch between the modes, hold A + B buttons for about 1.5s. Switching modes is indicated by a quick animation on the LED strip.

The [Sequencer](./SEQUENCER.md) generates random notes and rhythms depending on where you turn Pot Z: turned fully clockwise will result in all notes being random, while turned all the way down will lock in the actual sequence. In between values will morph and change a few notes, organically creating new sequences.

The [MIDI to CV Mode converter](./MIDI2CV.md) enables you to use external MIDI gear with the Moduleur. Connect your keyboard or sequencer to the MIDI in of the Brain module with a TRS-A MIDI cable and you can use the outputs of the Brain to control gate, pitch and more on the Moduleur.


## Build

```bash
./build-firmware.sh
```

## Development

This project includes brain-sdk as a git submodule. To update the SDK:

```bash
cd brain-sdk
git pull origin main
cd ..
git add brain-sdk
git commit -m "Update brain-sdk"
```
