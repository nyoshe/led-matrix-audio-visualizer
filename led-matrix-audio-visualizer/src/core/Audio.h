#pragma once

#include "AudioFrame.h"

// Samples the SparkFun Sound Detector and turns it into an AudioFrame.
namespace Audio {
void begin();
// Blocks for one block of samples (FFT_SAMPLES / SAMPLE_RATE_HZ seconds).
const AudioFrame& capture();
// Prints decibel levels and bar heights to Serial, for tuning.
void printStats();
}  // namespace Audio
