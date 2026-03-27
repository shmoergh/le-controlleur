# Le Controlleur

A versatile firmware for the Shmøergh Moduleur Brain module. Current functionality includes MIDI to CV conversion, with architecture evolving toward dual-mode operation.

## Install

Flash `le-controlleur-pico.uf2` (or `le-controlleur-pico-2.uf2` for Pico 2) to your Brain module by holding the BOOTSEL button while connecting it to your computer, then copy the `.uf2` file to the mounted drive.

## Features

- **MIDI to CV/Gate conversion**: Converts MIDI note-on/note-off messages to 1V/octave pitch CV and gate signals
- **Configurable MIDI channel**: Listen to any MIDI channel (1-16) with visual feedback via LEDs
- **Dual CV outputs**: Route pitch and gate to either output channel of the Brain module
- **CC to CV mapping**: Secondary CV output can provide velocity, modwheel, unison pitch, or duophonic split
- **Panic function**: Clear stuck notes by holding both buttons for 2 seconds
- **Interactive configuration**: Use buttons and potentiometers to change settings on the fly
- **Visual feedback**: 6-LED display shows current MIDI channel or selected CV output during configuration

### Basic Operation

Once flashed, the module will:
- Listen for MIDI notes on the configured MIDI channel (default: channel 1)
- Output pitch CV (1V/octave) and gate signals on the configured CV channel (default: Channel A)
- Convert MIDI note-on messages to gate high and MIDI note-off to gate low
- Output the configured CC-to-CV signal on the secondary CV channel (velocity, modwheel, unison pitch, or duophonic split)

### Help Sheet

<img width="2840" height="4104" alt="image" src="https://github.com/user-attachments/assets/7cf20108-f6d8-4ee8-bf01-7a1d4cb93ae7" />

[Printable version (PDF)](https://github.com/user-attachments/files/25797622/midi2cv-guide.pdf)


### Walkthrough

[Watch walkthrough on YouTube](https://www.youtube.com/watch?v=gNz6zGBfJJQ)


### Configuring MIDI Channel

1. **Press and hold Button A** (left button)
2. **Turn Pot X** (left potentiometer) to select MIDI channel 1-16
   - LEDs will display the selected channel number in binary (1-16)
3. **Release Button A** to save the setting

### Configuring CV Output Channel

1. **Press and hold Button B** (right button)
2. **Turn Pot Y** (middle pot) to select the output channel:
   - Turn fully counter-clockwise: **Channel A** (LEDs 1-3 illuminate)
   - Turn fully clockwise: **Channel B** (LEDs 4-6 illuminate)
3. **Release Button B** to save the setting

### Configuring CC to CV Mode

1. **Press and hold Button B**
2. **Turn Pot Z** (rightmost pot) to select the CC-to-CV mode:
   - **Velocity**: Outputs 0-5V based on note velocity
   - **Modwheel**: Outputs 0-5V based on MIDI CC1 (modulation wheel)
   - **Unison**: Outputs the same pitch CV as the primary channel
   - **Duophonic**: Splits the two most recent notes across the two CV channels
3. **Release Button Z** to save the setting

### Panic Function

If MIDI notes get stuck (e.g., if the MIDI cable is disconnected during a note):

1. **Press and hold both Button A and Button B simultaneously for 2 seconds**
2. The note stack will be cleared and the gate will turn off

### LED Indicators

- **During MIDI channel selection**: LEDs show channel number in binary (1-16)
- **During CV channel selection**: Left 3 LEDs for Channel A, right 3 LEDs for Channel B
- **During CC-to-CV mode selection**:
  - LED 4 on: **Velocity**
  - LED 5 on: **Modwheel**
  - LED 4+5 on: **Unison**
  - LED 6 on: **Duophonic**
- **During normal operation**: LEDs turn off to conserve power

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
