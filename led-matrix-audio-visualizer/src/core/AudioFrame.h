#pragma once

#include "../config.h"

// One analysed block of audio, handed to the active visualizer every frame.
// Everything is auto-gained, so visualizers can assume roughly 0..1 ranges
// regardless of how loud the room is.
struct AudioFrame {
  float bands[NUM_BANDS];         // 0..1 spectrum, log-spaced low -> high
  float waveform[FFT_SAMPLES];    // -1..1 time domain, DC removed
  float level;                    // 0..1 overall loudness (RMS)
  float envelope;                 // 0..1 Sound Detector ENVELOPE output
  bool gate;                      // Sound Detector GATE output
  uint32_t frame;                 // increments on every capture
};
