// Kaleidoscope: one audio-reactive quadrant mirrored four ways into a radial
// pattern. Wedges are coloured by frequency band and distance, rotate faster
// with loudness, and jolt on beats.

#include <math.h>

#include "../core/Visualizer.h"

class Kaleidoscope : public Visualizer {
 public:
  Kaleidoscope() { computeTable(); }

  const char* name() const override { return "Kaleidoscope"; }

  void reset() override {
    for (int b = 0; b < NUM_BANDS; b++) bandSmooth_[b] = 0;
    rotation_ = 0;
    paletteOffset_ = 0;
    flash_ = 0;
  }

  void draw(const AudioFrame& audio, Display& display) override {
    if (audio.beat) {
      flash_ = 1.0f;
      rotation_ += kBeatJolt;
      paletteOffset_ += kBeatPaletteJolt;
    }
    rotation_ = wrap01(rotation_ + kRotSpeedBase + audio.level * kRotSpeedAudio);
    paletteOffset_ = wrap01(paletteOffset_ + kPaletteDrift);

    // Smooth each band over time with an asymmetric EWMA: rise fast so
    // wedges snap bright on a hit, fall slowly so they don't flicker.
    for (int b = 0; b < NUM_BANDS; b++) {
      float mag = audio.bands[b];
      float alpha = mag > bandSmooth_[b] ? kAttackAlpha : kReleaseAlpha;
      bandSmooth_[b] += alpha * (mag - bandSmooth_[b]);
    }

    const float intensity = 0.6f + 0.4f * flash_;

    for (int qy = 0; qy < kQuadH; qy++) {
      for (int qx = 0; qx < kQuadW; qx++) {
        const int idx = qy * kQuadW + qx;

        const float a = wrap01(angleFrac_[idx] + rotation_);
        int band = (int)(a * NUM_BANDS);
        if (band >= NUM_BANDS) band = NUM_BANDS - 1;
        const float mag = bandSmooth_[band];
        const float dist = distNorm_[idx];

        float brightness = clamp01((0.15f + 0.95f * mag) * (1.0f - 0.35f * dist)) * intensity;
        float white = flash_ * (1.0f - dist) * 0.5f;
        float t = paletteOffset_ + a + dist * kDistSpread;
        uint16_t color = paletteColor(t, brightness, white);

        // 64x32 isn't square, so 8-fold symmetry won't tile; mirror 4 ways
        // instead. The pivot corner (kQuadW-1, kQuadH-1) lands on the panel's
        // true centre.
        const int xRight = PANEL_WIDTH - 1 - qx;
        const int yBottom = PANEL_HEIGHT - 1 - qy;
        display.drawPixel(qx, qy, color);
        display.drawPixel(xRight, qy, color);
        display.drawPixel(qx, yBottom, color);
        display.drawPixel(xRight, yBottom, color);
      }
    }

    flash_ *= 0.85f;
  }

 private:
  static constexpr int kQuadW = PANEL_WIDTH / 2;
  static constexpr int kQuadH = PANEL_HEIGHT / 2;
  static constexpr float kPi = 3.14159265358979323846f;

  static constexpr float kAttackAlpha = 0.8f;
  static constexpr float kReleaseAlpha = 0.25f;
  static constexpr float kRotSpeedBase = 0.0015f;    // turns/frame, idle rotation
  static constexpr float kRotSpeedAudio = 0.01f;     // extra turns/frame at full level
  static constexpr float kBeatJolt = 0.07f;          // sudden rotation jump on a beat
  static constexpr float kBeatPaletteJolt = 0.05f;   // palette jump on a beat
  static constexpr float kPaletteDrift = 0.0006f;    // slow constant palette scroll
  static constexpr float kDistSpread = 0.35f;        // how far the palette shifts rim->pivot

  static float wrap01(float t) { return t - floorf(t); }

  // Purely geometric, so it's filled once here and never changes.
  void computeTable() {
    const float pivotX = (float)(kQuadW - 1);
    const float pivotY = (float)(kQuadH - 1);
    const float maxDist = sqrtf(pivotX * pivotX + pivotY * pivotY);
    for (int qy = 0; qy < kQuadH; qy++) {
      for (int qx = 0; qx < kQuadW; qx++) {
        const float dx = qx - pivotX;
        const float dy = qy - pivotY;
        const float angle = atan2f(dy, dx);
        const int idx = qy * kQuadW + qx;
        angleFrac_[idx] = (angle + kPi) / (2.0f * kPi);
        distNorm_[idx] = clamp01(sqrtf(dx * dx + dy * dy) / maxDist);
      }
    }
  }

  float angleFrac_[kQuadW * kQuadH];
  float distNorm_[kQuadW * kQuadH];
  float bandSmooth_[NUM_BANDS] = {};
  float rotation_ = 0;
  float paletteOffset_ = 0;
  float flash_ = 0;
};

REGISTER_VISUALIZER(Kaleidoscope, 60);
