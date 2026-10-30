// Audio visualizer: ELEGOO ESP-WROOM-32 + Waveshare 64x32 HUB75 matrix +
// SparkFun Sound Detector. Audio is sampled/analysed on core 0
// (src/core/Audio); this loop draws the newest frame at RENDER_FPS on core 1.
// See README.md for the serial command reference, and src/core/Visualizer.h
// to add a visualizer.

#include "src/config.h"
#include "src/core/Audio.h"
#include "src/core/Matrix.h"
#include "src/core/Visualizer.h"

Display* display = nullptr;
size_t current = 0;
uint32_t lastSwitchMs = 0;
AudioFrame audio;           // newest frame from the audio task
float measuredRenderFps = 0;
bool calibrationReported = true;

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
    else if (c == 'd') {
      Serial.printf("render %.1f fps\n", measuredRenderFps);
      Audio::printStats();
    } else if (c == 'b') {
      Audio::printBands();
      Audio::printNoiseProfile();
    } else if (c == 'c') {
      Serial.println("Calibrating noise for 3 s, keep it quiet and don't touch the mic...");
      Audio::calibrateNoise(3.0f);
      calibrationReported = false;
    } else if (c == 'k') {
      Palettes::select(Palettes::current() + 1);
      Serial.printf("Palette: %s\n", Palettes::name(Palettes::current()));
    } else if (c == 'x') {
      Audio::clearNoiseCalibration();
      Serial.println("Noise calibration cleared.");
    } else if (c >= '0' && c <= '9' && (size_t)(c - '0') < Visualizers::count()) selectVisualizer(c - '0');
  }
}

void measureRenderFps() {
  static uint32_t windowStartMs = millis();
  static uint32_t frames = 0;
  frames++;
  uint32_t elapsed = millis() - windowStartMs;
  if (elapsed >= 1000) {
    measuredRenderFps = frames * 1000.0f / elapsed;
    frames = 0;
    windowStartMs += elapsed;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(Pins::BUTTON, INPUT_PULLUP);

  display = &Matrix::begin();
  Palettes::begin();
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
  if (!calibrationReported && !Audio::isCalibrating()) {
    calibrationReported = true;
    Serial.println("Noise calibration done and saved.");
    Audio::printNoiseProfile();
  }

  Audio::latest(audio);
  display->fillScreen(0);
  Visualizers::get(current)->draw(audio, *display);
  display->flipDMABuffer();

  measureRenderFps();
  // Hold a steady frame rate; audio keeps being analysed meanwhile.
  static TickType_t lastWake = xTaskGetTickCount();
  vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000 / RENDER_FPS));
}
