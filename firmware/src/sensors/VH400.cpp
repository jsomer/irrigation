#include "VH400.h"
#include "../config.h"

VH400::VH400(uint8_t pin, uint8_t averagingSamples)
  : _pin(pin)
  , _samples(averagingSamples)
  , _lastVWC(-1.0f)
  , _lastVoltage(0.0f)
  , _lastReadMs(0)
  , _hasReading(false)
{}

void VH400::begin() {
  pinMode(_pin, INPUT);
}

float VH400::readVoltage() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < _samples; i++) {
    sum += analogRead(_pin);
    delayMicroseconds(200);
  }
  float raw = static_cast<float>(sum) / _samples;
  return (raw / Sensor::ADC_MAX) * Sensor::VCC;
}

float VH400::readVWC() {
  float v = readVoltage();
  float vwc = voltageToVWC(v);

  _lastVoltage  = v;
  _lastVWC      = vwc;
  _lastReadMs   = millis();
  _hasReading   = true;

  return vwc;
}

bool VH400::isStale() const {
  if (!_hasReading) return true;
  return (millis() - _lastReadMs) > STALE_MS;
}

// Vegetronix piecewise linear calibration for mineral soils.
// Source: Vegetronix VH400 application note.
//   V < 1.1  → VWC = 10V - 1
//   1.1–1.3  → VWC = 25V - 17.5
//   1.3–1.82 → VWC = 48.08V - 47.5
//   1.82–2.2 → VWC = 26.32V - 7.89
//   V ≥ 2.2  → VWC = 62.5V - 87.5
float VH400::voltageToVWC(float v) {
  float vwc;
  if (v < 1.1f) {
    vwc = 10.0f * v - 1.0f;
  } else if (v < 1.3f) {
    vwc = 25.0f * v - 17.5f;
  } else if (v < 1.82f) {
    vwc = 48.08f * v - 47.5f;
  } else if (v < 2.2f) {
    vwc = 26.32f * v - 7.89f;
  } else {
    vwc = 62.5f * v - 87.5f;
  }

  // Clamp to valid physical range
  if (vwc < 0.0f)   vwc = 0.0f;
  if (vwc > 100.0f) vwc = 100.0f;
  return vwc;
}
