#pragma once
#include <Arduino.h>
#include "../config.h"

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
    case ErrorCode::MAX_PULSE_TIME: return "Pulse exceeded max single-run limit";
    case ErrorCode::HOURLY_LIMIT:   return "Hourly runtime limit reached";
    case ErrorCode::DAILY_LIMIT:    return "Daily runtime limit reached";
    case ErrorCode::PULSE_DENIED:   return "Pulse request denied";
    default:                        return "No fault";
  }
}

enum class ValveState : uint8_t {
  IDLE,
  PULSING,
  SETTLING,
  FAULT,
};

struct ValveParams {
  uint16_t pulseDurationS    = DefaultParams::PULSE_DURATION_S;
  uint16_t settleDurationS   = DefaultParams::SETTLE_DURATION_S;
  uint16_t maxPulseDurationS = DefaultParams::MAX_PULSE_DURATION_S;
  uint32_t maxRuntimeDayS    = DefaultParams::MAX_RUNTIME_DAY_S;
  uint32_t maxRuntimeHourS   = DefaultParams::MAX_RUNTIME_HOUR_S;
};

struct ValveTelemetry {
  ValveState  state;
  bool        valveOpen;
  uint32_t    runtimeTodayS;
  uint32_t    runtimeHourS;
  uint32_t    pulseCount;
  ErrorCode   errorCode;
  const char* faultReason;
};

class ValveController {
public:
  explicit ValveController(uint8_t valvePin);

  void begin();
  void update();

  bool requestPulse();
  void forceClose();
  void clearFault();
  bool setParams(const ValveParams& params);

  const ValveParams& params() const { return _params; }
  ValveTelemetry telemetry() const;
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
  uint32_t effectivePulseDurationS() const;
};
