// Audio visualizer
//   ELEGOO ESP-WROOM-32 + Waveshare 64x32 RGB (HUB75) LED matrix
//   + SparkFun Sound Detector
//
// Captures audio, then hands each analysed frame to the active visualizer.
// Visualizers live in src/visualizers/ and register themselves; see
// src/core/Visualizer.h for how to add one.
//
// Switching visualizers:
//   - press the board's BOOT button for the next one
//   - over serial (115200): n = next, p = previous, l = list, 0-9 = select

#include "src/config.h"
#include "src/core/Audio.h"
#include "src/core/Matrix.h"
#include "src/core/Visualizer.h"

Display* display = nullptr;
size_t current = 0;
uint32_t lastSwitchMs = 0;

void selectVisualizer(size_t index) {
  size_t n = Visualizers::count();
  current = (index + n) % n;
  Visualizers::get(current)->reset();
  lastSwitchMs = millis();
  Serial.printf("[%u] %s\n", (unsigned)current, Visualizers::get(current)->name());
}

void listVisualizers() {
  for (size_t i = 0; i < Visualizers::count(); i++) {
    Serial.printf("%c %u: %s\n", i == current ? '*' : ' ', (unsigned)i, Visualizers::get(i)->name());
  }
}

void handleButton() {
  static bool wasPressed = false;
  static uint32_t lastChangeMs = 0;
  bool pressed = digitalRead(Pins::BUTTON) == LOW;
  if (pressed != wasPressed && millis() - lastChangeMs > 30) {  // debounce
    lastChangeMs = millis();
    wasPressed = pressed;
    if (pressed) selectVisualizer(current + 1);
  }
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'n') selectVisualizer(current + 1);
    else if (c == 'p') selectVisualizer(current + Visualizers::count() - 1);
    else if (c == 'l') listVisualizers();
    else if (c >= '0' && c <= '9' && (size_t)(c - '0') < Visualizers::count()) selectVisualizer(c - '0');
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(Pins::BUTTON, INPUT_PULLUP);

  display = &Matrix::begin();
  Audio::begin();

  if (Visualizers::count() == 0) {
    Serial.println("No visualizers registered");
    while (true) delay(1000);
  }
  Serial.println("Audio visualizer running");
  listVisualizers();
  selectVisualizer(0);
}

void loop() {
  handleButton();
  handleSerial();
  if (AUTO_CYCLE_MS && millis() - lastSwitchMs >= AUTO_CYCLE_MS) selectVisualizer(current + 1);

  const AudioFrame& audio = Audio::capture();
  display->fillScreen(0);
  Visualizers::get(current)->draw(audio, *display);
  display->flipDMABuffer();
}
