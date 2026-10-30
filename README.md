# led-matrix-audio-visualizer

A 64-band FFT spectrum visualizer that runs on an ELEGOO ESP-WROOM-32 board, draws on a Waveshare 64x32 RGB (HUB75) LED matrix and listens through a SparkFun Sound Detector.

## Build environment

```sh
nix develop          # or `direnv allow` to load it automatically
make setup           # first time only: installs the ESP32 core and libraries into .arduino/
make upload          # compile and flash to /dev/ttyUSB0 (override with PORT=/dev/ttyUSBx)
make monitor         # serial console at 115200 baud
```

The ESP32 toolchains that arduino-cli downloads are prebuilt binaries for a normal Linux layout, so the flake wraps `arduino-cli` in an FHS environment. Without it they won't run on NixOS.

## Wiring

The pin map targets the **ELEGOO ESP-WROOM-32** dev board, which has the 38-pin ESP32-DevKitC layout. Each group of wires goes to a run of neighbouring header pins. The boot-strapping pins (0, 2, 12, 15) are left free so the board always boots and flashes cleanly.

```
              antenna
          ┌─────────────┐
  3V3   ● │ ◄ SD VCC    │ ●  GND   ◄ SD GND
  EN    ● │             │ ●  23    ◄ A
  VP 36 ● │ ◄ SD AUDIO  │ ●  22    ◄ B
  VN 39 ● │ ◄ SD ENVEL. │ ●  TX
  34    ● │ ◄ SD GATE   │ ●  RX
  35    ● │             │ ●  21    ◄ C
  32    ● │ ◄ R1        │ ●  GND   ◄ matrix GND
  33    ● │ ◄ G1        │ ●  19    ◄ D
  25    ● │ ◄ B1        │ ●  18    ◄ CLK
  26    ● │ ◄ R2        │ ●  5     ◄ LAT
  27    ● │ ◄ G2        │ ●  17    ◄ OE
  14    ● │ ◄ B2        │ ●  16
  12    ● │             │ ●  4
  GND   ● │             │ ●  0
  13    ● │             │ ●  2
  SD2   ● │             │ ●  15
  SD3   ● │             │ ●  SD1
  CMD   ● │             │ ●  SD0
  5V    ● │             │ ●  CLK
          └────[USB]────┘
```

| HUB75 (2×8 ribbon) | ESP32 | | HUB75 | ESP32 |
|---|---|---|---|---|
| R1 | 32 | | G1 | 33 |
| B1 | 25 | | GND | GND (right header, below 21) |
| R2 | 26 | | G2 | 27 |
| B2 | 14 | | E | — (not used at 32 rows) |
| A | 23 | | B | 22 |
| C | 21 | | D | 19 |
| CLK | 18 | | LAT | 5 |
| OE | 17 | | GND | — |

The colour lines run down the left header and the address/control lines down the right header, in the same order as the HUB75 connector.

Power the matrix from its own 5 V supply rated for at least 2–4 A, and tie its GND to the ESP32's GND.

| Sound Detector | ESP32 |
|---|---|
| VCC | 3V3 (keeps the analog outputs inside the ADC's range) |
| GND | GND (top of the right header) |
| AUDIO | VP / 36 |
| ENVELOPE | VN / 39 (wired but not used yet) |
| GATE | 34 (wired but not used yet) |

The detector's five wires go to the top four pins of the left header plus the GND directly opposite.

## Visualizers

| # | Name | What it shows |
|---|---|---|
| 0 | Spectrum Bars | 64 rainbow frequency bars that glow toward their tips, with falling peak dots. Bass hits flash the display, shift the colours and throw sparks |
| 1 | Spectrogram | Scrolling waterfall: time right → left, bass at the bottom, loudness as heat colour |

**Switching:** press the board's **BOOT** button to go to the next visualizer. Over serial (`make monitor`), send `n` for next, `p` for previous, `l` to list them, or `0`–`9` to pick one. Send `d` to print audio levels. To rotate through them automatically, set `AUTO_CYCLE_MS` in `src/config.h`.

### Project layout

```
led-matrix-audio-visualizer/
├── led-matrix-audio-visualizer.ino   main loop: capture → draw → flip, switching
└── src/
    ├── config.h                      pins, panel size, FFT and switching settings
    ├── core/
    │   ├── Audio.{h,cpp}             sampling + FFT → AudioFrame
    │   ├── AudioFrame.h              what each visualizer receives
    │   ├── Matrix.{h,cpp}            HUB75 panel setup
    │   ├── Color.h                   levelColor / heatColor / hueColor helpers
    │   ├── Visualizer.h              base class + REGISTER_VISUALIZER
    │   └── Registry.cpp              list of registered visualizers
    └── visualizers/                  one file per visualizer
        ├── SpectrumBars.cpp
        └── Spectrogram.cpp
```

### Adding a visualizer

Create one file in `src/visualizers/`. Nothing else needs editing, because the file registers itself:

```cpp
// src/visualizers/Pulse.cpp
#include "../core/Visualizer.h"

class Pulse : public Visualizer {
 public:
  const char* name() const override { return "Pulse"; }

  void draw(const AudioFrame& audio, Display& display) override {
    int r = audio.level * (PANEL_HEIGHT / 2);
    display.fillCircle(PANEL_WIDTH / 2, PANEL_HEIGHT / 2, r, hueColor(audio.frame / 200.0f));
  }
};

REGISTER_VISUALIZER(Pulse, 30);  // the number sets its position in the rotation
```

Each frame, `AudioFrame` gives you:
- `bands[64]`: the spectrum, 0–1, log-spaced from bass to treble.
- `bandsDb[64]`: the same bands on a decibel (log) scale, 0–1. It measures how far each band is above its own learned noise floor, so quiet detail shows and silence stays dark. The `LOG_*` settings in `config.h` control it.
- `waveform[256]`: the raw signal, −1 to 1.
- `level`: overall loudness, 0–1.
- `bass` and `beat`: bass level, 0–1, and a flag that is true on the frame a bass hit lands.
- `envelope` and `gate`: the Sound Detector's own outputs.

The levels adjust themselves to how loud the room is, so you don't need to scale them.

The screen is cleared before `draw()` and shown afterwards. Draw the whole picture every frame and keep any history in member variables. Reset that history in `reset()`, which runs whenever the visualizer is selected. `Display` is an Adafruit_GFX, so lines, circles, rectangles and text all work.

## Tuning

- **Panel stays dark or shows garbage:** some Waveshare panels use an FM6126A driver. Uncomment `cfg.driver = HUB75_I2S_CFG::FM6126A;` in `src/core/Matrix.cpp`.
- **Pixels shimmer:** try `cfg.clkphase = false;`.
- **Brightness:** change `PANEL_BRIGHTNESS` in `src/config.h`.
- **Decibel bands (`bandsDb`) noise:** if a visualizer using `bandsDb` shows bars in silence, raise `LOG_FLOOR_MARGIN_DB` in `src/config.h`. If the bars look too jumpy, raise `LOG_MIN_SPAN_DB`. Send `d` over serial to print the current levels while you tune.
- **Sensitivity:** the gain adjusts itself. If silence still shows bars, raise the `50.0f` floor passed to `trackGain()` in `src/core/Audio.cpp`. You can also change the gain resistor on the Sound Detector.
