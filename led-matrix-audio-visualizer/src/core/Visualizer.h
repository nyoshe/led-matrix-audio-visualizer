#pragma once

#include <stddef.h>

#include "../config.h"
#include "AudioFrame.h"
#include "Color.h"
#include "Matrix.h"
#include "Palette.h"

// Base class for every visualizer. To add one, subclass it in a new
// src/visualizers/*.cpp file ending with REGISTER_VISUALIZER(YourClass, order).
class Visualizer {
 public:
  virtual ~Visualizer() = default;

  // Shown on the serial console when the visualizer is selected.
  virtual const char* name() const = 0;

  // Called each time this visualizer becomes active; clear any history here.
  virtual void reset() {}

  // Draw one frame at RENDER_FPS; screen is pre-cleared, flipped after. Draw
  // everything each call -- keep persistence (trails, history) in members.
  virtual void draw(const AudioFrame& audio, Display& display) = 0;
};

namespace Visualizers {
// Registered visualizers, sorted by their `order`.
size_t count();
Visualizer* get(size_t index);
void add(Visualizer* visualizer, int order);
}  // namespace Visualizers

struct VisualizerRegistrar {
  VisualizerRegistrar(Visualizer* visualizer, int order) { Visualizers::add(visualizer, order); }
};

// Creates a single static instance of `Type` and adds it to the rotation.
// Lower `order` values come first.
#define REGISTER_VISUALIZER(Type, order) \
  static Type Type##Instance;            \
  static VisualizerRegistrar Type##Registrar(&Type##Instance, order)
