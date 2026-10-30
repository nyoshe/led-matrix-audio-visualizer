// Retro LED bargraph: a 16x8 grid of chunky segmented lamps, lit or dim, like
// an old car-stereo meter. Each column climbs fast and drains slow with its
// slice of the spectrum, bass to treble, with a peak-hold lamp.

#include "../core/Visualizer.h"

class LedBargraph : public Visualizer {
 public:
  const char* name() const override { return "LED Bargraph"; }

  void reset() override {
    for (int b = 0; b < kBars; b++) bar_[b] = peak_[b] = 0;
    flash_ = 0;
  }

  void draw(const AudioFrame& audio, Display& display) override {
    if (audio.beat) flash_ = 1.0f;

    for (int b = 0; b < kBars; b++) {
      float sum = 0;
      for (int k = 0; k < kBandsPerBar; k++) sum += audio.bands[b * kBandsPerBar + k];
      float target = (sum / kBandsPerBar) * kSegments;  // 0..kSegments, continuous
      smoothTowards(bar_[b], target, kAttackAlpha, kReleaseAlpha);

      // Peak never drops below the current bar.
      if (bar_[b] >= peak_[b]) peak_[b] = bar_[b];
      else peak_[b] = max(peak_[b] - kPeakDecay, bar_[b]);

      // Quantise the continuous, already-smoothed values to a whole lamp
      // count now -- smoothing the lamp count itself would look stepped.
      int lit = clampInt((int)(bar_[b] + 0.5f), 0, kSegments);
      int peakCount = clampInt((int)(peak_[b] + 0.5f), 0, kSegments);

      const int x0 = b * kBarPitch;
      for (int s = 0; s < kSegments; s++) {
        const float t = (float)s / (float)(kSegments - 1);  // 0 bottom -> 1 top
        const uint16_t base = levelColor(t);  // green -> yellow -> red, VU style

        uint16_t color;
        if (peakCount > 0 && s == peakCount - 1) {
          color = tint565(base, 1.0f, kPeakWhite + (1.0f - kPeakWhite) * flash_);
        } else if (s < lit) {
          color = tint565(base, 1.0f);  // lit lamp, full brightness
        } else {
          color = tint565(base, kGhostBrightness);  // unlit lamp, ghost dim
        }

        const int y0 = PANEL_HEIGHT - kSegPitch * (s + 1);
        for (int yy = 0; yy < kSegHeight; yy++) {
          for (int xx = 0; xx < kBarWidth; xx++) {
            display.drawPixel(x0 + xx, y0 + yy, color);
          }
        }
      }
    }

    flash_ *= kFlashDecay;
  }

 private:
  static constexpr int kBars = 16;                            // bar columns across the width
  static constexpr int kSegments = 8;                         // lamps stacked in each column
  static constexpr int kBarPitch = PANEL_WIDTH / kBars;        // 4px: 3 lit + 1 gap
  static constexpr int kBarWidth = kBarPitch - 1;              // 3
  static constexpr int kSegPitch = PANEL_HEIGHT / kSegments;   // 4px: 3 lit + 1 gap
  static constexpr int kSegHeight = kSegPitch - 1;             // 3
  static constexpr int kBandsPerBar = NUM_BANDS / kBars;       // bands averaged per column

  static constexpr float kAttackAlpha = 0.75f;   // how fast a column rises
  static constexpr float kReleaseAlpha = 0.12f;  // how slowly it falls back
  static constexpr float kPeakDecay = 0.04f;     // how slowly the peak lamp drains, in lamps/frame
  static constexpr float kGhostBrightness = 0.08f;  // unlit lamps, as a fraction of full colour
  static constexpr float kPeakWhite = 0.35f;     // peak lamp's white tint at rest (more on a beat)
  static constexpr float kFlashDecay = 0.85f;    // how fast the beat flash fades

  float bar_[kBars] = {};
  float peak_[kBars] = {};
  float flash_ = 0;
};

REGISTER_VISUALIZER(LedBargraph, 70);
