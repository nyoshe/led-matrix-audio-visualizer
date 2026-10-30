// Classic spectrum analyser: one bar per column, green -> red going up, with
// white peak markers that fall back slowly.

#include "../core/Visualizer.h"

class SpectrumBars : public Visualizer {
 public:
  const char* name() const override { return "Spectrum Bars"; }

  void reset() override {
    for (int x = 0; x < PANEL_WIDTH; x++) bar_[x] = peak_[x] = 0;
  }

  void draw(const AudioFrame& audio, Display& display) override {
    const uint16_t peakColor = rgb565(255, 255, 255);

    for (int x = 0; x < PANEL_WIDTH; x++) {
      float target = audio.bands[x * NUM_BANDS / PANEL_WIDTH] * PANEL_HEIGHT;

      // Fast attack, slower release.
      bar_[x] = target > bar_[x] ? target : bar_[x] * 0.85f + target * 0.15f;

      if (bar_[x] >= peak_[x]) peak_[x] = bar_[x];
      else peak_[x] = max(peak_[x] - 0.4f, 0.0f);

      int h = (int)bar_[x];
      for (int y = 0; y < h; y++) {
        display.drawPixel(x, PANEL_HEIGHT - 1 - y, levelColor((float)y / (PANEL_HEIGHT - 1)));
      }

      int p = min((int)peak_[x], PANEL_HEIGHT);
      if (p > 0) display.drawPixel(x, PANEL_HEIGHT - p, peakColor);
    }
  }

 private:
  float bar_[PANEL_WIDTH] = {};   // smoothed bar height, in pixels
  float peak_[PANEL_WIDTH] = {};  // falling peak marker, in pixels
};

REGISTER_VISUALIZER(SpectrumBars, 10);
