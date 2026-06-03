#pragma once
#include <Arduino.h>
#include "../config.h"

// ── Error codes ───────────────────────────────────────────────────────────────
// Structured codes included in valve telemetry; "none" when no fault.
//
//  E001  Hard pulse-duration safety limit hit during an active pulse
//  E002  Hourly runtime budget exhausted before pulse could complete
//  E003  Daily runtime budget exhausted before pulse could start
//  E004  Pulse request denied (already open, or in fault/settling state)

enum class ErrorCode : uint8_t {
  NONE           = 0,
  MAX_PULSE_TIME = 1,   // E001
  HOURLY_LIMIT   = 2,   // E002
  DAILY_LIMIT    = 3,   // E003
  PULSE_DENIED   = 4,   // E004
};

inline const char* errorCodeStr(ErrorCode c) {
  switch (c) {
    case ErrorCode::MAX_PULSE_TIME: return "E001";
    case ErrorCode::HOURLY_LIMIT:   return "E002";
    case ErrorCode::DAILY_LIMIT:    return "E003";
    case ErrorCode::PULSE_DENIED:   return "E004";
    default:                        return "none";
  }
}

inline const char* errorCodeDesc(ErrorCode c) {
  switch (c) {
    case ErrorCode::MAX_PULSE_TIME: return "Pulse exceeded 120 s hard limit";
    case ErrorCode::HOURLY_LIMIT:   return "Hourly runtime limit reached (600 s/hr)";
    case ErrorCode::DAILY_LIMIT:    return "Daily runtime limit reached";
    case ErrorCode::PULSE_DENIED:   return "Pulse request denied";
    default:                        return "No fault";
  }
}

// ── Valve state machine ───────────────────────────────────────────────────────

enum class ValveState : uint8_t {
  IDLE,       // valve closed, waiting for trigger
  PULSING,    // valve open, counting down pulse duration
  SETTLING,   // valve closed, counting down settle gap
  FAULT,      // safety limit hit; requires explicit reset
};

struct ValveParams {
  uint16_t pulseDurationS   = DefaultParams::PULSE_DURATION_S;
  uint16_t settleDurationS  = DefaultParams::SETTLE_DURATION_S;
  uint32_t maxRuntimeDayS   = DefaultParams::MAX_RUNTIME_DAY_S;
};

struct ValveTelemetry {
  ValveState  state;
  bool        valveOpen;
  uint32_t    runtimeTodayS;
  uint32_t    runtimeHourS;
  uint32_t    pulseCount;
  ErrorCode   errorCode;    // NONE when state != FAULT
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
  uint8_t     _pin;
  ValveState  _state;
  ValveParams _params;
  ErrorCode   _errorCode;

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
  void enterFault(ErrorCode code, const char* reason);
  void tickTimeWindows();

  uint32_t currentPulseRuntimeS() const;
};
