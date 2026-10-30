// Demoscene-style fire: each column is fed heat from the bottom by its
// frequency band, so loud bands burn tall. Beats send a flare across the
// whole base.

#include "../core/Visualizer.h"

class Fire : public Visualizer {
 public:
  Fire() {
    // Black -> red -> yellow -> white.
    for (int i = 0; i < 256; i++) {
      float t = i / 255.0f * 3;
      palette_[i] = rgb565((uint8_t)(clamp01(t) * 255), (uint8_t)(clamp01(t - 1) * 255),
                           (uint8_t)(clamp01(t - 2) * 255));
    }
  }

  const char* name() const override { return "Fire"; }

  void reset() override {
    memset(heat_, 0, sizeof(heat_));
    flare_ = 0;
  }

  void draw(const AudioFrame& audio, Display& display) override {
    if (audio.beat) flare_ = 1.0f;

    // The power below 1 lets quieter bands still raise a small flame.
    uint8_t* base = heat_[PANEL_HEIGHT - 1];
    for (int x = 0; x < PANEL_WIDTH; x++) {
      float level = powf(audio.bands[x * NUM_BANDS / PANEL_WIDTH], 0.7f);
      int fuel = (int)(level * 230 + flare_ * 120) + random(-20, 21);
      base[x] = constrain(fuel, 0, 255);
    }

    // Going top-down means every row reads the rows below it from the
    // previous frame, so heat climbs one row per frame.
    for (int y = 0; y < PANEL_HEIGHT - 1; y++) {
      const uint8_t* below = heat_[y + 1];
      const uint8_t* below2 = heat_[min(y + 2, PANEL_HEIGHT - 1)];
      for (int x = 0; x < PANEL_WIDTH; x++) {
        int left = x > 0 ? x - 1 : x;
        int right = x < PANEL_WIDTH - 1 ? x + 1 : x;
        int sum = below[left] + 2 * below[x] + below[right] + below2[x];
        int h = (int)(sum * (kKeep / 5.0f)) - random(0, kCoolingMax + 1);
        heat_[y][x] = h > 0 ? h : 0;
      }
    }

    for (int y = 0; y < PANEL_HEIGHT; y++) {
      for (int x = 0; x < PANEL_WIDTH; x++) {
        if (heat_[y][x]) display.drawPixel(x, y, palette_[heat_[y][x]]);
      }
    }

    flare_ *= 0.8f;
  }

 private:
  // Fraction of heat kept per row climbed; lower = shorter flames.
  static constexpr float kKeep = 0.94f;
  // Extra random cooling per row, for flicker.
  static constexpr int kCoolingMax = 3;

  uint8_t heat_[PANEL_HEIGHT][PANEL_WIDTH] = {};  // row 0 is the top
  uint16_t palette_[256];
  float flare_ = 0;  // 1 on a beat, decays to 0
};

REGISTER_VISUALIZER(Fire, 40);
