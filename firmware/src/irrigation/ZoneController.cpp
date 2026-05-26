#include "ZoneController.h"
#include <esp_log.h>

static const char* TAG = "ZoneController";

ZoneController::ZoneController(uint8_t valvePin, uint8_t zoneId)
  : _pin(valvePin)
  , _id(zoneId)
  , _state(ZoneState::IDLE)
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

void ZoneController::begin() {
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  _hourWindowStartMs = millis();
  _dayWindowStartMs  = millis();
  ESP_LOGI(TAG, "Zone %d initialised on pin %d", _id, _pin);
}

// ── Public API ────────────────────────────────────────────────────────────────

bool ZoneController::requestPulse() {
  if (_state != ZoneState::IDLE) {
    ESP_LOGW(TAG, "Zone %d pulse request ignored: state=%d", _id, (int)_state);
    return false;
  }
  if (!canOpen()) {
    enterFault("safety limit exceeded");
    return false;
  }
  openValve();
  _pulseStartMs = millis();
  _state = ZoneState::PULSING;
  _pulseCount++;
  ESP_LOGI(TAG, "Zone %d pulse started (count=%lu)", _id, _pulseCount);
  return true;
}

void ZoneController::forceClose() {
  closeValve();
  if (_state != ZoneState::FAULT) {
    _state = ZoneState::IDLE;
  }
  ESP_LOGW(TAG, "Zone %d force-closed", _id);
}

void ZoneController::clearFault() {
  if (_state == ZoneState::FAULT) {
    _faultReason = nullptr;
    _state = ZoneState::IDLE;
    ESP_LOGI(TAG, "Zone %d fault cleared", _id);
  }
}

bool ZoneController::setParams(const PulseParams& p) {
  // Clamp to hard safety limits — AI/HA cannot exceed them
  if (p.pulseDurationS > Safety::MAX_PULSE_DURATION_S) {
    ESP_LOGW(TAG, "Zone %d: pulseDuration %d clamped to %d",
             _id, p.pulseDurationS, Safety::MAX_PULSE_DURATION_S);
  }
  if (p.settleDurationS < Safety::MIN_SETTLE_S) {
    ESP_LOGW(TAG, "Zone %d: settleDuration %d clamped to %d",
             _id, p.settleDurationS, Safety::MIN_SETTLE_S);
  }

  _params.pulseDurationS  = min((uint16_t)Safety::MAX_PULSE_DURATION_S, p.pulseDurationS);
  _params.settleDurationS = max((uint16_t)Safety::MIN_SETTLE_S,         p.settleDurationS);
  _params.targetVWC       = p.targetVWC;
  _params.resumeVWC       = p.resumeVWC;
  return true;
}

// ── State machine ─────────────────────────────────────────────────────────────

void ZoneController::update() {
  tickTimeWindows();

  switch (_state) {
    case ZoneState::PULSING: {
      uint32_t elapsed = currentPulseRuntimeS();

      // Accumulate runtime into time-window buckets
      _runtimeHourS = min(_runtimeHourS + 0u, _runtimeHourS);  // updated on close
      _runtimeTodayS = min(_runtimeTodayS + 0u, _runtimeTodayS);

      // Hard limit: pulse ran too long
      if (elapsed >= Safety::MAX_PULSE_DURATION_S) {
        ESP_LOGW(TAG, "Zone %d: hard pulse limit hit (%d s)", _id, elapsed);
        closeValve();
        enterFault("max pulse duration exceeded");
        break;
      }

      // Soft limit: configured pulse duration elapsed
      if (elapsed >= _params.pulseDurationS) {
        closeValve();
        _settleStartMs = millis();
        _state = ZoneState::SETTLING;
        ESP_LOGI(TAG, "Zone %d pulse done (%d s), settling %d s",
                 _id, elapsed, _params.settleDurationS);
      }
      break;
    }

    case ZoneState::SETTLING: {
      uint32_t settledS = (millis() - _settleStartMs) / 1000UL;
      if (settledS >= _params.settleDurationS) {
        _state = ZoneState::IDLE;
        ESP_LOGI(TAG, "Zone %d settle complete", _id);
      }
      break;
    }

    case ZoneState::IDLE:
    case ZoneState::FAULT:
    default:
      break;
  }
}

ZoneTelemetry ZoneController::telemetry() const {
  return {
    .state          = _state,
    .valveOpen      = _valveOpen,
    .runtimeTodayS  = _runtimeTodayS,
    .runtimeHourS   = _runtimeHourS,
    .pulseCount     = _pulseCount,
    .faultReason    = (_state == ZoneState::FAULT) ? _faultReason : nullptr,
  };
}

bool ZoneController::canOpen() const {
  if (_state == ZoneState::FAULT)    return false;
  if (_valveOpen)                    return false;
  if (_runtimeHourS  >= Safety::MAX_RUNTIME_HOUR_S) return false;
  if (_runtimeTodayS >= Safety::MAX_RUNTIME_DAY_S)  return false;
  return true;
}

// ── Private helpers ───────────────────────────────────────────────────────────

void ZoneController::openValve() {
  digitalWrite(_pin, HIGH);
  _valveOpen = true;
}

void ZoneController::closeValve() {
  digitalWrite(_pin, LOW);
  _valveOpen = false;

  if (_pulseStartMs > 0) {
    uint32_t pulsedS = currentPulseRuntimeS();
    _runtimeHourS  += pulsedS;
    _runtimeTodayS += pulsedS;
    _pulseStartMs   = 0;
  }
}

void ZoneController::enterFault(const char* reason) {
  closeValve();
  _state = ZoneState::FAULT;
  _faultReason = reason;
  ESP_LOGE(TAG, "Zone %d FAULT: %s", _id, reason);
}

void ZoneController::tickTimeWindows() {
  unsigned long now = millis();

  if ((now - _hourWindowStartMs) >= 3600000UL) {
    _runtimeHourS    = 0;
    _hourWindowStartMs = now;
  }
  if ((now - _dayWindowStartMs) >= 86400000UL) {
    _runtimeTodayS  = 0;
    _dayWindowStartMs = now;
  }
}

uint32_t ZoneController::currentPulseRuntimeS() const {
  if (_pulseStartMs == 0) return 0;
  return (millis() - _pulseStartMs) / 1000UL;
}
