#pragma once

#include <stddef.h>

#include "../config.h"
#include "AudioFrame.h"
#include "Color.h"
#include "Matrix.h"

// Base class for every visualizer.
//
// To add one, create a .cpp file in src/visualizers/, subclass Visualizer and
// finish the file with REGISTER_VISUALIZER(YourClass, order). Nothing else
// needs to change: it shows up in the rotation automatically.
class Visualizer {
 public:
  virtual ~Visualizer() = default;

  // Shown on the serial console when the visualizer is selected.
  virtual const char* name() const = 0;

  // Called each time this visualizer becomes active; clear any history here.
  virtual void reset() {}

  // Draw one frame; called RENDER_FPS times a second with the newest audio.
  // The screen has already been cleared to black and is flipped onto the
  // panel afterwards, so draw the whole picture every time and keep any
  // persistence (trails, history) in member variables.
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
