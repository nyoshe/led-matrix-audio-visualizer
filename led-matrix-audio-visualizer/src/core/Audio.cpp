#include "Audio.h"

#include <Arduino.h>
#include <arduinoFFT.h>
#include <Preferences.h>
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
float bandWeight[NUM_BANDS];        // treble boost applied to each band's magnitude
AudioFrame working;                 // the frame being built
float bandDb[NUM_BANDS];            // raw dB of each band
float noiseFloorDb[NUM_BANDS];      // learned noise level of each band
bool noiseFloorReady = false;
float spanDb = LOG_MIN_SPAN_DB;     // dB above the floor that maps to 1.0

// Per-analysis-frame rates, derived from time constants in begin().
float agcAttack, agcRelease, bassAverageDecay, floorFall, floorRise;

// ---- Noise calibration -----------------------------------------------------
// The quiet-room magnitude of every FFT bin (ADC noise plus steady hum and
// interference tones), measured on request and saved to flash.
// NOISE_OVERSUBTRACT times it is subtracted from each bin before the bins
// become bands, so silence draws (almost) nothing.
//
// Bumps and handling noise during calibration would inflate a plain average,
// so each bin is averaged over short blocks and the quietest block is kept.

constexpr int kBins = FFT_SAMPLES / 2;
constexpr uint32_t kCalibrationBlock = 16;  // frames per block (~0.1 s)
float noiseMag[kBins];                      // 0 until calibrated
float blockSum[kBins];
float quietestBlock[kBins];
uint32_t blockFrames = 0;
uint32_t calibrationFrames = 0;             // collected so far
uint32_t calibrationTarget = 0;             // frames wanted
volatile bool calibrating = false;

void saveCalibration() {
  Preferences prefs;
  prefs.begin("audio");
  prefs.putUInt("noiseRate", SAMPLE_RATE_HZ * ADC_OVERSAMPLE);
  prefs.putBytes("noiseMag", noiseMag, sizeof(noiseMag));
  prefs.end();
}

void collectCalibration() {
  for (int b = 0; b < kBins; b++) blockSum[b] += vReal[b];
  if (++blockFrames == kCalibrationBlock) {
    for (int b = 0; b < kBins; b++) {
      quietestBlock[b] = min(quietestBlock[b], blockSum[b] / kCalibrationBlock);
      blockSum[b] = 0;
    }
    blockFrames = 0;
  }
  if (++calibrationFrames < calibrationTarget) return;
  memcpy(noiseMag, quietestBlock, sizeof(noiseMag));
  calibrating = false;
  saveCalibration();
}

// Loads a saved calibration, if it was made with the current FFT and rates.
void loadCalibration() {
  Preferences prefs;
  prefs.begin("audio", true);
  if (prefs.getUInt("noiseRate", 0) == SAMPLE_RATE_HZ * ADC_OVERSAMPLE &&
      prefs.getBytesLength("noiseMag") == sizeof(noiseMag)) {
    prefs.getBytes("noiseMag", noiseMag, sizeof(noiseMag));
  }
  prefs.end();
}

// ---- Shared with the render loop --------------------------------------------

portMUX_TYPE frameLock = portMUX_INITIALIZER_UNLOCKED;
AudioFrame published;      // newest complete frame
bool beatPending = false;  // a beat landed since the render loop last looked

// Measured rates, for printStats().
uint32_t rateWindowStartMs = 0, samplesInWindow = 0, rawSamplesInWindow = 0, analysesInWindow = 0;
uint32_t busyUsInWindow = 0;  // time the audio task spent processing
float measuredRawSampleRate = 0, measuredBusyPercent = 0;
float measuredSampleRate = 0, measuredAnalysisRate = 0;
uint32_t analysisUsSum = 0, analysisUsMax = 0;      // time spent analysing, this window
float measuredAnalysisUsAvg = 0, measuredAnalysisUsMax = 0;

// ---- ADC --------------------------------------------------------------------

constexpr int kAdcChannels = 2;  // AUDIO and ENVELOPE
constexpr uint32_t kReadBytes = AUDIO_HOP_SAMPLES * ADC_OVERSAMPLE * kAdcChannels * SOC_ADC_DIGI_RESULT_BYTES;
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
  config.sample_freq_hz = SAMPLE_RATE_HZ * ADC_OVERSAMPLE * kAdcChannels;  // conversions/s, both pins
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
  const float hi = min(FFT_SAMPLES / 2.0f, FFT_MAX_HZ * FFT_SAMPLES / SAMPLE_RATE_HZ);
  for (int i = 0; i <= NUM_BANDS; i++) {
    bandStart[i] = (uint16_t)(lo * powf(hi / lo, (float)i / NUM_BANDS));
  }
  // Guarantee each band covers at least one bin.
  for (int i = 1; i <= NUM_BANDS; i++) {
    if (bandStart[i] <= bandStart[i - 1]) bandStart[i] = bandStart[i - 1] + 1;
  }
  if (bandStart[NUM_BANDS] > hi) bandStart[NUM_BANDS] = (uint16_t)hi;

  // Treble weighting: boost each band by TREBLE_TILT_DB_PER_OCTAVE for every
  // octave its centre frequency is above the lowest band's.
  auto centre = [](int i) { return (bandStart[i] + bandStart[i + 1] - 1) * 0.5f; };
  for (int i = 0; i < NUM_BANDS; i++) {
    float octaves = log2f(centre(i) / centre(0));
    bandWeight[i] = powf(10.0f, octaves * TREBLE_TILT_DB_PER_OCTAVE / 20.0f);
  }
}

// Automatic gain control: follows a level in dB with a fast attack and a
// slower release, so a loud spike only squashes the display briefly. Works in
// dB so recovering from a 20 dB spike takes the same time at any volume.
// Never follows below `floor`, so silence isn't amplified into noise.
struct GainFollower {
  float floor;
  float levelDb;

  explicit GainFollower(float floor) : floor(floor), levelDb(20.0f * log10f(floor)) {}

  // Returns the gain that puts the followed level at AGC_HEADROOM.
  float update(float level) {
    float db = 20.0f * log10f(max(level, floor));
    levelDb += (db - levelDb) * (db > levelDb ? agcAttack : agcRelease);
    return powf(10.0f, levelDb / 20.0f) / AGC_HEADROOM;
  }
};

GainFollower spectrumAgc(50.0f);  // loudest band
GainFollower waveAgc(40.0f);      // largest sample
float spectrumGain = 1.0f;
float waveGain = 1.0f;

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
  waveGain = waveAgc.update(peak);

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

  // Full height tracks the loudest band: jump up, ease back like the AGC.
  spanDb = loudest > spanDb ? loudest : spanDb + (loudest - spanDb) * agcRelease;
  spanDb = constrain(spanDb, LOG_MIN_SPAN_DB, LOG_MAX_SPAN_DB);

  for (int i = 0; i < NUM_BANDS; i++) working.bandsDb[i] = clamp01(above[i] / spanDb);
}

void buildBands() {
  float loudest = 0;
  for (int i = 0; i < NUM_BANDS; i++) {
    float sum = 0, cleanSum = 0;
    int n = 0;
    for (int b = bandStart[i]; b < bandStart[i + 1] && b < FFT_SAMPLES / 2; b++) {
      sum += vReal[b];
      cleanSum += max(vReal[b] - noiseMag[b] * NOISE_OVERSUBTRACT, 0.0f);
      n++;
    }
    // bandsDb learns its own noise floor, so it uses the uncorrected level.
    float magnitude = n ? sum / n : 0;
    bandDb[i] = 20.0f * log10f(max(magnitude, 1e-3f));
    working.bands[i] = (n ? cleanSum / n : 0) * bandWeight[i];
    loudest = max(loudest, working.bands[i]);
  }

  spectrumGain = spectrumAgc.update(loudest);

  for (int i = 0; i < NUM_BANDS; i++) working.bands[i] = min(working.bands[i] / spectrumGain, 1.0f);
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
    measuredRawSampleRate = rawSamplesInWindow * 1000.0f / elapsed;
    measuredBusyPercent = busyUsInWindow / (elapsed * 10.0f);
    rawSamplesInWindow = busyUsInWindow = 0;
    measuredAnalysisRate = analysesInWindow * 1000.0f / elapsed;
    measuredAnalysisUsAvg = (float)analysisUsSum / analysesInWindow;
    measuredAnalysisUsMax = analysisUsMax;
    analysisUsSum = analysisUsMax = 0;
    samplesInWindow = analysesInWindow = 0;
    rateWindowStartMs += elapsed;
  }
}

void analyse() {
  uint32_t startUs = micros();
  loadWindow();
  buildWaveform();  // before the FFT overwrites vReal

  fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  fft.compute(FFTDirection::Forward);
  fft.complexToMagnitude();
  if (calibrating) collectCalibration();
  buildBands();

  working.frame++;
  detectBeat();
  if (envelopeCount) working.envelope = envelopeSum / (envelopeCount * 4095.0f);
  envelopeSum = envelopeCount = 0;
  working.gate = digitalRead(Pins::GATE);

  publish();
  uint32_t tookUs = micros() - startUs;
  analysisUsSum += tookUs;
  analysisUsMax = max(analysisUsMax, tookUs);
  measureRates();
}

// The ESP32's DMA ADC adds a fixed offset pattern that repeats every 16
// samples at 20 kHz, i.e. every 0.8 ms (measured: one sample in 16 reads ~32
// counts low), which shows up as false tones at multiples of 1250 Hz. It
// survives oversampling, so it's corrected after averaging. Learn the average
// offset at each position in the cycle and subtract it. Real audio isn't
// locked to the cycle, so it averages out and only the fixed pattern is
// learned.
constexpr int kPatternPeriod = 16;
constexpr float kPatternLearnRate = 0.0005f;  // per sample at that position (~1.6 s)
float patternOffset[kPatternPeriod];
float dcLevel = 2048;
uint32_t sampleIndex = 0;

float removeAdcPattern(float x) {
  float& offset = patternOffset[sampleIndex++ % kPatternPeriod];
  dcLevel += (x - dcLevel) * 0.001f;
  offset += ((x - dcLevel) - offset) * kPatternLearnRate;
  return x - offset;
}

void addAudioSample(float sample) {
  samplesInWindow++;
  hop[hopFill++] = removeAdcPattern(sample);
  if (hopFill < AUDIO_HOP_SAMPLES) return;

  // Slide the window along by one hop and analyse it.
  memmove(window, window + AUDIO_HOP_SAMPLES, (FFT_SAMPLES - AUDIO_HOP_SAMPLES) * sizeof(float));
  memcpy(window + FFT_SAMPLES - AUDIO_HOP_SAMPLES, hop, AUDIO_HOP_SAMPLES * sizeof(float));
  hopFill = 0;
  analyse();
}

// Average every ADC_OVERSAMPLE raw readings into one audio sample.
void addRawAudioSample(uint16_t raw) {
  static float sum = 0;
  static int count = 0;
  rawSamplesInWindow++;
  sum += raw;
  if (++count < ADC_OVERSAMPLE) return;
  addAudioSample(sum / ADC_OVERSAMPLE);
  sum = 0;
  count = 0;
}

void audioTask(void*) {
  static uint8_t buffer[kReadBytes];
  rateWindowStartMs = millis();
  while (true) {
    uint32_t length = 0;
    if (adc_continuous_read(adc, buffer, kReadBytes, &length, 1000) != ESP_OK) continue;
    uint32_t startUs = micros();

    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= length; i += SOC_ADC_DIGI_RESULT_BYTES) {
      auto* result = reinterpret_cast<adc_digi_output_data_t*>(&buffer[i]);
      if (result->type1.channel == audioChannel) {
        addRawAudioSample(result->type1.data);
      } else if (result->type1.channel == envelopeChannel) {
        envelopeSum += result->type1.data;
        envelopeCount++;
      }
    }
    busyUsInWindow += micros() - startUs;
  }
}

}  // namespace

void Audio::begin() {
  pinMode(Pins::GATE, INPUT);
  setupBands();

  agcAttack = 1 - decayPerFrame(AGC_ATTACK_S);
  agcRelease = 1 - decayPerFrame(AGC_RELEASE_S);
  bassAverageDecay = decayPerFrame(0.4f);
  floorFall = 1 - decayPerFrame(LOG_FLOOR_FALL_S);
  floorRise = 1 - decayPerFrame(LOG_FLOOR_RISE_S);
  loadCalibration();

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
  float linear[NUM_BANDS], height[NUM_BANDS], aboveFloor[NUM_BANDS];
  for (int i = 0; i < NUM_BANDS; i++) {
    linear[i] = published.bands[i];
    height[i] = published.bandsDb[i];
    aboveFloor[i] = bandDb[i] - noiseFloorDb[i];
  }
  std::sort(linear, linear + NUM_BANDS);
  std::sort(height, height + NUM_BANDS);
  std::sort(aboveFloor, aboveFloor + NUM_BANDS);
  Serial.printf("audio %.0f samples/s, %.1f analyses/s | spectrum gain %.0f (%.1f dB)\n", measuredSampleRate,
                measuredAnalysisRate, spectrumGain, 20.0f * log10f(spectrumGain));
  Serial.printf("analysis takes avg %.0f us, max %.0f us (budget %.0f us per hop)\n", measuredAnalysisUsAvg,
                measuredAnalysisUsMax, AUDIO_HOP_SAMPLES * 1e6f / SAMPLE_RATE_HZ);
  Serial.printf("ADC %.0f raw samples/s (%ux oversampling) | audio task busy %.0f%% of core 0\n",
                measuredRawSampleRate, ADC_OVERSAMPLE, measuredBusyPercent);
  float third[3] = {};
  for (int i = 0; i < NUM_BANDS; i++) third[i * 3 / NUM_BANDS] += published.bands[i] * 3 / NUM_BANDS;
  Serial.printf("bands: median %.2f  max %.2f | low/mid/high avg %.2f %.2f %.2f | level %.2f\n",
                linear[NUM_BANDS / 2], linear[NUM_BANDS - 1], third[0], third[1], third[2], published.level);
  Serial.printf("bandsDb: above floor median %.1f  max %.1f dB | span %.1f dB | median %.2f  max %.2f\n",
                aboveFloor[NUM_BANDS / 2], aboveFloor[NUM_BANDS - 1], spanDb, height[NUM_BANDS / 2],
                height[NUM_BANDS - 1]);
}

void Audio::printBands() {
  // Reads the audio task's state without locking; fine for a rough printout.
  for (int i = 0; i < NUM_BANDS; i++) {
    float hz = bandStart[i] * (float)SAMPLE_RATE_HZ / FFT_SAMPLES;
    Serial.printf("%2d %5.0f Hz %5.1f dB  floor %5.1f\n", i, hz, bandDb[i], noiseFloorDb[i]);
  }
}

void Audio::calibrateNoise(float seconds) {
  memset(blockSum, 0, sizeof(blockSum));
  for (int b = 0; b < kBins; b++) quietestBlock[b] = INFINITY;
  blockFrames = calibrationFrames = 0;
  // At least one full block, so every bin gets a value.
  calibrationTarget = max((uint32_t)(seconds * ANALYSIS_RATE_HZ), kCalibrationBlock);
  calibrating = true;
}

void Audio::clearNoiseCalibration() {
  memset(noiseMag, 0, sizeof(noiseMag));
  Preferences prefs;
  prefs.begin("audio");
  prefs.remove("noiseMag");
  prefs.remove("noiseRate");
  prefs.end();
}

bool Audio::isCalibrating() { return calibrating; }

void Audio::printNoiseProfile() {
  // Median and the five noisiest bins, so steady tones are easy to spot.
  float sorted[kBins];
  memcpy(sorted, noiseMag, sizeof(sorted));
  std::sort(sorted, sorted + kBins);
  Serial.printf("noise profile: median bin %.1f (%.1f dB)\n", sorted[kBins / 2],
                20.0f * log10f(max(sorted[kBins / 2], 1e-3f)));
  bool used[kBins] = {};
  for (int k = 0; k < 5; k++) {
    int loudest = -1;
    for (int b = FFT_MIN_BIN; b < kBins; b++) {
      if (!used[b] && (loudest < 0 || noiseMag[b] > noiseMag[loudest])) loudest = b;
    }
    used[loudest] = true;
    Serial.printf("  bin %3d %5.0f Hz: %.1f dB\n", loudest, loudest * (float)SAMPLE_RATE_HZ / FFT_SAMPLES,
                  20.0f * log10f(max(noiseMag[loudest], 1e-3f)));
  }
}
