#include <Arduino.h>
#include <WiFiS3.h>

#include "log.h"
#include "config.h"
#include "secrets.h"
#include "sensors/VH400.h"
#include "irrigation/ZoneController.h"
#include "mqtt/MqttManager.h"

static const char* TAG = "Main";

// ── Hardware objects ──────────────────────────────────────────────────────────

VH400 sensors[ZONE_COUNT] = {
  VH400(Pin::SENSOR_ZONE_0),
  VH400(Pin::SENSOR_ZONE_1),
};

ZoneController zones[ZONE_COUNT] = {
  ZoneController(Pin::VALVE_ZONE_0, 0),
  ZoneController(Pin::VALVE_ZONE_1, 1),
};

MqttManager mqtt(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD, ZONE_COUNT);

// ── Timers ────────────────────────────────────────────────────────────────────

unsigned long lastSensorReadMs   = 0;
unsigned long lastTelemetryMs    = 0;

float latestVWC[ZONE_COUNT] = { -1.0f, -1.0f };

// ── WiFi ──────────────────────────────────────────────────────────────────────

void connectWiFi() {
  LOG_I(TAG, "Connecting to WiFi %s ...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    LOG_I(TAG, "WiFi connected. IP: %s", WiFi.localIP().toString().c_str());
  } else {
    LOG_E(TAG, "WiFi failed - running in offline failsafe mode");
  }
}

// ── MQTT command handler ──────────────────────────────────────────────────────

static const char* zoneStateName(ZoneState s) {
  switch (s) {
    case ZoneState::IDLE:     return "idle";
    case ZoneState::PULSING:  return "pulsing";
    case ZoneState::SETTLING: return "settling";
    case ZoneState::FAULT:    return "fault";
    default:                  return "unknown";
  }
}

void onCommand(const MqttCommand& cmd) {
  uint8_t z = cmd.zoneId;
  JsonDocument& doc = *cmd.doc;
  const char* action = doc["action"] | "";

  LOG_I(TAG, "Command zone %d: %s", z, action);

  if (strcmp(action, "pulse") == 0) {
    if (!zones[z].requestPulse()) {
      LOG_W(TAG, "Zone %d pulse request denied", z);
    }

  } else if (strcmp(action, "close") == 0) {
    zones[z].forceClose();

  } else if (strcmp(action, "clear_fault") == 0) {
    zones[z].clearFault();

  } else if (strcmp(action, "configure") == 0) {
    // AI/HA sends recommended parameter updates.
    // ZoneController clamps everything to hard safety limits internally.
    PulseParams p = zones[z].params();
    if (doc["pulse_duration_s"].is<uint16_t>())  p.pulseDurationS  = doc["pulse_duration_s"];
    if (doc["settle_duration_s"].is<uint16_t>()) p.settleDurationS = doc["settle_duration_s"];
    if (doc["target_vwc"].is<float>())           p.targetVWC       = doc["target_vwc"];
    if (doc["resume_vwc"].is<float>())           p.resumeVWC       = doc["resume_vwc"];
    zones[z].setParams(p);
    LOG_I(TAG, "Zone %d params updated", z);

  } else {
    LOG_W(TAG, "Unknown action: %s", action);
  }
}

// ── Automated pulse trigger ───────────────────────────────────────────────────
// Simple moisture-threshold trigger; HA/AI layer can also push explicit pulses.

void checkAutoTrigger() {
  for (uint8_t z = 0; z < ZONE_COUNT; z++) {
    float vwc = latestVWC[z];
    if (vwc < 0) continue;  // no reading yet

    ZoneState state = zones[z].telemetry().state;
    const PulseParams& p = zones[z].params();

    if (state == ZoneState::IDLE && vwc < p.resumeVWC) {
      LOG_I(TAG, "Zone %d auto-pulse: VWC %.1f < resume %.1f", z, vwc, p.resumeVWC);
      zones[z].requestPulse();
    }
  }
}

// ── Failsafe ──────────────────────────────────────────────────────────────────

void checkFailsafe() {
  uint32_t disconnectedS = mqtt.secondsSinceConnected();
  if (disconnectedS >= Safety::FAILSAFE_DISCONNECT_S) {
    for (uint8_t z = 0; z < ZONE_COUNT; z++) {
      if (zones[z].telemetry().valveOpen) {
        LOG_E(TAG, "FAILSAFE: MQTT lost %lu s, closing zone %d", disconnectedS, z);
        zones[z].forceClose();
      }
    }
  }
}

// ── Setup / loop ──────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(Pin::STATUS_LED, OUTPUT);

  for (uint8_t z = 0; z < ZONE_COUNT; z++) {
    sensors[z].begin();
    zones[z].begin();
  }

  connectWiFi();

  mqtt.setCommandCallback(onCommand);
  mqtt.begin(MQTT_CLIENT_ID);

  LOG_I(TAG, "Irrigation controller ready");
}

void loop() {
  unsigned long now = millis();

  // 1 — Keep MQTT alive
  mqtt.loop();

  // 2 — Read sensors on interval
  if (now - lastSensorReadMs >= Sensor::READ_INTERVAL_MS) {
    lastSensorReadMs = now;
    for (uint8_t z = 0; z < ZONE_COUNT; z++) {
      latestVWC[z] = sensors[z].readVWC();
    }
    checkAutoTrigger();
  }

  // 3 — Tick zone state machines
  for (uint8_t z = 0; z < ZONE_COUNT; z++) {
    zones[z].update();
  }

  // 4 — Failsafe check every loop (cheap comparison)
  checkFailsafe();

  // 5 — Publish telemetry on interval
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    for (uint8_t z = 0; z < ZONE_COUNT; z++) {
      ZoneTelemetry t = zones[z].telemetry();
      mqtt.publishTelemetry(z, latestVWC[z], t.valveOpen,
                            t.runtimeTodayS, t.runtimeHourS,
                            t.pulseCount, zoneStateName(t.state),
                            t.faultReason);
    }
    // Blink LED to show we're alive
    digitalWrite(Pin::STATUS_LED, !digitalRead(Pin::STATUS_LED));
  }
}
