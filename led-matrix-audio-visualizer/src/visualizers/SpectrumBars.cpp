// Spectrum analyser: one bar per column, with peak markers that fall back
// slowly.
//
// Each column gets its own hue, so the bars form a rainbow that slowly drifts
// round the colour wheel (faster when the music is loud). Bars glow brighter
// towards their tips, and tall bars burn towards white at the top. Bass hits
// flash everything brighter, jolt the hue, wash the background in the
// complementary colour and throw sparks above the tallest bars.

#include "../core/Visualizer.h"

class SpectrumBars : public Visualizer {
 public:
  const char* name() const override { return "Spectrum Bars"; }

  void reset() override {
    for (int x = 0; x < PANEL_WIDTH; x++) bar_[x] = peak_[x] = 0;
    hue_ = 0;
    flash_ = 0;
  }

  void draw(const AudioFrame& audio, Display& display) override {
    if (audio.beat) {
      flash_ = 1.0f;
      hue_ += 0.12f;
    }
    hue_ += 0.002f + audio.level * 0.01f;
    hue_ -= (int)hue_;

    // Normal frames sit at 55% brightness, leaving headroom for beat flashes.
    const float intensity = 0.55f + 0.45f * flash_;
    if (flash_ > 0.1f) display.fillScreen(hsvColor(hue_ + 0.5f, 1.0f, flash_ * 0.2f));

    for (int x = 0; x < PANEL_WIDTH; x++) {
      // Decibel scale, so quiet detail is visible next to loud bass.
      float target = audio.bandsDb[x * NUM_BANDS / PANEL_WIDTH] * PANEL_HEIGHT;

      // Fast attack, slower release.
      bar_[x] = target > bar_[x] ? target : bar_[x] * 0.85f + target * 0.15f;

      if (bar_[x] >= peak_[x]) peak_[x] = bar_[x];
      else peak_[x] = max(peak_[x] - 0.4f, 0.0f);

      const float hue = hue_ + x * (0.8f / PANEL_WIDTH);
      const float height = bar_[x] / PANEL_HEIGHT;  // 0..1, how tall this bar is
      const int h = (int)bar_[x];

      for (int y = 0; y < h; y++) {
        float along = (float)(y + 1) / h;              // 0 at the base, 1 at the tip
        float b = (0.25f + 0.75f * along) * intensity;  // glow towards the tip
        float s = 1.0f - along * max(height - 0.5f, 0.0f) * 1.6f;  // tall tips burn white
        display.drawPixel(x, PANEL_HEIGHT - 1 - y, hsvColor(hue, s, b));
      }

      int p = min((int)peak_[x], PANEL_HEIGHT);
      if (p > 0) {
        // Peak dots are a pastel of the bar's colour, white during a flash.
        display.drawPixel(x, PANEL_HEIGHT - p, hsvColor(hue, 0.4f * (1 - flash_), 1.0f));
      }

      // Sparks fly off the top of tall bars, mostly on beats.
      if (height > 0.6f && random(100) < 4 + (int)(flash_ * 40)) {
        int sparkY = PANEL_HEIGHT - p - 1 - random(4);
        if (sparkY >= 0) display.drawPixel(x, sparkY, rgb565(255, 255, 255));
      }
    }

    flash_ *= 0.82f;
  }

 private:
  float bar_[PANEL_WIDTH] = {};   // smoothed bar height, in pixels
  float peak_[PANEL_WIDTH] = {};  // falling peak marker, in pixels
  float hue_ = 0;                 // base hue, drifts over time
  float flash_ = 0;               // 1 on a beat, decays to 0
};

REGISTER_VISUALIZER(SpectrumBars, 10);
