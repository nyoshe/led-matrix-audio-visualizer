#pragma once

// All hardware and tuning constants in one place.

#include <stdint.h>

// ---- Matrix -----------------------------------------------------------------
constexpr int PANEL_WIDTH  = 64;
constexpr int PANEL_HEIGHT = 32;
constexpr int PANEL_CHAIN  = 1;
constexpr uint8_t PANEL_BRIGHTNESS = 96;  // 0-255

// Pin map for the ELEGOO ESP-WROOM-32 (38-pin DevKitC layout), chosen so each
// group of wires lands on a consecutive run of header pins and the boot
// strapping pins (0, 2, 12, 15) are never driven.
namespace Pins {
// HUB75 colour lines: left header, six pins in a row (32 33 25 26 27 14).
constexpr int8_t R1 = 32;
constexpr int8_t G1 = 33;
constexpr int8_t B1 = 25;
constexpr int8_t R2 = 26;
constexpr int8_t G2 = 27;
constexpr int8_t B2 = 14;
// HUB75 address/control: right header, a run from 23 down to 17
// (skipping TX/RX and the GND pin, which the matrix GND can use).
constexpr int8_t A   = 23;
constexpr int8_t B   = 22;
constexpr int8_t C   = 21;
constexpr int8_t D   = 19;
constexpr int8_t CLK = 18;
constexpr int8_t LAT = 5;
constexpr int8_t OE  = 17;
constexpr int8_t E   = -1;  // unused on a 1/16 scan 32-row panel

// Sound detector AUDIO output: top of the left header, an input-only ADC1 pin.
constexpr int8_t AUDIO = 36;  // VP: raw waveform, biased at VCC/2

// The board's BOOT button (GPIO0 has an on-board pull-up and is only a
// strapping pin at reset, so it's free to read afterwards).
constexpr int8_t BUTTON = 0;
}  // namespace Pins

// ---- Audio analysis ---------------------------------------------------------
// Audio is sampled by the ADC's DMA engine and analysed on core 0, in the
// background, independently of the screen's frame rate.
// The ADC runs at SAMPLE_RATE_HZ x ADC_OVERSAMPLE and each group of
// ADC_OVERSAMPLE readings is averaged into one sample, which lowers random ADC
// noise (by up to 6 dB at 4x). The DMA ADC accepts 20 kHz to 2 MHz in total
// across both pins. Each analysis must finish within one hop of samples; `d`
// prints the timing and the audio task's CPU load.
constexpr uint16_t FFT_SAMPLES       = 256;    // window size, power of 2 (12.8 ms at 20 kHz)
constexpr uint32_t SAMPLE_RATE_HZ    = 20000;  // after averaging; Nyquist 10 kHz, bin 78 Hz
constexpr uint16_t ADC_OVERSAMPLE    = 4;      // ADC readings averaged per sample (1 = off)
constexpr uint16_t AUDIO_HOP_SAMPLES = 128;    // analyse every N new samples (156 Hz at 20 kHz)
constexpr uint16_t FFT_MIN_BIN       = 2;      // skip DC and the lowest rumble (bin 2 = 156 Hz)
constexpr float    FFT_MAX_HZ        = 10000;  // top of the band layout (capped at Nyquist)
constexpr int      NUM_BANDS         = 64;     // log-spaced bands in AudioFrame

constexpr float ANALYSIS_RATE_HZ = (float)SAMPLE_RATE_HZ / AUDIO_HOP_SAMPLES;

// Music and most mics get quieter towards the treble (measured here: about
// 1.6 dB per octave), which leaves the right-hand bars short. AudioFrame::bands
// are boosted by this much per octave above the lowest band to even that out.
// 0 = no weighting. Measured on this setup, 2 evens out the low and high
// thirds of the display on average; 3 makes the treble about twice as tall.
constexpr float TREBLE_TILT_DB_PER_OCTAVE = 2.0f;

// Noise calibration (serial `c`, with the mic covered): this many times the
// measured noise is subtracted from each FFT bin. Above 1 also removes most
// of the frame-to-frame flicker of the noise, not just its average.
constexpr float NOISE_OVERSUBTRACT = 1.5f;

// Decibel scale (AudioFrame::bandsDb). Each band learns its own noise floor
// (drops quickly, creeps up slowly), and 0..1 covers how far the band is above
// that floor, up to the loudest band recently seen.
constexpr float LOG_FLOOR_MARGIN_DB = 8.0f;   // ignore this much above the floor (hides hiss)
constexpr float LOG_MIN_SPAN_DB     = 12.0f;  // full height is at least this far above the floor
constexpr float LOG_MAX_SPAN_DB     = 40.0f;  // ...and at most this far
constexpr float LOG_FLOOR_FALL_S    = 0.4f;   // time constant for the floor dropping to quieter input
constexpr float LOG_FLOOR_RISE_S    = 10.0f;  // time constant for it creeping back up

// Automatic gain: the display scale follows the loudest band (in dB), rising
// quickly when audio gets louder and easing back when it gets quieter. The
// followed level is drawn at AGC_HEADROOM of full height.
constexpr float AGC_ATTACK_S  = 0.02f;  // time constant for catching up with louder audio
constexpr float AGC_RELEASE_S = 0.5f;   // time constant for easing back after it gets quieter
constexpr float AGC_HEADROOM  = 0.9f;   // followed level sits at this fraction of full height

// ---- Rendering --------------------------------------------------------------
// Visualizers are drawn at this fixed rate, from the newest analysed audio.
constexpr uint32_t RENDER_FPS = 60;

// ---- Visualizer switching ---------------------------------------------------
constexpr uint32_t AUTO_CYCLE_MS = 0;  // switch visualizer every N ms; 0 = off
