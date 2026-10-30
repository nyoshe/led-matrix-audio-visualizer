#pragma once

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// The HUB75 panel. It's an Adafruit_GFX, so lines, circles, text etc. work.
using Display = MatrixPanel_I2S_DMA;

namespace Matrix {
// Initialises the panel (double buffered) or halts if it can't.
Display& begin();
}  // namespace Matrix
