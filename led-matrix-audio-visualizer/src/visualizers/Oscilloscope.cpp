// Oscilloscope: the raw waveform drawn as a glowing line, with fading trails
// of the previous few traces. Each trace starts at a rising zero-crossing so
// the picture holds still on steady tones instead of scrolling. The colour
// drifts faster when the music is loud, and beats flash the trace brighter
// and whiter.

#include "../core/Visualizer.h"

class Oscilloscope : public Visualizer {
 public:
  const char* name() const override { return "Oscilloscope"; }

  void reset() override {
    for (int t = 0; t < kTrails; t++) {
      for (int x = 0; x < PANEL_WIDTH; x++) trails_[t][x] = PANEL_HEIGHT / 2;
    }
    hue_ = 0;
    flash_ = 0;
  }

  void draw(const AudioFrame& audio, Display& display) override {
    if (audio.beat) flash_ = 1.0f;
    hue_ += 0.003f + audio.level * 0.01f;
    hue_ -= (int)hue_;

    drawAxis(display);

    // Age the trails, then record the newest trace in slot 0.
    memmove(trails_[1], trails_[0], sizeof(trails_[0]) * (kTrails - 1));
    const int start = findTrigger(audio.waveform);
    const float mid = (PANEL_HEIGHT - 1) / 2.0f;
    for (int x = 0; x < PANEL_WIDTH; x++) {
      float s = audio.waveform[start + x * kSpanSamples / PANEL_WIDTH];
      int y = (int)lroundf(mid - s * mid);
      trails_[0][x] = constrain(y, 0, PANEL_HEIGHT - 1);
    }

    // Oldest (dimmest) first, so the newest trace is drawn on top.
    for (int t = kTrails - 1; t >= 0; t--) {
      float age = (float)t / kTrails;  // 0 = newest
      float brightness = (1.0f - age) * (0.6f + 0.4f * flash_);
      float saturation = t == 0 ? 1.0f - 0.7f * flash_ : 1.0f;
      for (int x = 1; x < PANEL_WIDTH; x++) {
        uint16_t c = hsvColor(hue_ + x * (0.3f / PANEL_WIDTH), saturation, brightness);
        display.drawLine(x - 1, trails_[t][x - 1], x, trails_[t][x], c);
      }
    }

    flash_ *= 0.85f;
  }

 private:
  static constexpr int kTrails = 4;                 // traces kept, including the newest
  static constexpr int kSpanSamples = FFT_SAMPLES / 2;  // samples across the screen (6.4 ms)

  // First rising zero-crossing that still leaves a full screen of samples
  // after it; falls back to the start if there isn't one.
  static int findTrigger(const float* w) {
    for (int i = 1; i < FFT_SAMPLES - kSpanSamples; i++) {
      if (w[i - 1] <= 0 && w[i] > 0) return i;
    }
    return 0;
  }

  // Faint dotted centre line, like a scope graticule.
  static void drawAxis(Display& display) {
    const uint16_t dim = rgb565(40, 40, 40);
    for (int x = 0; x < PANEL_WIDTH; x += 4) display.drawPixel(x, PANEL_HEIGHT / 2, dim);
  }

  int8_t trails_[kTrails][PANEL_WIDTH] = {};  // y of each trace, newest first
  float hue_ = 0;                             // base hue, drifts over time
  float flash_ = 0;                           // 1 on a beat, decays to 0
};

REGISTER_VISUALIZER(Oscilloscope, 30);
