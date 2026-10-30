#pragma once

// Colour helpers shared by visualizers. All return RGB565.

#include <stdint.h>

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

inline float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }

// Green -> yellow -> red, like a VU meter.
inline uint16_t levelColor(float t) {
  t = clamp01(t);
  uint8_t r = t < 0.5f ? (uint8_t)(t * 2 * 255) : 255;
  uint8_t g = t < 0.5f ? 255 : (uint8_t)((1 - t) * 2 * 255);
  return rgb565(r, g, 0);
}

// Black -> blue -> magenta -> red -> yellow -> white, for intensity maps.
inline uint16_t heatColor(float t) {
  t = clamp01(t) * 4;
  if (t < 1) return rgb565(0, 0, (uint8_t)(t * 255));
  t -= 1;
  if (t < 1) return rgb565((uint8_t)(t * 255), 0, 255);
  t -= 1;
  if (t < 1) return rgb565(255, (uint8_t)(t * 255), (uint8_t)((1 - t) * 255));
  t -= 1;
  return rgb565(255, 255, (uint8_t)(t * 255));
}

// Hue in 0..1 around the colour wheel, full saturation.
inline uint16_t hueColor(float h, float brightness = 1.0f) {
  h = (h - (int)h) * 6;
  if (h < 0) h += 6;
  float f = h - (int)h;
  uint8_t v = (uint8_t)(clamp01(brightness) * 255);
  uint8_t q = (uint8_t)(v * (1 - f));
  uint8_t t = (uint8_t)(v * f);
  switch ((int)h) {
    case 0: return rgb565(v, t, 0);
    case 1: return rgb565(q, v, 0);
    case 2: return rgb565(0, v, t);
    case 3: return rgb565(0, q, v);
    case 4: return rgb565(t, 0, v);
    default: return rgb565(v, 0, q);
  }
}
