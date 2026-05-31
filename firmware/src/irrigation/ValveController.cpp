#include "ValveController.h"
#include "../log.h"

static const char* TAG = "ValveController";

ValveController::ValveController(uint8_t valvePin)
  : _pin(valvePin)
  , _state(ValveState::IDLE)
  , _errorCode(ErrorCode::NONE)
  , _valveOpen(false)
  , _pulseStartMs(0)
  , _settleStartMs(0)
  , _runtimeTodayS(0)
  , _runtimeHourS(0)
  , _pulseCount(0)
  , _faultReason(nullptr)
  , _hourWindowStartMs(0)
  , _dayWindowStartMs(0)
{}

void ValveController::begin() {
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  _hourWindowStartMs = millis();
  _dayWindowStartMs  = millis();
  LOG_I(TAG, "Valve initialised on pin %d", _pin);
}

// ── Public API ────────────────────────────────────────────────────────────────

bool ValveController::requestPulse() {
  if (_state != ValveState::IDLE) {
    LOG_W(TAG, "Pulse request ignored: state=%d", (int)_state);
    return false;
  }

  // Check each safety limit individually to produce a precise error code.
  if (_runtimeHourS >= Safety::MAX_RUNTIME_HOUR_S) {
    enterFault(ErrorCode::HOURLY_LIMIT, "hourly runtime limit exceeded");
    return false;
  }
  if (_runtimeTodayS >= Safety::MAX_RUNTIME_DAY_S) {
    enterFault(ErrorCode::DAILY_LIMIT, "daily runtime limit exceeded");
    return false;
  }
  if (!canOpen()) {
    enterFault(ErrorCode::PULSE_DENIED, "pulse request denied");
    return false;
  }

  openValve();
  _pulseStartMs = millis();
  _state = ValveState::PULSING;
  _pulseCount++;
  LOG_I(TAG, "Pulse started (count=%lu)", _pulseCount);
  return true;
}

void ValveController::forceClose() {
  closeValve();
  if (_state != ValveState::FAULT) {
    _state = ValveState::IDLE;
  }
  LOG_W(TAG, "Valve force-closed");
}

void ValveController::clearFault() {
  if (_state == ValveState::FAULT) {
    _errorCode   = ErrorCode::NONE;
    _faultReason = nullptr;
    _state       = ValveState::IDLE;
    LOG_I(TAG, "Fault cleared");
  }
}

bool ValveController::setParams(const ValveParams& p) {
  if (p.pulseDurationS > Safety::MAX_PULSE_DURATION_S) {
    LOG_W(TAG, "pulseDuration %d clamped to %d",
             p.pulseDurationS, Safety::MAX_PULSE_DURATION_S);
  }
  if (p.settleDurationS < Safety::MIN_SETTLE_S) {
    LOG_W(TAG, "settleDuration %d clamped to %d",
             p.settleDurationS, Safety::MIN_SETTLE_S);
  }
  _params.pulseDurationS  = min((uint16_t)Safety::MAX_PULSE_DURATION_S, p.pulseDurationS);
  _params.settleDurationS = max((uint16_t)Safety::MIN_SETTLE_S,         p.settleDurationS);
  return true;
}

// ── State machine ─────────────────────────────────────────────────────────────

void ValveController::update() {
  tickTimeWindows();

  switch (_state) {
    case ValveState::PULSING: {
      uint32_t elapsed = currentPulseRuntimeS();

      if (elapsed >= Safety::MAX_PULSE_DURATION_S) {
        LOG_W(TAG, "Hard pulse limit hit (%d s)", elapsed);
        closeValve();
        enterFault(ErrorCode::MAX_PULSE_TIME, "pulse exceeded 120 s hard limit");
        break;
      }

      if (elapsed >= _params.pulseDurationS) {
        closeValve();
        _settleStartMs = millis();
        _state = ValveState::SETTLING;
        LOG_I(TAG, "Pulse done (%d s), settling %d s",
                 elapsed, _params.settleDurationS);
      }
      break;
    }

    case ValveState::SETTLING: {
      uint32_t settledS = (millis() - _settleStartMs) / 1000UL;
      if (settledS >= _params.settleDurationS) {
        _state = ValveState::IDLE;
        LOG_I(TAG, "Settle complete");
      }
      break;
    }

    case ValveState::IDLE:
    case ValveState::FAULT:
    default:
      break;
  }
}

ValveTelemetry ValveController::telemetry() const {
  bool inFault = (_state == ValveState::FAULT);
  return {
    .state          = _state,
    .valveOpen      = _valveOpen,
    .runtimeTodayS  = _runtimeTodayS,
    .runtimeHourS   = _runtimeHourS,
    .pulseCount     = _pulseCount,
    .errorCode      = inFault ? _errorCode   : ErrorCode::NONE,
    .faultReason    = inFault ? _faultReason : nullptr,
  };
}

bool ValveController::canOpen() const {
  if (_state == ValveState::FAULT)    return false;
  if (_valveOpen)                     return false;
  if (_runtimeHourS  >= Safety::MAX_RUNTIME_HOUR_S) return false;
  if (_runtimeTodayS >= Safety::MAX_RUNTIME_DAY_S)  return false;
  return true;
}

// ── Private helpers ───────────────────────────────────────────────────────────

void ValveController::openValve() {
  digitalWrite(_pin, HIGH);
  _valveOpen = true;
}

void ValveController::closeValve() {
  digitalWrite(_pin, LOW);
  _valveOpen = false;

  if (_pulseStartMs > 0) {
    uint32_t pulsedS = currentPulseRuntimeS();
    _runtimeHourS  += pulsedS;
    _runtimeTodayS += pulsedS;
    _pulseStartMs   = 0;
  }
}

void ValveController::enterFault(ErrorCode code, const char* reason) {
  closeValve();
  _state       = ValveState::FAULT;
  _errorCode   = code;
  _faultReason = reason;
  LOG_E(TAG, "FAULT [%s]: %s", errorCodeStr(code), reason);
}

void ValveController::tickTimeWindows() {
  unsigned long now = millis();

  if ((now - _hourWindowStartMs) >= 3600000UL) {
    _runtimeHourS      = 0;
    _hourWindowStartMs = now;
  }
  if ((now - _dayWindowStartMs) >= 86400000UL) {
    _runtimeTodayS    = 0;
    _dayWindowStartMs = now;
  }
}

uint32_t ValveController::currentPulseRuntimeS() const {
  if (_pulseStartMs == 0) return 0;
  return (millis() - _pulseStartMs) / 1000UL;
}
