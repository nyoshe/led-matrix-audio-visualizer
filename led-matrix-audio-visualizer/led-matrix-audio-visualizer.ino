// Audio spectrum visualizer
//   ESP32 + Waveshare 64x32 RGB (HUB75) LED matrix + SparkFun Sound Detector
//
// The Sound Detector's AUDIO output is sampled with the ADC, run through an
// FFT and drawn as 64 log-spaced frequency bars with falling peak markers.

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <arduinoFFT.h>

// ---- Matrix -----------------------------------------------------------------
#define PANEL_WIDTH  64
#define PANEL_HEIGHT 32
#define PANEL_CHAIN  1

// HUB75 wiring (these are the library's defaults for the classic ESP32).
// E is unused on a 1/16 scan 32-row panel.
#define R1_PIN  25
#define G1_PIN  26
#define B1_PIN  27
#define R2_PIN  14
#define G2_PIN  12
#define B2_PIN  13
#define A_PIN   23
#define B_PIN   19
#define C_PIN    5
#define D_PIN   17
#define E_PIN   -1
#define LAT_PIN  4
#define OE_PIN  15
#define CLK_PIN 16

// ---- Sound detector ---------------------------------------------------------
// Use ADC1 pins only (ADC2 is unavailable while WiFi is active).
#define AUDIO_PIN    34  // AUDIO    -> raw waveform, biased at VCC/2
#define ENVELOPE_PIN 35  // ENVELOPE -> amplitude envelope
#define GATE_PIN     32  // GATE     -> high when sound exceeds threshold

// ---- FFT --------------------------------------------------------------------
#define SAMPLES        256      // must be a power of 2
#define SAMPLE_RATE_HZ 16000    // Nyquist = 8 kHz, bin width = 62.5 Hz
#define MIN_BIN        2        // skip DC and the lowest rumble

MatrixPanel_I2S_DMA *matrix = nullptr;

float vReal[SAMPLES];
float vImag[SAMPLES];
ArduinoFFT<float> FFT(vReal, vImag, SAMPLES, SAMPLE_RATE_HZ);

uint16_t bandStart[PANEL_WIDTH + 1];  // first FFT bin of each column
float barLevel[PANEL_WIDTH];          // smoothed bar height, in pixels
float peakLevel[PANEL_WIDTH];         // falling peak marker, in pixels
float autoGain = 1.0f;                // tracks the loudest recent band

const uint32_t samplePeriodUs = 1000000UL / SAMPLE_RATE_HZ;

void setupMatrix() {
  HUB75_I2S_CFG::i2s_pins pins = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN,
                                  A_PIN,  B_PIN,  C_PIN,  D_PIN,  E_PIN,
                                  LAT_PIN, OE_PIN, CLK_PIN};
  HUB75_I2S_CFG cfg(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN, pins);
  cfg.double_buff = true;  // draw off-screen, then flip: no flicker
  // Some Waveshare panels use an FM6126A driver; if yours stays dark, try:
  // cfg.driver = HUB75_I2S_CFG::FM6126A;
  // If colours/pixels shimmer, try: cfg.clkphase = false;

  matrix = new MatrixPanel_I2S_DMA(cfg);
  if (!matrix->begin()) {
    Serial.println("Matrix init failed (not enough DMA memory?)");
    while (true) delay(1000);
  }
  matrix->setBrightness8(96);  // 0-255
  matrix->clearScreen();
}

// Spread FFT bins across columns logarithmically so bass isn't squashed into
// a couple of pixels.
void setupBands() {
  const float lo = MIN_BIN;
  const float hi = SAMPLES / 2;
  for (int x = 0; x <= PANEL_WIDTH; x++) {
    float f = lo * powf(hi / lo, (float)x / PANEL_WIDTH);
    bandStart[x] = (uint16_t)f;
  }
  // Guarantee each column covers at least one bin.
  for (int x = 1; x <= PANEL_WIDTH; x++) {
    if (bandStart[x] <= bandStart[x - 1]) bandStart[x] = bandStart[x - 1] + 1;
  }
  if (bandStart[PANEL_WIDTH] > SAMPLES / 2) bandStart[PANEL_WIDTH] = SAMPLES / 2;
}

void sampleAudio() {
  uint32_t next = micros();
  for (int i = 0; i < SAMPLES; i++) {
    while ((int32_t)(micros() - next) < 0) {}
    next += samplePeriodUs;
    vReal[i] = analogRead(AUDIO_PIN);
    vImag[i] = 0;
  }

  // Remove the DC bias (AUDIO idles around VCC/2).
  float mean = 0;
  for (int i = 0; i < SAMPLES; i++) mean += vReal[i];
  mean /= SAMPLES;
  for (int i = 0; i < SAMPLES; i++) vReal[i] -= mean;
}

uint16_t barColor(int y) {
  // y = 0 is the bottom row: green -> yellow -> red going up.
  float t = (float)y / (PANEL_HEIGHT - 1);
  uint8_t r = t < 0.5f ? (uint8_t)(t * 2 * 255) : 255;
  uint8_t g = t < 0.5f ? 255 : (uint8_t)((1 - t) * 2 * 255);
  return matrix->color565(r, g, 0);
}

void drawSpectrum() {
  float bands[PANEL_WIDTH];
  float loudest = 0;

  for (int x = 0; x < PANEL_WIDTH; x++) {
    float sum = 0;
    int n = 0;
    for (int b = bandStart[x]; b < bandStart[x + 1] && b < SAMPLES / 2; b++) {
      sum += vReal[b];
      n++;
    }
    bands[x] = n ? sum / n : 0;
    if (bands[x] > loudest) loudest = bands[x];
  }

  // Automatic gain: jump up on loud input, decay slowly in quiet passages.
  if (loudest > autoGain) autoGain = loudest;
  else autoGain = max(autoGain * 0.995f, 50.0f);

  matrix->fillScreen(0);

  for (int x = 0; x < PANEL_WIDTH; x++) {
    float target = bands[x] / autoGain * PANEL_HEIGHT;
    if (target > PANEL_HEIGHT) target = PANEL_HEIGHT;

    // Fast attack, slower release.
    barLevel[x] = target > barLevel[x] ? target : barLevel[x] * 0.85f + target * 0.15f;

    if (barLevel[x] >= peakLevel[x]) peakLevel[x] = barLevel[x];
    else peakLevel[x] = max(peakLevel[x] - 0.4f, 0.0f);

    int h = (int)barLevel[x];
    for (int y = 0; y < h; y++) {
      matrix->drawPixel(x, PANEL_HEIGHT - 1 - y, barColor(y));
    }

    int p = (int)peakLevel[x];
    if (p > 0) {
      matrix->drawPixel(x, PANEL_HEIGHT - min(p, PANEL_HEIGHT), matrix->color565(255, 255, 255));
    }
  }

  matrix->flipDMABuffer();
}

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetPinAttenuation(AUDIO_PIN, ADC_11db);  // ~0-3.3 V input range
  pinMode(GATE_PIN, INPUT);

  setupMatrix();
  setupBands();
  Serial.println("Audio visualizer running");
}

void loop() {
  sampleAudio();
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();
  drawSpectrum();
}
