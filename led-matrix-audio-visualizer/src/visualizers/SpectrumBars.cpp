// Spectrum analyser: one bar per column, with peak markers that fall back
// slowly.
//
// Colours come from the current palette (serial `k`): it spreads across the
// columns and slowly scrolls (faster when the music is loud), and each bar
// also shifts along the palette from base to tip. Bars glow brighter towards
// their tips, and tall bars burn towards white at the top. Bass hits flash
// the bars brighter, jolt the palette along and throw sparks above the
// tallest bars.

#include "../core/Visualizer.h"

class SpectrumBars : public Visualizer {
 public:
  const char* name() const override { return "Spectrum Bars"; }

  void reset() override {
    for (int x = 0; x < PANEL_WIDTH; x++) bar_[x] = peak_[x] = 0;
    offset_ = 0;
    flash_ = 0;
  }

  void draw(const AudioFrame& audio, Display& display) override {
    if (audio.beat) {
      flash_ = 1.0f;
      offset_ += 0.12f;
    }
    offset_ += 0.002f + audio.level * 0.01f;
    offset_ -= (int)offset_;

    // Normal frames sit at 55% brightness, leaving headroom for beat flashes.
    const float intensity = 0.55f + 0.45f * flash_;

    for (int x = 0; x < PANEL_WIDTH; x++) {
      float magnitude = audio.bands[x * NUM_BANDS / PANEL_WIDTH] * PANEL_HEIGHT;

      // Smooth the magnitude over time with an asymmetric EWMA: rise fast so
      // hits land immediately, fall slowly so bars don't flicker.
      float alpha = magnitude > bar_[x] ? kAttackAlpha : kReleaseAlpha;
      bar_[x] += alpha * (magnitude - bar_[x]);

      // Peak dots follow the raw magnitude, so they snap to every hit even
      // though the bars are smoothed. Never let them sit below the bar.
      if (magnitude >= peak_[x]) peak_[x] = magnitude;
      else peak_[x] = max(peak_[x] - 0.4f, 0.0f);
      peak_[x] = max(peak_[x], bar_[x]);

      const float t = offset_ + x * (kColumnSpread / PANEL_WIDTH);  // palette position
      const float height = bar_[x] / PANEL_HEIGHT;  // 0..1, how tall this bar is
      const int h = (int)bar_[x];

      for (int y = 0; y < h; y++) {
        float along = (float)(y + 1) / h;              // 0 at the base, 1 at the tip
        float b = (0.25f + 0.75f * along) * intensity;  // glow towards the tip
        float white = along * max(height - 0.5f, 0.0f) * 1.6f;  // tall tips burn white
        display.drawPixel(x, PANEL_HEIGHT - 1 - y, paletteColor(t + along * kHeightSpread, b, white));
      }

      int p = min((int)peak_[x], PANEL_HEIGHT);
      if (p > 0) {
        // Peak dots are a pastel of the bar's colour, white during a flash.
        float white = 0.6f + 0.4f * flash_;
        display.drawPixel(x, PANEL_HEIGHT - p, paletteColor(t + kHeightSpread, 1.0f, white));
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
  // Weight of the newest frame in each bar's exponentially weighted moving
  // average, separately for rising and falling: 1 = no smoothing, lower =
  // smoother but slower to respond. A frame k frames old contributes
  // alpha * (1 - alpha)^k.
  static constexpr float kAttackAlpha  = 0.8f;
  static constexpr float kReleaseAlpha = 0.3f;
  // How much of the palette spans the width of the screen, and how far along
  // it a full-height bar shifts from base to tip.
  static constexpr float kColumnSpread = 0.6f;
  static constexpr float kHeightSpread = 0.15f;

  float bar_[PANEL_WIDTH] = {};   // smoothed bar height, in pixels
  float peak_[PANEL_WIDTH] = {};  // falling peak marker, in pixels
  float offset_ = 0;              // palette position, drifts over time
  float flash_ = 0;               // 1 on a beat, decays to 0
};

REGISTER_VISUALIZER(SpectrumBars, 10);
