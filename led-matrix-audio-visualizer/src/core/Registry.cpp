#include "Visualizer.h"

namespace {

struct Entry {
  Visualizer* visualizer;
  int order;
};

// Plain zero-initialised arrays rather than a container: visualizers register
// from static constructors in other files, and zero-initialisation is
// guaranteed to have happened before any of those run.
constexpr size_t MAX_VISUALIZERS = 32;
Entry entries[MAX_VISUALIZERS];
size_t entryCount;

}  // namespace

size_t Visualizers::count() { return entryCount; }

Visualizer* Visualizers::get(size_t index) {
  return index < entryCount ? entries[index].visualizer : nullptr;
}

void Visualizers::add(Visualizer* visualizer, int order) {
  if (entryCount == MAX_VISUALIZERS) return;
  // Insertion sort, so the list is ordered regardless of link order.
  size_t i = entryCount++;
  while (i > 0 && entries[i - 1].order > order) {
    entries[i] = entries[i - 1];
    i--;
  }
  entries[i] = {visualizer, order};
}
