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
| 0 | Spectrum Bars | 64 rainbow frequency bars that glow toward their tips, with falling peak dots. Bass hits flash the bars brighter, shift the colours and throw sparks |
| 1 | Spectrogram | Scrolling waterfall: time right → left, bass at the bottom, loudness as heat colour |
| 2 | Oscilloscope | The raw waveform as a glowing line with fading trails, held steady on a rising zero-crossing. Colour drifts with loudness, and beats flash it brighter |
| 3 | Fire | Demoscene-style flames fed from the bottom by each column's band. Loud bands burn tall, and beats send a flare across the base |
| 4 | VU Meter | Twin analog gauges (bass left, treble right) with a green→yellow→red scale arc, swinging needle and a peak-hold lamp that flashes on beats |
| 5 | Kaleidoscope | One rotating quadrant mirrored four ways, coloured by angle (frequency band) and distance from the centre. Spins faster when it's loud, and beats jolt it |
| 6 | LED Bargraph | A chunky 16×8 grid of retro LED segments, green→yellow→red, with dim "ghost" segments when unlit and a peak-hold segment per column |

**Switching:** press the board's **BOOT** button to go to the next visualizer. Over serial (`make monitor`), send `n` for next, `p` for previous, `l` to list them, or `0`–`9` to pick one. Send `d` to print audio levels and frame rates, or `b` to print every band's frequency and level plus the calibrated noise profile (handy for spotting interference). Send `c` to calibrate the noise, or `x` to clear the calibration (see Tuning). To rotate through them automatically, set `AUTO_CYCLE_MS` in `src/config.h`.

**Colour palettes:** send `k` over serial to cycle through the colour palettes: Synthwave (the default), Sunset, Ocean, Aurora, Lava, Ice and Rainbow. Spectrum Bars and Oscilloscope use them, and the choice is saved across reboots. Each palette is a looping gradient defined in `src/core/Palette.cpp`, so adding one is a list of colour stops.

### How audio and drawing fit together

Audio and drawing run independently, on separate CPU cores:

- **Core 0, audio:** the ADC reads the Sound Detector at 80 kHz into memory via DMA, with no CPU involved. Every 4 readings (`ADC_OVERSAMPLE`) are averaged into one 20 kHz sample, which lowers ADC noise (about 4 dB measured here). Every `AUDIO_HOP_SAMPLES` (128) new samples, a background task analyses the latest `FFT_SAMPLES` (256), about 156 times a second. Each window overlaps the previous one by half, so a fresh analysis is ready every 6.4 ms. `d` prints the rates, timing and CPU load.
- **Core 1, drawing:** `loop()` draws the active visualizer at a steady `RENDER_FPS` using the newest analysis. It never waits for audio. A beat that lands between two frames is held until the next frame, so none are missed.

All of these settings are in `src/config.h`. Raising `RENDER_FPS` or the analysis rate doesn't affect the other.

### Project layout

```
led-matrix-audio-visualizer/
├── led-matrix-audio-visualizer.ino   render loop (core 1): draw newest audio at RENDER_FPS, switching
└── src/
    ├── config.h                      pins, panel size, FFT and switching settings
    ├── core/
    │   ├── Audio.{h,cpp}             DMA sampling + FFT task (core 0) → AudioFrame
    │   ├── AudioFrame.h              what each visualizer receives
    │   ├── Matrix.{h,cpp}            HUB75 panel setup
    │   ├── Color.h                   levelColor / heatColor / hueColor helpers
    │   ├── Palette.{h,cpp}           looping colour palettes (serial `k`), paletteColor()
    │   ├── Visualizer.h              base class + REGISTER_VISUALIZER
    │   └── Registry.cpp              list of registered visualizers
    └── visualizers/                  one file per visualizer
        ├── SpectrumBars.cpp
        ├── Spectrogram.cpp
        ├── Oscilloscope.cpp
        ├── Fire.cpp
        ├── VuMeter.cpp
        ├── Kaleidoscope.cpp
        └── LedBargraph.cpp
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
- `bands[64]`: the spectrum, 0–1, log-spaced from bass to treble. The treble is boosted by `TREBLE_TILT_DB_PER_OCTAVE` so high bands aren't dwarfed by the bass.
- `bandsDb[64]`: the same bands on a decibel (log) scale, 0–1. It measures how far each band is above its own learned noise floor, so quiet detail shows and silence stays dark. The `LOG_*` settings in `config.h` control it.
- `waveform[256]`: the raw signal, −1 to 1.
- `level`: overall loudness, 0–1.
- `bass` and `beat`: bass level, 0–1, and a flag that is true on the frame a bass hit lands.
- `envelope` and `gate`: the Sound Detector's own outputs.

The levels adjust themselves to how loud the room is, so you don't need to scale them.

The screen is cleared before `draw()` and shown afterwards. Draw the whole picture every frame and keep any history in member variables. Reset that history in `reset()`, which runs whenever the visualizer is selected. `Display` is an Adafruit_GFX, so lines, circles, rectangles and text all work.

## Tuning

- **Noise calibration:** during a quiet moment, without touching the mic, send `c` over serial. Covering the mic by hand tends to add noise rather than remove it. For 3 seconds the board measures the noise in every FFT bin, including ADC hiss, low rumble and steady interference tones such as the matrix's 625 Hz whine. It keeps the quietest 0.1 s stretch of each bin, so a bump or a brief sound during calibration doesn't spoil it. From then on, `NOISE_OVERSUBTRACT` (1.5) times that noise is subtracted from each bin before the bars are drawn, so silence stays dark. The result is saved to flash and reloaded on boot. Recalibrate after changing the sample rate, `ADC_OVERSAMPLE` or `FFT_SAMPLES`, since an old calibration is ignored if they don't match. Raise `NOISE_OVERSUBTRACT` if silence still flickers, or lower it if quiet sounds disappear. If the bars go missing after a calibration, send `x` to clear it.
- **Panel stays dark or shows garbage:** some Waveshare panels use an FM6126A driver. Uncomment `cfg.driver = HUB75_I2S_CFG::FM6126A;` in `src/core/Matrix.cpp`.
- **Pixels shimmer:** try `cfg.clkphase = false;`.
- **Brightness:** change `PANEL_BRIGHTNESS` in `src/config.h`.
- **Decibel bands (`bandsDb`) noise:** if a visualizer using `bandsDb` shows bars in silence, raise `LOG_FLOOR_MARGIN_DB` in `src/config.h`. If the bars look too jumpy, raise `LOG_MIN_SPAN_DB`. Send `d` over serial to print the current levels while you tune.
- **Treble vs bass balance:** raise `TREBLE_TILT_DB_PER_OCTAVE` in `src/config.h` if the right-hand bars run short, or lower it if they tower over the bass. `d` prints the average bar height for the low, middle and high thirds of the display.
- **Recovery after loud spikes:** `AGC_RELEASE_S` sets how quickly the scale eases back after a loud moment. Lower is faster, but too low makes quiet and loud songs look the same. `AGC_ATTACK_S` sets how quickly it catches up when audio gets louder.
- **Sensitivity:** the gain adjusts itself. If silence still shows bars, raise the `50.0f` floor passed to `trackGain()` in `src/core/Audio.cpp`. You can also change the gain resistor on the Sound Detector.
