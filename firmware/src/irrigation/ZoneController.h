#pragma once
#include <Arduino.h>
#include "../config.h"

enum class ZoneState : uint8_t {
  IDLE,       // valve closed, waiting for trigger
  PULSING,    // valve open, counting down pulse duration
  SETTLING,   // valve closed, counting down settle gap
  FAULT,      // safety limit hit; requires explicit reset
};

struct PulseParams {
  uint16_t pulseDurationS  = DefaultParams::PULSE_DURATION_S;
  uint16_t settleDurationS = DefaultParams::SETTLE_DURATION_S;
  float    targetVWC       = DefaultParams::TARGET_VWC;
  float    resumeVWC       = DefaultParams::RESUME_VWC;
};

struct ZoneTelemetry {
  ZoneState state;
  bool      valveOpen;
  uint32_t  runtimeTodayS;
  uint32_t  runtimeHourS;
  uint32_t  pulseCount;
  const char* faultReason;  // nullptr when state != FAULT
};

class ZoneController {
public:
  ZoneController(uint8_t valvePin, uint8_t zoneId);

  void begin();

  // Call every loop iteration. Drives the pulse/settle state machine.
  void update();

  // Request a single pulse cycle. Returns false if safety limits deny it.
  bool requestPulse();

  // Immediately close valve and return to IDLE. Always succeeds.
  void forceClose();

  // Clear FAULT state so the zone can operate again.
  void clearFault();

  // Update runtime-tunable parameters (validated against hard limits).
  bool setParams(const PulseParams& params);

  const PulseParams& params() const { return _params; }

  ZoneTelemetry telemetry() const;

  // True if valve can legally open right now (checks all safety limits).
  bool canOpen() const;

  uint8_t id() const { return _id; }

private:
  uint8_t   _pin;
  uint8_t   _id;
  ZoneState _state;
  PulseParams _params;

  bool          _valveOpen;
  unsigned long _pulseStartMs;
  unsigned long _settleStartMs;

  uint32_t      _runtimeTodayS;
  uint32_t      _runtimeHourS;
  uint32_t      _pulseCount;
  const char*   _faultReason;

  unsigned long _hourWindowStartMs;
  unsigned long _dayWindowStartMs;

  void openValve();
  void closeValve();
  void enterFault(const char* reason);

  // Advance time-window counters; resets hour/day buckets when window expires.
  void tickTimeWindows();

  uint32_t currentPulseRuntimeS() const;
};
