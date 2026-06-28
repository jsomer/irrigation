#pragma once
#include <Arduino.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
// VH400 sensors connect to analog input pins.
// Solenoid via 5 V relay module (optocoupler IN on D5, trigger < 5 mA); HIGH = open.

namespace Pin {
  constexpr uint8_t SENSOR_0    = A0;
  constexpr uint8_t SENSOR_1    = A1;
  constexpr uint8_t SENSOR_2    = A2;
  constexpr uint8_t SENSOR_3    = A3;
  constexpr uint8_t SENSOR_4    = A4;
  constexpr uint8_t SENSOR_5    = A5;
  constexpr uint8_t VALVE       = 5;    // single shared valve
  constexpr uint8_t STATUS_LED  = LED_BUILTIN;
}

// ── Sensors / zones ───────────────────────────────────────────────────────────
constexpr uint8_t SENSOR_COUNT = 6;

// ── MQTT topics ───────────────────────────────────────────────────────────────
#define MQTT_ROOT             "irrigation"
#define MQTT_TOPIC_TELEMETRY  "telemetry"
#define MQTT_TOPIC_COMMAND    "command"
#define MQTT_TOPIC_STATUS     "status"
#define MQTT_TOPIC_CONFIG     "config"
#define MQTT_CLIENT_ID        "irrigation-controller"

// ── Hardware-only limits (not HA tuning knobs) ────────────────────────────────
// Operational limits are pushed from Home Assistant via MQTT configure and
// persisted to flash. Only stuck-valve protection and physical floors remain.
namespace Hardware {
  constexpr uint32_t ABSOLUTE_MAX_OPEN_S = 86400;  // 24 hr stuck-relay backstop
  constexpr uint32_t MIN_SETTLE_S        = 60;     // physical minimum settle
}

// ── Sensor ────────────────────────────────────────────────────────────────────
namespace Sensor {
  constexpr uint8_t  AVERAGING_SAMPLES  = 16;
  constexpr uint32_t READ_INTERVAL_MS   = 5000;
  constexpr float    VCC                = 5.0f;   // UNO R4 default analog ref (AVCC), not 3.3 V
  constexpr uint16_t ADC_MAX            = 1023;
}

constexpr uint32_t TELEMETRY_INTERVAL_MS = 10000;
