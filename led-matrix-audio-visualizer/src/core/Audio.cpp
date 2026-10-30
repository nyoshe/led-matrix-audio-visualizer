#include "Audio.h"

#include <Arduino.h>
#include <arduinoFFT.h>

namespace {

float vReal[FFT_SAMPLES];
float vImag[FFT_SAMPLES];
ArduinoFFT<float> fft(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE_HZ);

uint16_t bandStart[NUM_BANDS + 1];  // first FFT bin of each band
float spectrumGain = 1.0f;          // tracks the loudest recent band
float waveGain = 1.0f;              // tracks the largest recent sample
AudioFrame frame;

const uint32_t samplePeriodUs = 1000000UL / SAMPLE_RATE_HZ;

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
  else gain = max(gain * 0.995f, floor);
}

void sample() {
  uint32_t next = micros();
  for (int i = 0; i < FFT_SAMPLES; i++) {
    while ((int32_t)(micros() - next) < 0) {}
    next += samplePeriodUs;
    vReal[i] = analogRead(Pins::AUDIO);
    vImag[i] = 0;
  }

  // Remove the DC bias (AUDIO idles around VCC/2).
  float mean = 0;
  for (int i = 0; i < FFT_SAMPLES; i++) mean += vReal[i];
  mean /= FFT_SAMPLES;
  for (int i = 0; i < FFT_SAMPLES; i++) vReal[i] -= mean;
}

void buildWaveform() {
  float peak = 0;
  for (int i = 0; i < FFT_SAMPLES; i++) peak = max(peak, fabsf(vReal[i]));
  trackGain(waveGain, peak, 40.0f);

  float sumSq = 0;
  for (int i = 0; i < FFT_SAMPLES; i++) {
    float s = constrain(vReal[i] / waveGain, -1.0f, 1.0f);
    frame.waveform[i] = s;
    sumSq += s * s;
  }
  // A full-scale sine has an RMS of 1/sqrt(2); scale so that reads as 1.
  frame.level = min(sqrtf(sumSq / FFT_SAMPLES) * 1.414f, 1.0f);
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
    frame.bands[i] = n ? sum / n : 0;
    loudest = max(loudest, frame.bands[i]);
  }

  trackGain(spectrumGain, loudest, 50.0f);
  for (int i = 0; i < NUM_BANDS; i++) {
    frame.bands[i] = min(frame.bands[i] / spectrumGain, 1.0f);
  }
}

}  // namespace

void Audio::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(Pins::AUDIO, ADC_11db);     // ~0-3.3 V input range
  analogSetPinAttenuation(Pins::ENVELOPE, ADC_11db);
  pinMode(Pins::GATE, INPUT);
  setupBands();
}

const AudioFrame& Audio::capture() {
  sample();
  buildWaveform();  // before the FFT overwrites vReal

  fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  fft.compute(FFTDirection::Forward);
  fft.complexToMagnitude();
  buildBands();

  frame.envelope = analogRead(Pins::ENVELOPE) / 4095.0f;
  frame.gate = digitalRead(Pins::GATE);
  frame.frame++;
  return frame;
}
