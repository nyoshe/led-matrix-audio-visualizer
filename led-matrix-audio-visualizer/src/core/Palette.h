#pragma once

// Colour palettes shared by visualizers. Each palette is a looping gradient,
// so a visualizer can scroll through it (t wraps around 0..1) without a seam.
// The current palette is switched over serial (`k`) and remembered across
// reboots.

#include <stddef.h>
#include <stdint.h>

namespace Palettes {
// Loads the saved choice; call once at startup.
void begin();
size_t count();
const char* name(size_t index);
size_t current();
// Switches palette (wraps around) and saves the choice to flash.
void select(size_t index);
}  // namespace Palettes

// Colour at position t (wraps around) in the current palette, as RGB565.
// `brightness` scales it (0..1); `white` blends it towards white (0..1), for
// highlights such as hot bar tips.
uint16_t paletteColor(float t, float brightness = 1.0f, float white = 0.0f);
