#include "Audio.h"

#include <Arduino.h>
#include <arduinoFFT.h>
#include <esp_adc/adc_continuous.h>

#include <algorithm>

#include "Color.h"

// How it works: the ADC's DMA engine samples AUDIO and ENVELOPE continuously
// into memory. A task on core 0 collects the samples and, every
// AUDIO_HOP_SAMPLES new ones, analyses the most recent FFT_SAMPLES (so windows
// overlap). Each result is published for the render loop on core 1 to pick up
// whenever it draws, so neither side waits on the other.

namespace {

// ---- Analysis state (only touched by the audio task) ------------------------

float vReal[FFT_SAMPLES];
float vImag[FFT_SAMPLES];
ArduinoFFT<float> fft(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE_HZ);

float window[FFT_SAMPLES];            // most recent samples, newest last
float hop[AUDIO_HOP_SAMPLES];         // samples collected since the last analysis
int hopFill = 0;
uint32_t envelopeSum = 0;
uint32_t envelopeCount = 0;

uint16_t bandStart[NUM_BANDS + 1];  // first FFT bin of each band
float spectrumGain = 1.0f;          // tracks the loudest recent band
float waveGain = 1.0f;              // tracks the largest recent sample
AudioFrame working;                 // the frame being built
float bandDb[NUM_BANDS];            // raw dB of each band
float noiseFloorDb[NUM_BANDS];      // learned noise level of each band
bool noiseFloorReady = false;
float spanDb = LOG_MIN_SPAN_DB;     // dB above the floor that maps to 1.0

// Per-analysis-frame rates, derived from time constants in begin().
float gainDecay, bassAverageDecay, floorFall, floorRise;

// ---- Shared with the render loop --------------------------------------------

portMUX_TYPE frameLock = portMUX_INITIALIZER_UNLOCKED;
AudioFrame published;      // newest complete frame
bool beatPending = false;  // a beat landed since the render loop last looked

// Measured rates, for printStats().
uint32_t rateWindowStartMs = 0, samplesInWindow = 0, analysesInWindow = 0;
float measuredSampleRate = 0, measuredAnalysisRate = 0;

// ---- ADC --------------------------------------------------------------------

constexpr int kAdcChannels = 2;  // AUDIO and ENVELOPE
constexpr uint32_t kReadBytes = AUDIO_HOP_SAMPLES * kAdcChannels * SOC_ADC_DIGI_RESULT_BYTES;
adc_continuous_handle_t adc = nullptr;
adc_channel_t audioChannel, envelopeChannel;

void startAdc() {
  adc_unit_t unit;
  ESP_ERROR_CHECK(adc_continuous_io_to_channel(Pins::AUDIO, &unit, &audioChannel));
  ESP_ERROR_CHECK(adc_continuous_io_to_channel(Pins::ENVELOPE, &unit, &envelopeChannel));

  adc_continuous_handle_cfg_t handleConfig = {};
  handleConfig.max_store_buf_size = kReadBytes * 4;
  handleConfig.conv_frame_size = kReadBytes;
  ESP_ERROR_CHECK(adc_continuous_new_handle(&handleConfig, &adc));

  adc_digi_pattern_config_t pattern[kAdcChannels] = {};
  const adc_channel_t channels[kAdcChannels] = {audioChannel, envelopeChannel};
  for (int i = 0; i < kAdcChannels; i++) {
    pattern[i].atten = ADC_ATTEN_DB_12;  // ~0-3.3 V input range
    pattern[i].channel = channels[i];
    pattern[i].unit = ADC_UNIT_1;
    pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  }

  adc_continuous_config_t config = {};
  config.pattern_num = kAdcChannels;
  config.adc_pattern = pattern;
  config.sample_freq_hz = SAMPLE_RATE_HZ * kAdcChannels;  // conversions/s across both pins
  config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
  ESP_ERROR_CHECK(adc_continuous_config(adc, &config));
  ESP_ERROR_CHECK(adc_continuous_start(adc));
}

// ---- Analysis ---------------------------------------------------------------

// Per-frame multiplier that decays to 1/e after `seconds`.
float decayPerFrame(float seconds) { return expf(-1.0f / (seconds * ANALYSIS_RATE_HZ)); }

// Spread FFT bins across bands logarithmically so bass isn't squashed into a
// couple of bands.
void setupBands() {
  const float lo = FFT_MIN_BIN;
  const float hi = FFT_SAMPLES / 2;
  for (int i = 0; i <= NUM_BANDS; i++) {
    bandStart[i] = (uint16_t)(lo * powf(hi / lo, (float)i / NUM_BANDS));
  }
  // Guarantee each band covers at least one bin.
  for (int i = 1; i <= NUM_BANDS; i++) {
    if (bandStart[i] <= bandStart[i - 1]) bandStart[i] = bandStart[i - 1] + 1;
  }
  if (bandStart[NUM_BANDS] > FFT_SAMPLES / 2) bandStart[NUM_BANDS] = FFT_SAMPLES / 2;
}

// Jump up on loud input, decay slowly in quiet passages, never drop below
// `floor` so silence doesn't get amplified into noise.
void trackGain(float& gain, float loudest, float floor) {
  if (loudest > gain) gain = loudest;
  else gain = max(gain * gainDecay, floor);
}

void loadWindow() {
  // Remove the DC bias (AUDIO idles around VCC/2).
  float mean = 0;
  for (int i = 0; i < FFT_SAMPLES; i++) mean += window[i];
  mean /= FFT_SAMPLES;
  for (int i = 0; i < FFT_SAMPLES; i++) {
    vReal[i] = window[i] - mean;
    vImag[i] = 0;
  }
}

void buildWaveform() {
  float peak = 0;
  for (int i = 0; i < FFT_SAMPLES; i++) peak = max(peak, fabsf(vReal[i]));
  trackGain(waveGain, peak, 40.0f);

  float sumSq = 0;
  for (int i = 0; i < FFT_SAMPLES; i++) {
    float s = constrain(vReal[i] / waveGain, -1.0f, 1.0f);
    working.waveform[i] = s;
    sumSq += s * s;
  }
  // A full-scale sine has an RMS of 1/sqrt(2); scale so that reads as 1.
  working.level = min(sqrtf(sumSq / FFT_SAMPLES) * 1.414f, 1.0f);
}

// Decibel bands: how far each band is above its own learned noise floor.
void buildBandsDb() {
  float loudest = 0;
  float above[NUM_BANDS];
  for (int i = 0; i < NUM_BANDS; i++) {
    float& floorDb = noiseFloorDb[i];
    if (!noiseFloorReady) floorDb = bandDb[i];
    else floorDb += (bandDb[i] - floorDb) * (bandDb[i] < floorDb ? floorFall : floorRise);

    above[i] = bandDb[i] - floorDb - LOG_FLOOR_MARGIN_DB;
    loudest = max(loudest, above[i]);
  }
  noiseFloorReady = true;

  // Full height tracks the loudest band: jump up, decay slowly.
  spanDb = loudest > spanDb ? loudest : spanDb * gainDecay;
  spanDb = constrain(spanDb, LOG_MIN_SPAN_DB, LOG_MAX_SPAN_DB);

  for (int i = 0; i < NUM_BANDS; i++) working.bandsDb[i] = clamp01(above[i] / spanDb);
}

void buildBands() {
  float loudest = 0;
  for (int i = 0; i < NUM_BANDS; i++) {
    float sum = 0;
    int n = 0;
    for (int b = bandStart[i]; b < bandStart[i + 1] && b < FFT_SAMPLES / 2; b++) {
      sum += vReal[b];
      n++;
    }
    working.bands[i] = n ? sum / n : 0;
    loudest = max(loudest, working.bands[i]);
  }

  trackGain(spectrumGain, loudest, 50.0f);

  for (int i = 0; i < NUM_BANDS; i++) {
    bandDb[i] = 20.0f * log10f(max(working.bands[i], 1e-3f));
    working.bands[i] = min(working.bands[i] / spectrumGain, 1.0f);
  }
  buildBandsDb();
}

// A beat is bass energy jumping well above its recent average.
void detectBeat() {
  constexpr int bassBands = 6;
  constexpr uint32_t minMsBetweenBeats = 120;
  static float bassAverage = 0;
  static uint32_t lastBeatMs = 0;

  float bass = 0;
  for (int i = 0; i < bassBands; i++) bass += working.bands[i];
  working.bass = bass / bassBands;

  uint32_t now = millis();
  working.beat = working.bass > bassAverage * 1.4f && working.bass > 0.35f &&
                 now - lastBeatMs > minMsBetweenBeats;
  if (working.beat) lastBeatMs = now;
  bassAverage = bassAverage * bassAverageDecay + working.bass * (1 - bassAverageDecay);
}

void publish() {
  portENTER_CRITICAL(&frameLock);
  published = working;
  beatPending |= working.beat;
  portEXIT_CRITICAL(&frameLock);
}

void measureRates() {
  analysesInWindow++;
  uint32_t elapsed = millis() - rateWindowStartMs;
  if (elapsed >= 1000) {
    measuredSampleRate = samplesInWindow * 1000.0f / elapsed;
    measuredAnalysisRate = analysesInWindow * 1000.0f / elapsed;
    samplesInWindow = analysesInWindow = 0;
    rateWindowStartMs += elapsed;
  }
}

void analyse() {
  loadWindow();
  buildWaveform();  // before the FFT overwrites vReal

  fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  fft.compute(FFTDirection::Forward);
  fft.complexToMagnitude();
  buildBands();

  working.frame++;
  detectBeat();
  if (envelopeCount) working.envelope = envelopeSum / (envelopeCount * 4095.0f);
  envelopeSum = envelopeCount = 0;
  working.gate = digitalRead(Pins::GATE);

  publish();
  measureRates();
}

void addAudioSample(uint16_t raw) {
  samplesInWindow++;
  hop[hopFill++] = raw;
  if (hopFill < AUDIO_HOP_SAMPLES) return;

  // Slide the window along by one hop and analyse it.
  memmove(window, window + AUDIO_HOP_SAMPLES, (FFT_SAMPLES - AUDIO_HOP_SAMPLES) * sizeof(float));
  memcpy(window + FFT_SAMPLES - AUDIO_HOP_SAMPLES, hop, AUDIO_HOP_SAMPLES * sizeof(float));
  hopFill = 0;
  analyse();
}

void audioTask(void*) {
  static uint8_t buffer[kReadBytes];
  rateWindowStartMs = millis();
  while (true) {
    uint32_t length = 0;
    if (adc_continuous_read(adc, buffer, kReadBytes, &length, 1000) != ESP_OK) continue;

    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= length; i += SOC_ADC_DIGI_RESULT_BYTES) {
      auto* result = reinterpret_cast<adc_digi_output_data_t*>(&buffer[i]);
      if (result->type1.channel == audioChannel) {
        addAudioSample(result->type1.data);
      } else if (result->type1.channel == envelopeChannel) {
        envelopeSum += result->type1.data;
        envelopeCount++;
      }
    }
  }
}

}  // namespace

void Audio::begin() {
  pinMode(Pins::GATE, INPUT);
  setupBands();

  gainDecay = decayPerFrame(4.0f);
  bassAverageDecay = decayPerFrame(0.4f);
  floorFall = 1 - decayPerFrame(LOG_FLOOR_FALL_S);
  floorRise = 1 - decayPerFrame(LOG_FLOOR_RISE_S);

  startAdc();
  // Core 0, so it never competes with the render loop on core 1.
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 5, nullptr, 0);
}

void Audio::latest(AudioFrame& out) {
  portENTER_CRITICAL(&frameLock);
  out = published;
  out.beat = beatPending;
  beatPending = false;
  portEXIT_CRITICAL(&frameLock);
}

void Audio::printStats() {
  // Reads the audio task's state without locking; fine for a rough printout.
  float height[NUM_BANDS], aboveFloor[NUM_BANDS];
  for (int i = 0; i < NUM_BANDS; i++) {
    height[i] = published.bandsDb[i];
    aboveFloor[i] = bandDb[i] - noiseFloorDb[i];
  }
  std::sort(height, height + NUM_BANDS);
  std::sort(aboveFloor, aboveFloor + NUM_BANDS);
  Serial.printf("audio %.0f samples/s, %.1f analyses/s\n", measuredSampleRate, measuredAnalysisRate);
  Serial.printf("dB above floor: median %.1f  max %.1f | span %.1f dB | height median %.2f  max %.2f | level %.2f\n",
                aboveFloor[NUM_BANDS / 2], aboveFloor[NUM_BANDS - 1], spanDb,
                height[NUM_BANDS / 2], height[NUM_BANDS - 1], published.level);
}

void Audio::printBands() {
  // Reads the audio task's state without locking; fine for a rough printout.
  for (int i = 0; i < NUM_BANDS; i++) {
    float hz = bandStart[i] * (float)SAMPLE_RATE_HZ / FFT_SAMPLES;
    Serial.printf("%2d %5.0f Hz %5.1f dB  floor %5.1f\n", i, hz, bandDb[i], noiseFloorDb[i]);
  }
}
