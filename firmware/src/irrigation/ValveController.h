#pragma once
#include <Arduino.h>
#include "../config.h"

enum class ErrorCode : uint8_t {
  NONE           = 0,
  MAX_PULSE_TIME = 1,   // E001 — hardware stuck-valve backstop
};

inline const char* errorCodeStr(ErrorCode c) {
  switch (c) {
    case ErrorCode::MAX_PULSE_TIME: return "E001";
    default:                        return "none";
  }
}

enum class ValveState : uint8_t {
  IDLE,
  PULSING,
  SETTLING,
  FAULT,
};

struct ValveParams {
  uint16_t settleDurationS   = 0;
  uint16_t maxPulseDurationS = 0;
};

struct ValveTelemetry {
  ValveState  state;
  bool        valveOpen;
  uint32_t    pulseCount;
  uint32_t    actualPulseS;
  uint32_t    pulseElapsedS;
  ErrorCode   errorCode;
  const char* faultReason;
};

class ValveController {
public:
  explicit ValveController(uint8_t valvePin);

  void begin();
  void update();

  bool requestPulse(uint32_t durationS);
  void forceClose();
  void clearFault();
  bool setParams(const ValveParams& params);

  bool isConfigured() const { return _configured; }
  void setConfigured(bool configured) { _configured = configured; }

  const ValveParams& params() const { return _params; }
  ValveTelemetry telemetry() const;

private:
  uint8_t     _pin;
  ValveState  _state;
  ValveParams _params;
  ErrorCode   _errorCode;
  bool        _configured;

  bool          _valveOpen;
  unsigned long _pulseStartMs;
  unsigned long _settleStartMs;
  uint32_t      _requestedPulseS;
  uint32_t      _lastActualPulseS;
  uint32_t      _pulseCount;
  const char*   _faultReason;

  void openValve();
  void closeValve();
  void enterFault(ErrorCode code, const char* reason);
  uint32_t currentPulseRuntimeS() const;
  bool paramsValid() const;
};
