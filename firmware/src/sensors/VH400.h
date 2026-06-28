#pragma once
#include <Arduino.h>

// Driver for the Vegetronix VH400 capacitive soil moisture probe.
// Output: 0–3 V analog → converted to volumetric water content (VWC) %.
// Calibration uses the Vegetronix piecewise-linear equation for mineral soils.
class VH400 {
public:
  explicit VH400(uint8_t pin, uint8_t averagingSamples = 16);

  void  begin();

  // Returns the averaged VWC reading (0–100 %). Returns -1 on read error.
  float readVWC();

  // Returns raw averaged voltage (0–3.0 V; VH400 max output).
  float readVoltage();

  // True if no successful read has been taken yet or last read was >STALE_MS ago.
  bool  isStale() const;

  uint8_t pin() const { return _pin; }

  // Last averaged signal voltage from the most recent readVWC() (0–3 V).
  float lastVoltage() const { return _lastVoltage; }

  static constexpr uint32_t STALE_MS = 30000;

private:
  uint8_t       _pin;
  uint8_t       _samples;
  float         _lastVWC;
  float         _lastVoltage;
  unsigned long _lastReadMs;
  bool          _hasReading;

  // Vegetronix mineral-soil piecewise-linear calibration (V → VWC %).
  static float voltageToVWC(float v);
};
