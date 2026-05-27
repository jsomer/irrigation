#pragma once
#include <Arduino.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
// VH400 sensors connect to analog input pins.
// Single solenoid valve connects through a relay/MOSFET driver; HIGH = open.

namespace Pin {
  constexpr uint8_t SENSOR_0    = A0;
  constexpr uint8_t SENSOR_1    = A1;
  constexpr uint8_t VALVE       = 5;    // single shared valve
  constexpr uint8_t STATUS_LED  = LED_BUILTIN;
}

// ── Sensors / zones ───────────────────────────────────────────────────────────
// SENSOR_COUNT controls how many VH400 probes are read and reported.
// Adding more sensors: increment this and add a Pin::SENSOR_N entry above.
constexpr uint8_t SENSOR_COUNT = 2;

// ── MQTT topics ───────────────────────────────────────────────────────────────
// Sensor telemetry:  MQTT_ROOT "/sensor/" <sensorId> "/telemetry"
// Sensor config:     MQTT_ROOT "/sensor/" <sensorId> "/config"
// Valve telemetry:   MQTT_ROOT "/valve/telemetry"
// Valve command:     MQTT_ROOT "/valve/command"
// Valve status:      MQTT_ROOT "/valve/status"
constexpr const char* MQTT_ROOT                  = "irrigation";
constexpr const char* MQTT_TOPIC_TELEMETRY        = "telemetry";
constexpr const char* MQTT_TOPIC_COMMAND          = "command";
constexpr const char* MQTT_TOPIC_STATUS           = "status";
constexpr const char* MQTT_TOPIC_CONFIG           = "config";
constexpr const char* MQTT_CLIENT_ID              = "irrigation-controller";

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

// ── Default parameters ────────────────────────────────────────────────────────
namespace DefaultParams {
  // Valve timing (overridable via MQTT configure)
  constexpr uint16_t PULSE_DURATION_S   = 30;
  constexpr uint16_t SETTLE_DURATION_S  = 300;
  // Per-sensor dry threshold (overridable via MQTT sensor config)
  constexpr float    RESUME_VWC         = 25.0f;
}

// ── Telemetry ─────────────────────────────────────────────────────────────────
constexpr uint32_t TELEMETRY_INTERVAL_MS = 10000;  // publish every 10 s
