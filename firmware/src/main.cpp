#include <Arduino.h>
#include <WiFiS3.h>

#include "log.h"
#include "config.h"
#include "secrets.h"
#include "sensors/VH400.h"
#include "irrigation/ValveController.h"
#include "mqtt/MqttManager.h"

static const char* TAG = "Main";

// ── Hardware objects ──────────────────────────────────────────────────────────

// Sensor pins ordered to match their index (0 = A0, 1 = A1, …)
static constexpr uint8_t SENSOR_PINS[SENSOR_COUNT] = {
  Pin::SENSOR_0,
  Pin::SENSOR_1,
};

VH400          sensors[SENSOR_COUNT] = {
  VH400(Pin::SENSOR_0),
  VH400(Pin::SENSOR_1),
};

// Per-sensor dry threshold (overridable via MQTT sensor config).
float sensorResumeVWC[SENSOR_COUNT];

ValveController valve(Pin::VALVE);

MqttManager mqtt(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD, SENSOR_COUNT);

// ── Timers ────────────────────────────────────────────────────────────────────

unsigned long lastSensorReadMs = 0;
unsigned long lastTelemetryMs  = 0;

float latestVWC[SENSOR_COUNT];

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

static const char* valveStateName(ValveState s) {
  switch (s) {
    case ValveState::IDLE:     return "idle";
    case ValveState::PULSING:  return "pulsing";
    case ValveState::SETTLING: return "settling";
    case ValveState::FAULT:    return "fault";
    default:                   return "unknown";
  }
}

void onCommand(const MqttCommand& cmd) {
  JsonDocument& doc = *cmd.doc;

  if (cmd.target == MqttCommandTarget::VALVE) {
    const char* action = doc["action"] | "";
    LOG_I(TAG, "Valve command: %s", action);

    if (strcmp(action, "pulse") == 0) {
      if (!valve.requestPulse()) {
        LOG_W(TAG, "Valve pulse request denied");
      }

    } else if (strcmp(action, "close") == 0) {
      valve.forceClose();

    } else if (strcmp(action, "clear_fault") == 0) {
      valve.clearFault();

    } else if (strcmp(action, "configure") == 0) {
      ValveParams p = valve.params();
      if (doc["pulse_duration_s"].is<uint16_t>())  p.pulseDurationS  = doc["pulse_duration_s"];
      if (doc["settle_duration_s"].is<uint16_t>()) p.settleDurationS = doc["settle_duration_s"];
      valve.setParams(p);
      LOG_I(TAG, "Valve params updated");

    } else {
      LOG_W(TAG, "Unknown valve action: %s", action);
    }

  } else if (cmd.target == MqttCommandTarget::SENSOR) {
    uint8_t s = cmd.sensorId;
    if (doc["resume_vwc"].is<float>()) {
      sensorResumeVWC[s] = doc["resume_vwc"];
      LOG_I(TAG, "Sensor %d resumeVWC updated to %.1f", s, sensorResumeVWC[s]);
    }
  }
}

// ── Automated pulse trigger ───────────────────────────────────────────────────
// Fires if ANY sensor reads below its dry threshold and the valve is idle.

void checkAutoTrigger() {
  if (!valve.canOpen()) return;

  for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
    float vwc = latestVWC[s];
    if (vwc < 0) continue;  // no reading yet

    if (vwc < sensorResumeVWC[s]) {
      LOG_I(TAG, "Auto-pulse: sensor %d VWC %.1f < resume %.1f",
               s, vwc, sensorResumeVWC[s]);
      valve.requestPulse();
      return;  // one pulse request is enough
    }
  }
}

// ── Failsafe ──────────────────────────────────────────────────────────────────

void checkFailsafe() {
  uint32_t disconnectedS = mqtt.secondsSinceConnected();
  if (disconnectedS >= Safety::FAILSAFE_DISCONNECT_S) {
    ValveTelemetry t = valve.telemetry();
    if (t.valveOpen) {
      LOG_E(TAG, "FAILSAFE: MQTT lost %lu s, closing valve", disconnectedS);
      valve.forceClose();
    }
  }
}

// ── Setup / loop ──────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("\n=== Irrigation Controller booting ===");

  pinMode(Pin::STATUS_LED, OUTPUT);

  // Initialise per-sensor thresholds to firmware default
  for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
    sensorResumeVWC[s] = DefaultParams::RESUME_VWC;
    latestVWC[s]       = -1.0f;
    sensors[s].begin();
  }

  valve.begin();

  connectWiFi();

  mqtt.setCommandCallback(onCommand);
  mqtt.begin(MQTT_CLIENT_ID);

  LOG_I(TAG, "Irrigation controller ready — %d sensor(s), 1 valve", SENSOR_COUNT);
}

void loop() {
  unsigned long now = millis();

  // 1 — Keep MQTT alive
  mqtt.loop();

  // 2 — Read sensors on interval
  if (now - lastSensorReadMs >= Sensor::READ_INTERVAL_MS) {
    lastSensorReadMs = now;
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
      latestVWC[s] = sensors[s].readVWC();
    }
    checkAutoTrigger();
  }

  // 3 — Tick valve state machine
  valve.update();

  // 4 — Failsafe check every loop (cheap comparison)
  checkFailsafe();

  // 5 — Publish telemetry on interval
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;

    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
      mqtt.publishSensorTelemetry(s, latestVWC[s]);
    }

    ValveTelemetry t = valve.telemetry();
    mqtt.publishValveTelemetry(t.valveOpen, t.runtimeTodayS, t.runtimeHourS,
                               t.pulseCount, valveStateName(t.state),
                               t.faultReason);

    LOG_I(TAG, "valve=%s s0=%.1f%% s1=%.1f%%",
             valveStateName(t.state), latestVWC[0], latestVWC[1]);

    // Blink LED to show we're alive
    digitalWrite(Pin::STATUS_LED, !digitalRead(Pin::STATUS_LED));
  }
}
