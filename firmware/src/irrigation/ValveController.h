#pragma once
#include <Arduino.h>
#include "../config.h"

enum class ValveState : uint8_t {
  IDLE,       // valve closed, waiting for trigger
  PULSING,    // valve open, counting down pulse duration
  SETTLING,   // valve closed, counting down settle gap
  FAULT,      // safety limit hit; requires explicit reset
};

struct ValveParams {
  uint16_t pulseDurationS  = DefaultParams::PULSE_DURATION_S;
  uint16_t settleDurationS = DefaultParams::SETTLE_DURATION_S;
};

struct ValveTelemetry {
  ValveState  state;
  bool        valveOpen;
  uint32_t    runtimeTodayS;
  uint32_t    runtimeHourS;
  uint32_t    pulseCount;
  const char* faultReason;  // nullptr when state != FAULT
};

class ValveController {
public:
  explicit ValveController(uint8_t valvePin);

  void begin();

  // Call every loop iteration. Drives the pulse/settle state machine.
  void update();

  // Request a single pulse cycle. Returns false if safety limits deny it.
  bool requestPulse();

  // Immediately close valve and return to IDLE. Always succeeds.
  void forceClose();

  // Clear FAULT state so the valve can operate again.
  void clearFault();

  // Update runtime-tunable parameters (validated against hard limits).
  bool setParams(const ValveParams& params);

  const ValveParams& params() const { return _params; }

  ValveTelemetry telemetry() const;

  // True if valve can legally open right now (checks all safety limits).
  bool canOpen() const;

private:
  uint8_t    _pin;
  ValveState _state;
  ValveParams _params;

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
  void tickTimeWindows();

  uint32_t currentPulseRuntimeS() const;
};
