#pragma once

#include "AudioFrame.h"

// Samples the SparkFun Sound Detector and turns it into AudioFrames, in the
// background on core 0.
namespace Audio {
// Starts DMA sampling and the analysis task.
void begin();
// Copies the newest analysed frame into `out`. Never blocks. `beat` is true if
// a beat landed at any point since the previous call, so none are missed when
// audio is analysed faster than the screen is drawn.
void latest(AudioFrame& out);
// Prints audio rates, decibel levels and bar heights to Serial, for tuning.
void printStats();
// Prints every band's frequency and level to Serial, for finding noise.
void printBands();
}  // namespace Audio
