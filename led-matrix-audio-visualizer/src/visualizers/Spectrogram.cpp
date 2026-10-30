// Scrolling spectrogram ("waterfall"): time runs right to left, frequency runs
// bottom (bass) to top (treble), and colour shows loudness on a heat map.

#include "../core/Visualizer.h"

class Spectrogram : public Visualizer {
 public:
  Spectrogram() {
    for (int i = 0; i < 256; i++) palette_[i] = heatColor(i / 255.0f);
  }

  const char* name() const override { return "Spectrogram"; }

  void reset() override {
    memset(history_, 0, sizeof(history_));
    head_ = 0;
  }

  void draw(const AudioFrame& audio, Display& display) override {
    // Record the newest column: each row averages the bands it covers.
    constexpr int bandsPerRow = NUM_BANDS / PANEL_HEIGHT;
    uint8_t* column = history_[head_];
    for (int row = 0; row < PANEL_HEIGHT; row++) {
      float sum = 0;
      for (int b = 0; b < bandsPerRow; b++) sum += audio.bands[row * bandsPerRow + b];
      // A gamma below 1 lifts quiet detail out of the black.
      float v = powf(sum / bandsPerRow, 0.6f);
      column[row] = (uint8_t)(clamp01(v) * 255);
    }
    head_ = (head_ + 1) % PANEL_WIDTH;

    // Oldest column on the left, newest on the right.
    for (int x = 0; x < PANEL_WIDTH; x++) {
      const uint8_t* col = history_[(head_ + x) % PANEL_WIDTH];
      for (int row = 0; row < PANEL_HEIGHT; row++) {
        if (col[row]) display.drawPixel(x, PANEL_HEIGHT - 1 - row, palette_[col[row]]);
      }
    }
  }

 private:
  uint8_t history_[PANEL_WIDTH][PANEL_HEIGHT] = {};  // ring buffer of columns
  int head_ = 0;                                     // next column to write
  uint16_t palette_[256];
};

REGISTER_VISUALIZER(Spectrogram, 20);
