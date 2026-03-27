# Le Controlleur

`Le Controlleur` is a dual-mode performance firmware for the Shmøergh Moduleur Brain module.

It combines:
- a clockable `Sequencer` engine for autonomous and generative modular playback
- a hands-on `MIDI to CV` interface for keyboard/controller-driven patches

The goal is fast, playable control with minimal hardware controls: two buttons, three pots, two CV outs, one gate out, and clear LED/console feedback.

## Install

Flash `le-controlleur-pico.uf2` (or `le-controlleur-pico-2.uf2` for Pico 2) to your Brain module by holding the BOOTSEL button while connecting it to your computer, then copy the `.uf2` file to the mounted drive.

## User Guide

- [Sequencer Mode](./SEQUENCER.md)
- [MIDI to CV Mode](./MIDI2CV.md)

### Switching Main Modes

To switch between `Sequencer` and `MIDI to CV`, press and hold `Button A + Button B` together for about `1.5 seconds`.

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
