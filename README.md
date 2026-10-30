# led-matrix-audio-visualizer

A 64-band FFT spectrum visualizer that runs on an ESP32, draws on a Waveshare 64x32 RGB (HUB75) LED matrix and listens through a SparkFun Sound Detector.

## Build environment

```sh
nix develop          # or `direnv allow` to load it automatically
make setup           # first time only: installs the ESP32 core and libraries into .arduino/
make upload          # compile and flash to /dev/ttyUSB0 (override with PORT=/dev/ttyUSBx)
make monitor         # serial console at 115200 baud
```

The ESP32 toolchains that arduino-cli downloads are prebuilt binaries for a normal Linux layout, so the flake wraps `arduino-cli` in an FHS environment. Without it they won't run on NixOS.

## Wiring

| Matrix (HUB75) | ESP32 | | Matrix | ESP32 |
|---|---|---|---|---|
| R1 | 25 | | A | 23 |
| G1 | 26 | | B | 19 |
| B1 | 27 | | C | 5 |
| R2 | 14 | | D | 17 |
| G2 | 12 | | E | — (not used at 32 rows) |
| B2 | 13 | | LAT | 4 |
| CLK | 16 | | OE | 15 |

Power the matrix from its own 5 V supply rated for at least 2–4 A, and tie its GND to the ESP32's GND.

| Sound Detector | ESP32 |
|---|---|
| VCC | 3V3 (keeps the analog outputs inside the ADC's range) |
| GND | GND |
| AUDIO | 34 |
| ENVELOPE | 35 (wired but not used yet) |
| GATE | 32 (wired but not used yet) |

## Tuning

- **Panel stays dark or shows garbage:** some Waveshare panels use an FM6126A driver. Uncomment `cfg.driver = HUB75_I2S_CFG::FM6126A;` in `setupMatrix()`.
- **Pixels shimmer:** try `cfg.clkphase = false;`.
- **Brightness:** change `setBrightness8()`.
- **Sensitivity:** the gain adjusts itself. If silence still shows bars, raise the `50.0f` floor in `drawSpectrum()`. You can also change the gain resistor on the Sound Detector.
