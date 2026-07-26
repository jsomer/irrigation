#include <Arduino.h>
#include <WiFiS3.h>

#include "log.h"
#include "config.h"
#include "secrets.h"
#include "config/ConfigStore.h"
#include "sensors/VH400.h"
#include "irrigation/ValveController.h"
#include "mqtt/MqttManager.h"

static const char* TAG = "Main";

VH400 sensors[SENSOR_COUNT] = {
  VH400(Pin::SENSOR_0),
  VH400(Pin::SENSOR_1),
  VH400(Pin::SENSOR_2),
  VH400(Pin::SENSOR_3),
  VH400(Pin::SENSOR_4),
  VH400(Pin::SENSOR_5),
};

uint32_t failsafeDisconnectS = 0;

ValveController valve(Pin::VALVE);
ConfigStore     configStore;

ConfigSource configSource = ConfigSource::NONE;

unsigned long lastSensorReadMs = 0;
unsigned long lastTelemetryMs  = 0;

float latestVWC[SENSOR_COUNT];

MqttManager mqtt(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD,
                 IRRIGATION_INSTANCE_ID, IRRIGATION_INSTANCE_NAME, SENSOR_COUNT);

static unsigned long lastWifiCheckMs = 0;
static constexpr uint32_t WIFI_CHECK_INTERVAL_MS  = 30000;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;

void connectWiFi() {
  LOG_I(TAG, "Connecting to WiFi %s ...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    LOG_I(TAG, "WiFi connected. IP: %s", WiFi.localIP().toString().c_str());
  } else {
    LOG_E(TAG, "WiFi failed - will retry in %d s", WIFI_CHECK_INTERVAL_MS / 1000);
  }
}

void maintainWiFi() {
  unsigned long now = millis();
  if (now - lastWifiCheckMs < WIFI_CHECK_INTERVAL_MS) return;
  lastWifiCheckMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    LOG_W(TAG, "WiFi lost — reconnecting...");
    WiFi.disconnect();
    connectWiFi();
  }
}

static PersistedConfig buildPersistedConfig() {
  PersistedConfig cfg = {};
  cfg.configured          = 1;
  cfg.failsafeDisconnectS = failsafeDisconnectS;
  ConfigStore::valveParamsToPersisted(valve.params(), cfg);
  return cfg;
}

static void applyPersistedConfig(const PersistedConfig& cfg) {
  ValveParams p;
  ConfigStore::persistedToValveParams(cfg, p);
  valve.setParams(p);
  valve.setConfigured(true);
  failsafeDisconnectS = cfg.failsafeDisconnectS;
  LOG_I(TAG, "Applied persisted config (failsafe=%lu s)", failsafeDisconnectS);
}

static bool saveRuntimeConfig() {
  if (!valve.isConfigured()) return false;
  return configStore.save(buildPersistedConfig());
}

static void applyHaConfigure(JsonDocument& doc) {
  ValveParams p = valve.params();
  if (doc["settle_duration_s"].is<uint16_t>())    p.settleDurationS   = doc["settle_duration_s"];
  if (doc["max_pulse_duration_s"].is<uint16_t>()) p.maxPulseDurationS = doc["max_pulse_duration_s"];
  valve.setParams(p);
  valve.setConfigured(true);

  if (!doc["failsafe_disconnect_s"].isNull()) {
    failsafeDisconnectS = doc["failsafe_disconnect_s"].as<uint32_t>();
    LOG_I(TAG, "Failsafe disconnect set to %lu s", failsafeDisconnectS);
  }

  configSource = ConfigSource::HA;
  saveRuntimeConfig();
  LOG_I(TAG, "Valve params updated from HA");
}

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
  if (cmd.target != MqttCommandTarget::VALVE) return;

  JsonDocument& doc = *cmd.doc;
  const char* action = doc["action"] | "";
  LOG_I(TAG, "Valve command: %s", action);

  if (strcmp(action, "pulse") == 0) {
    uint32_t durationS = doc["pulse_duration_s"] | 0;
    if (!valve.requestPulse(durationS)) {
      LOG_W(TAG, "Valve pulse request denied");
    }

  } else if (strcmp(action, "close") == 0) {
    valve.forceClose();

  } else if (strcmp(action, "clear_fault") == 0) {
    valve.clearFault();

  } else if (strcmp(action, "configure") == 0) {
    applyHaConfigure(doc);

  } else {
    LOG_W(TAG, "Unknown valve action: %s", action);
  }
}

void checkFailsafe() {
  if (failsafeDisconnectS == 0) return;

  uint32_t disconnectedS = mqtt.secondsSinceConnected();
  if (disconnectedS >= failsafeDisconnectS) {
    ValveTelemetry t = valve.telemetry();
    if (t.valveOpen) {
      LOG_E(TAG, "FAILSAFE: MQTT lost %lu s, closing valve", disconnectedS);
      valve.forceClose();
    }
  }
}

void setup() {
  Serial.begin(115200);
  { unsigned long t = millis(); while (!Serial && millis() - t < 3000) {} }
  Serial.println("\n=== Irrigation Controller booting ===");

  pinMode(Pin::STATUS_LED, OUTPUT);

  for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
    latestVWC[s] = -1.0f;
    sensors[s].begin();
  }

  valve.begin();

  PersistedConfig persisted;
  if (configStore.load(persisted)) {
    applyPersistedConfig(persisted);
    configSource = ConfigSource::PERSISTED;
    LOG_I(TAG, "Booted with persisted HA config");
  } else {
    LOG_I(TAG, "No persisted config — waiting for HA configure");
  }

  connectWiFi();

  mqtt.setCommandCallback(onCommand);
  mqtt.begin();

  LOG_I(TAG, "%s controller ready — %d sensor(s), 1 valve",
        IRRIGATION_INSTANCE_NAME, SENSOR_COUNT);
}

void loop() {
  unsigned long now = millis();

  maintainWiFi();
  mqtt.loop();

  if (now - lastSensorReadMs >= Sensor::READ_INTERVAL_MS) {
    lastSensorReadMs = now;
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
      latestVWC[s] = sensors[s].readVWC();
    }
  }

  valve.update();
  checkFailsafe();

  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;

    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
      mqtt.publishSensorTelemetry(s, latestVWC[s], sensors[s].lastVoltage());
    }

    ValveTelemetry t = valve.telemetry();
    mqtt.publishValveTelemetry(
      t.valveOpen, t.pulseCount, t.actualPulseS, t.pulseElapsedS,
      valveStateName(t.state), errorCodeStr(t.errorCode), t.faultReason,
      valve.isConfigured(), configSourceStr(configSource));

    LOG_I(TAG, "valve=%s cfg=%s", valveStateName(t.state), configSourceStr(configSource));
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
      LOG_I(TAG, "  s%d=%.1f%% (%.2fV)", s, latestVWC[s], sensors[s].lastVoltage());
    }

    digitalWrite(Pin::STATUS_LED, !digitalRead(Pin::STATUS_LED));
  }
}
