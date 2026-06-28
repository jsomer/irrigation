#include "ValveController.h"
#include "../log.h"

static const char* TAG = "ValveController";

ValveController::ValveController(uint8_t valvePin)
  : _pin(valvePin)
  , _state(ValveState::IDLE)
  , _errorCode(ErrorCode::NONE)
  , _configured(false)
  , _valveOpen(false)
  , _pulseStartMs(0)
  , _settleStartMs(0)
  , _requestedPulseS(0)
  , _lastActualPulseS(0)
  , _pulseCount(0)
  , _faultReason(nullptr)
{}

void ValveController::begin() {
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  LOG_I(TAG, "Valve initialised on pin %d", _pin);
}

bool ValveController::paramsValid() const {
  return _params.maxPulseDurationS > 0 && _params.settleDurationS > 0;
}

bool ValveController::requestPulse(uint32_t durationS) {
  if (!_configured || !paramsValid()) {
    LOG_W(TAG, "Pulse denied: not configured");
    return false;
  }
  if (_state != ValveState::IDLE) {
    LOG_W(TAG, "Pulse denied: state=%d", (int)_state);
    return false;
  }
  if (durationS == 0) {
    LOG_W(TAG, "Pulse denied: duration 0");
    return false;
  }

  uint32_t limitS = durationS;
  if (limitS > _params.maxPulseDurationS) {
    limitS = _params.maxPulseDurationS;
  }
  if (limitS > Hardware::ABSOLUTE_MAX_OPEN_S) {
    limitS = Hardware::ABSOLUTE_MAX_OPEN_S;
  }

  _requestedPulseS = limitS;
  openValve();
  _pulseStartMs = millis();
  _state = ValveState::PULSING;
  _pulseCount++;
  LOG_I(TAG, "Pulse started %lu s (count=%lu)", limitS, _pulseCount);
  return true;
}

void ValveController::forceClose() {
  if (_state == ValveState::PULSING && _pulseStartMs > 0) {
    _lastActualPulseS = currentPulseRuntimeS();
  }
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
  _params.maxPulseDurationS = p.maxPulseDurationS;

  if (p.settleDurationS < Hardware::MIN_SETTLE_S) {
    LOG_W(TAG, "settleDuration %d clamped to %lu",
             p.settleDurationS, Hardware::MIN_SETTLE_S);
  }
  _params.settleDurationS = max((uint16_t)Hardware::MIN_SETTLE_S, p.settleDurationS);

  LOG_I(TAG, "Params: max_pulse=%d s settle=%d s",
           _params.maxPulseDurationS, _params.settleDurationS);
  return true;
}

void ValveController::update() {
  switch (_state) {
    case ValveState::PULSING: {
      uint32_t elapsed = currentPulseRuntimeS();

      if (elapsed >= Hardware::ABSOLUTE_MAX_OPEN_S) {
        LOG_W(TAG, "Hardware stuck-valve limit hit (%lu s)", elapsed);
        _lastActualPulseS = elapsed;
        closeValve();
        enterFault(ErrorCode::MAX_PULSE_TIME, "pulse exceeded hardware safety limit");
        break;
      }

      if (elapsed >= _requestedPulseS) {
        _lastActualPulseS = elapsed;
        closeValve();
        _settleStartMs = millis();
        _state = ValveState::SETTLING;
        LOG_I(TAG, "Pulse done (%lu s), settling %d s", elapsed, _params.settleDurationS);
      }
      break;
    }

    case ValveState::SETTLING: {
      uint32_t settledS = (millis() - _settleStartMs) / 1000UL;
      if (settledS >= _params.settleDurationS) {
        _state = ValveState::IDLE;
        LOG_I(TAG, "Settle complete → idle");
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
  uint32_t elapsed = (_state == ValveState::PULSING) ? currentPulseRuntimeS() : 0;
  return {
    .state          = _state,
    .valveOpen      = _valveOpen,
    .pulseCount     = _pulseCount,
    .actualPulseS   = _lastActualPulseS,
    .pulseElapsedS  = elapsed,
    .errorCode      = inFault ? _errorCode   : ErrorCode::NONE,
    .faultReason    = inFault ? _faultReason : nullptr,
  };
}

void ValveController::openValve() {
  digitalWrite(_pin, HIGH);
  _valveOpen = true;
}

void ValveController::closeValve() {
  digitalWrite(_pin, LOW);
  _valveOpen = false;
  _pulseStartMs = 0;
}

void ValveController::enterFault(ErrorCode code, const char* reason) {
  closeValve();
  _state       = ValveState::FAULT;
  _errorCode   = code;
  _faultReason = reason;
  LOG_E(TAG, "FAULT [%s]: %s", errorCodeStr(code), reason);
}

uint32_t ValveController::currentPulseRuntimeS() const {
  if (_pulseStartMs == 0) return 0;
  return (millis() - _pulseStartMs) / 1000UL;
}
