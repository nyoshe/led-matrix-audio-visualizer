#include "Matrix.h"

#include "../config.h"

Display& Matrix::begin() {
  HUB75_I2S_CFG::i2s_pins pins = {Pins::R1, Pins::G1, Pins::B1, Pins::R2, Pins::G2, Pins::B2,
                                  Pins::A,  Pins::B,  Pins::C,  Pins::D,  Pins::E,
                                  Pins::LAT, Pins::OE, Pins::CLK};
  HUB75_I2S_CFG cfg(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN, pins);
  cfg.double_buff = true;  // draw off-screen, then flip: no flicker
  // Some Waveshare panels use an FM6126A driver; if yours stays dark, try:
  // cfg.driver = HUB75_I2S_CFG::FM6126A;
  // If colours/pixels shimmer, try: cfg.clkphase = false;

  Display* display = new Display(cfg);
  if (!display->begin()) {
    Serial.println("Matrix init failed (not enough DMA memory?)");
    while (true) delay(1000);
  }
  display->setBrightness8(PANEL_BRIGHTNESS);
  display->clearScreen();
  return *display;
}
