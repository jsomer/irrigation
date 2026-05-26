#pragma once
#include <Arduino.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
// VH400 sensors connect to analog input pins.
// Valves connect through a relay/MOSFET driver; pin is HIGH = valve open.

namespace Pin {
  constexpr uint8_t SENSOR_ZONE_0  = A0;
  constexpr uint8_t SENSOR_ZONE_1  = A1;
  constexpr uint8_t VALVE_ZONE_0   = 5;
  constexpr uint8_t VALVE_ZONE_1   = 6;
  constexpr uint8_t STATUS_LED     = LED_BUILTIN;
}

// ── Zones ─────────────────────────────────────────────────────────────────────
constexpr uint8_t ZONE_COUNT = 2;

// ── MQTT topics ───────────────────────────────────────────────────────────────
// Full topic: MQTT_ROOT "/zone/" <zoneId> "/" <suffix>
constexpr const char* MQTT_ROOT              = "irrigation";
constexpr const char* MQTT_TOPIC_TELEMETRY   = "telemetry";
constexpr const char* MQTT_TOPIC_COMMAND     = "command";
constexpr const char* MQTT_TOPIC_STATUS      = "status";
constexpr const char* MQTT_TOPIC_CONFIG      = "config";   // AI/HA writes params here
constexpr const char* MQTT_CLIENT_ID         = "irrigation-controller";

// ── Safety hard limits (never overridden by MQTT/AI) ─────────────────────────
namespace Safety {
  constexpr uint32_t MAX_PULSE_DURATION_S  = 120;   // single valve open event
  constexpr uint32_t MAX_RUNTIME_HOUR_S    = 600;   // 10 min/hr
  constexpr uint32_t MAX_RUNTIME_DAY_S     = 3600;  // 1 hr/day
  constexpr uint32_t MIN_SETTLE_S          = 60;    // min gap between pulses
  constexpr uint32_t FAILSAFE_DISCONNECT_S = 120;   // close valve if MQTT lost
}

// ── Sensor ────────────────────────────────────────────────────────────────────
namespace Sensor {
  constexpr uint8_t  AVERAGING_SAMPLES  = 16;
  constexpr uint32_t READ_INTERVAL_MS   = 5000;
  constexpr float    VCC                = 5.0f;   // AVCC = 5 V on UNO R4
  constexpr uint16_t ADC_MAX            = 1023;   // 10-bit default resolution
}

// ── Default pulse parameters (overridable at runtime via MQTT config topic) ───
namespace DefaultParams {
  constexpr uint16_t PULSE_DURATION_S   = 30;
  constexpr uint16_t SETTLE_DURATION_S  = 300;
  constexpr float    TARGET_VWC         = 40.0f;
  constexpr float    RESUME_VWC         = 25.0f;
}

// ── Telemetry ─────────────────────────────────────────────────────────────────
constexpr uint32_t TELEMETRY_INTERVAL_MS = 10000;  // publish every 10 s
