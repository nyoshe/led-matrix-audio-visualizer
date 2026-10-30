#include "Palette.h"

#include <Preferences.h>
#include <math.h>

#include "Color.h"

namespace {

struct ColorStop {
  float pos;  // 0..1 along the gradient
  uint8_t r, g, b;
};

struct Palette {
  const char* name;
  const ColorStop* stops;
  int count;
};

// Every palette starts and ends on the same colour so it loops seamlessly.
const ColorStop kSynthwave[] = {
    {0.00f, 0x5a, 0x10, 0xc8}, {0.30f, 0xff, 0x2a, 0x6d}, {0.50f, 0xff, 0x9e, 0x3d},
    {0.70f, 0x05, 0xd9, 0xe8}, {1.00f, 0x5a, 0x10, 0xc8}};
const ColorStop kSunset[] = {
    {0.00f, 0x6a, 0x1b, 0x9a}, {0.30f, 0xe0, 0x1e, 0x5a}, {0.55f, 0xff, 0x6f, 0x00},
    {0.75f, 0xff, 0xc8, 0x3c}, {1.00f, 0x6a, 0x1b, 0x9a}};
const ColorStop kOcean[] = {
    {0.00f, 0x00, 0x2a, 0x8c}, {0.35f, 0x00, 0x9e, 0xb0}, {0.55f, 0x5c, 0xff, 0xd0},
    {0.80f, 0x00, 0x5f, 0xd0}, {1.00f, 0x00, 0x2a, 0x8c}};
const ColorStop kAurora[] = {
    {0.00f, 0x00, 0xff, 0x87}, {0.35f, 0x00, 0xc3, 0xff}, {0.65f, 0x7b, 0x2f, 0xf7},
    {0.85f, 0xc8, 0x3c, 0xd2}, {1.00f, 0x00, 0xff, 0x87}};
const ColorStop kLava[] = {
    {0.00f, 0x8c, 0x00, 0x00}, {0.30f, 0xff, 0x20, 0x00}, {0.55f, 0xff, 0x8c, 0x00},
    {0.75f, 0xff, 0xd2, 0x1e}, {1.00f, 0x8c, 0x00, 0x00}};
const ColorStop kIce[] = {
    {0.00f, 0x1e, 0x2d, 0xa0}, {0.35f, 0x1e, 0x90, 0xff}, {0.60f, 0x9a, 0xe6, 0xff},
    {0.80f, 0x6a, 0x5a, 0xe6}, {1.00f, 0x1e, 0x2d, 0xa0}};
const ColorStop kRainbow[] = {
    {0.000f, 0xff, 0x00, 0x00}, {0.167f, 0xff, 0xff, 0x00}, {0.333f, 0x00, 0xff, 0x00},
    {0.500f, 0x00, 0xff, 0xff}, {0.667f, 0x00, 0x00, 0xff}, {0.833f, 0xff, 0x00, 0xff},
    {1.000f, 0xff, 0x00, 0x00}};

#define PALETTE(name, stops) {name, stops, sizeof(stops) / sizeof(stops[0])}
const Palette kPalettes[] = {
    PALETTE("Synthwave", kSynthwave), PALETTE("Sunset", kSunset), PALETTE("Ocean", kOcean),
    PALETTE("Aurora", kAurora),       PALETTE("Lava", kLava),     PALETTE("Ice", kIce),
    PALETTE("Rainbow", kRainbow),
};
constexpr size_t kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

size_t currentIndex = 0;
uint8_t lut[256][3];  // the current palette, sampled at 256 points

// Blend in linear light (squaring approximates sRGB's gamma), which keeps
// midpoints bright instead of muddy.
uint8_t mix(uint8_t a, uint8_t b, float f) {
  float la = a / 255.0f, lb = b / 255.0f;
  float l = la * la + (lb * lb - la * la) * f;
  return (uint8_t)(sqrtf(l) * 255 + 0.5f);
}

void buildLut(const Palette& p) {
  int s = 0;
  for (int i = 0; i < 256; i++) {
    float t = i / 256.0f;
    while (s < p.count - 2 && t >= p.stops[s + 1].pos) s++;
    const ColorStop& a = p.stops[s];
    const ColorStop& b = p.stops[s + 1];
    float f = (t - a.pos) / (b.pos - a.pos);
    lut[i][0] = mix(a.r, b.r, f);
    lut[i][1] = mix(a.g, b.g, f);
    lut[i][2] = mix(a.b, b.b, f);
  }
}

}  // namespace

void Palettes::begin() {
  Preferences prefs;
  prefs.begin("display", true);
  currentIndex = prefs.getUInt("palette", 0) % kPaletteCount;
  prefs.end();
  buildLut(kPalettes[currentIndex]);
}

size_t Palettes::count() { return kPaletteCount; }

const char* Palettes::name(size_t index) { return kPalettes[index % kPaletteCount].name; }

size_t Palettes::current() { return currentIndex; }

void Palettes::select(size_t index) {
  currentIndex = index % kPaletteCount;
  buildLut(kPalettes[currentIndex]);
  Preferences prefs;
  prefs.begin("display");
  prefs.putUInt("palette", currentIndex);
  prefs.end();
}

uint16_t paletteColor(float t, float brightness, float white) {
  t -= floorf(t);
  const uint8_t* c = lut[(int)(t * 256) & 255];
  brightness = clamp01(brightness);
  white = clamp01(white);
  uint8_t rgb[3];
  for (int i = 0; i < 3; i++) {
    float v = c[i] + (255 - c[i]) * white;
    rgb[i] = (uint8_t)(v * brightness);
  }
  return rgb565(rgb[0], rgb[1], rgb[2]);
}
