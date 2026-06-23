#pragma once
#include <Arduino.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
// VH400 sensors connect to analog input pins.
// Solenoid via 5 V relay module (optocoupler IN on D5, trigger < 5 mA); HIGH = open.

namespace Pin {
  constexpr uint8_t SENSOR_0    = A0;
  constexpr uint8_t SENSOR_1    = A1;
  constexpr uint8_t VALVE       = 5;    // single shared valve
  constexpr uint8_t STATUS_LED  = LED_BUILTIN;
}

// ── Sensors / zones ───────────────────────────────────────────────────────────
constexpr uint8_t SENSOR_COUNT = 2;

// ── MQTT topics ───────────────────────────────────────────────────────────────
#define MQTT_ROOT             "irrigation"
#define MQTT_TOPIC_TELEMETRY  "telemetry"
#define MQTT_TOPIC_COMMAND    "command"
#define MQTT_TOPIC_STATUS     "status"
#define MQTT_TOPIC_CONFIG     "config"
#define MQTT_CLIENT_ID        "irrigation-controller"

// ── Emergency hard limits (never overridden by MQTT) ─────────────────────────
// Operational limits are pushed from Home Assistant via MQTT configure.
namespace Safety {
  constexpr uint32_t EMERGENCY_MAX_PULSE_S       = 7200;   // 2 hr stuck-valve backstop
  constexpr uint32_t EMERGENCY_MAX_RUNTIME_HOUR_S = 7200; // 2 hr/hr absolute max
  constexpr uint32_t EMERGENCY_MAX_RUNTIME_DAY_S  = 28800; // 8 hr/day absolute max
  constexpr uint32_t EMERGENCY_FAILSAFE_DISCONNECT_S = 7200; // 2 hr MQTT-loss cap
  constexpr uint32_t MIN_RUNTIME_DAY_S           = 60;
  constexpr uint32_t MIN_SETTLE_S                = 60;
}

// ── Sensor ────────────────────────────────────────────────────────────────────
namespace Sensor {
  constexpr uint8_t  AVERAGING_SAMPLES  = 16;
  constexpr uint32_t READ_INTERVAL_MS   = 5000;
  constexpr float    VCC                = 3.3f;
  constexpr uint16_t ADC_MAX            = 1023;
}

// ── Default parameters (overridden by HA via MQTT on connect) ────────────────
namespace DefaultParams {
  constexpr uint16_t PULSE_DURATION_S      = 600;   // 10 min
  constexpr uint16_t SETTLE_DURATION_S     = 1200;  // 20 min
  constexpr uint16_t MAX_PULSE_DURATION_S  = 1200; // 20 min single-run cap
  constexpr uint32_t MAX_RUNTIME_DAY_S     = 7200;  // 120 min/day
  constexpr uint32_t MAX_RUNTIME_HOUR_S    = 1800;  // 30 min/hr
  constexpr uint32_t FAILSAFE_DISCONNECT_S = 1800;  // 30 min
  constexpr bool     AUTO_TRIGGER_ENABLED  = false; // HA controls irrigation
  constexpr float    RESUME_VWC            = 35.0f;
}

constexpr uint32_t TELEMETRY_INTERVAL_MS = 10000;
