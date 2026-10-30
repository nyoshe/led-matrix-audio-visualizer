// Twin analog VU meters, since the input is mono: the panel is split into two
// 32x32 quadrants, each a classic swinging-needle gauge with a scale arc,
// tick marks, a red zone at the top of the scale and a peak-hold lamp.
//
// LEFT gauge is driven by audio.bass (the low end); RIGHT gauge by the
// average of the top half of audio.bands (the high end). Each needle snaps
// towards its target fast (attack) but eases back slowly (release), and
// springs a little past its target before settling, like a real meter's
// mechanical inertia. The arc itself is coloured green -> yellow -> red along
// the scale (levelColor), so "in the red zone" always means visually red;
// the needle stays plain white so it reads clearly against the coloured arc.
// Each gauge's peak lamp glows red while its needle sits in the red zone and
// flashes towards white on every detected beat.

#include <math.h>

#include "../core/Visualizer.h"

class VuMeter : public Visualizer {
 public:
  const char* name() const override { return "VU Meter"; }

  void reset() override {
    left_ = Channel();
    right_ = Channel();
  }

  void draw(const AudioFrame& audio, Display& display) override {
    float treble = 0;
    for (int i = NUM_BANDS / 2; i < NUM_BANDS; i++) treble += audio.bands[i];
    treble /= (NUM_BANDS / 2);

    drawGauge(display, left_, audio.bass, audio.beat, 0, PANEL_WIDTH / 2);
    drawGauge(display, right_, treble, audio.beat, PANEL_WIDTH / 2, PANEL_WIDTH);
  }

 private:
  // Per-gauge state: the EWMA-smoothed driving signal, the spring-damped
  // needle position that chases it (allowing a little overshoot), and the
  // two lamp brightnesses (steady red-zone glow, brief white beat flash).
  struct Channel {
    float smooth = 0;
    float needle = 0;
    float needleVel = 0;
    float redLamp = 0;
    float flashLamp = 0;
  };

  void drawGauge(Display& display, Channel& ch, float input, bool beat, int xMin, int xMax) {
    input = clamp01(input);

    // Fast attack, slow release: hits register immediately, decay eases off.
    float alpha = input > ch.smooth ? kAttackAlpha : kReleaseAlpha;
    ch.smooth += alpha * (input - ch.smooth);

    // Spring-damped needle chasing the smoothed value: light underdamping
    // gives a small overshoot/wobble before it settles, like real inertia.
    float accel = kSpringStiffness * (ch.smooth - ch.needle) - kSpringDamping * ch.needleVel;
    ch.needleVel += accel;
    ch.needle += ch.needleVel;
    // Clamp the physics state loosely so a hard hit can't make it diverge.
    if (ch.needle < -0.2f) { ch.needle = -0.2f; ch.needleVel = 0; }
    if (ch.needle > 1.2f) { ch.needle = 1.2f; ch.needleVel = 0; }

    // The displayed needle has a mechanical stop at each end of the scale.
    const float pos = clamp01(ch.needle);
    const bool redZone = pos > kRedZoneStart;

    const int cx = (xMin + xMax) / 2;
    const int py = PANEL_HEIGHT - 1;  // pivot sits on the bottom row

    // Scale arc + tick marks: green -> yellow -> red, matching the VU scale.
    for (int d = -kSweepDeg; d <= kSweepDeg; d += 2) {
      float t = (float)(d + kSweepDeg) / (2 * kSweepDeg);  // 0..1 along the scale
      float rad = d * kDegToRad;
      int ax = cx + (int)(kRadius * sinf(rad));
      int ay = py - (int)(kRadius * cosf(rad));
      display.drawPixel(ax, ay, levelColor(t));
    }
    for (int i = 0; i <= kNumTicks; i++) {
      float t = (float)i / kNumTicks;
      float rad = (t * 2 - 1) * kSweepDeg * kDegToRad;
      for (int r = kRadius - 3; r <= kRadius; r++) {
        int tx = cx + (int)(r * sinf(rad));
        int ty = py - (int)(r * cosf(rad));
        display.drawPixel(tx, ty, levelColor(t));
      }
    }

    // Needle: plain white, pivoting from the bottom-centre of this half.
    float angleDeg = (pos - 0.5f) * 2.0f * kSweepDeg;
    float rad = angleDeg * kDegToRad;
    int nx = cx + (int)((kRadius - 2) * sinf(rad));
    int ny = py - (int)((kRadius - 2) * cosf(rad));
    display.drawLine(cx, py, nx, ny, rgb565(255, 255, 255));

    // Peak-hold lamp above the apex of the arc: fast on, slow off while in
    // the red zone, plus a brief white flash whenever a beat lands.
    ch.redLamp += redZone ? (1.0f - ch.redLamp) * 0.6f : -0.05f;
    ch.redLamp = clamp01(ch.redLamp);
    ch.flashLamp = beat ? 1.0f : ch.flashLamp * 0.75f;

    float lit = max(ch.redLamp, ch.flashLamp);
    if (lit > 0.05f) {
      uint8_t glow = (uint8_t)(220 * ch.flashLamp);
      display.fillCircle(cx, py - kRadius - 3, 1, rgb565(255, glow, glow));
    }
  }

  static constexpr float kAttackAlpha     = 0.6f;    // ~50ms-ish rise
  static constexpr float kReleaseAlpha    = 0.12f;   // ~300ms-ish fall
  static constexpr float kSpringStiffness = 0.3f;    // needle spring constant
  static constexpr float kSpringDamping   = 0.6f;    // lightly underdamped (wobble)
  static constexpr int   kSweepDeg        = 50;      // needle swings +/- this many degrees
  static constexpr int   kRadius          = 20;       // scale arc radius, in pixels
  static constexpr int   kNumTicks        = 4;        // tick marks across the scale
  static constexpr float kRedZoneStart    = 0.85f;    // needle position where the red zone begins
  static constexpr float kDegToRad        = 3.14159265f / 180.0f;

  Channel left_;
  Channel right_;
};

REGISTER_VISUALIZER(VuMeter, 50);
