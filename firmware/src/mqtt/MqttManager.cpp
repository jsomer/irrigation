#include "MqttManager.h"
#include <esp_log.h>
#include <cstring>
#include <cstdio>

static const char* TAG = "MqttManager";

MqttManager* MqttManager::_instance = nullptr;

MqttManager::MqttManager(const char* broker, uint16_t port,
                         const char* user, const char* password,
                         uint8_t zoneCount)
  : _mqtt(_wifiClient)
  , _broker(broker)
  , _port(port)
  , _user(user)
  , _password(password)
  , _clientId(nullptr)
  , _zoneCount(zoneCount)
  , _lastConnectedMs(0)
  , _lastReconnectAttemptMs(0)
{
  _instance = this;
}

void MqttManager::begin(const char* clientId) {
  _clientId = clientId;
  _mqtt.setServer(_broker, _port);
  _mqtt.setCallback(MqttManager::onMqttMessage);
  _mqtt.setBufferSize(512);
  _mqtt.setKeepAlive(30);
}

void MqttManager::setCommandCallback(CommandCallback cb) {
  _commandCb = cb;
}

void MqttManager::loop() {
  if (!_mqtt.connected()) {
    unsigned long now = millis();
    if (now - _lastReconnectAttemptMs >= RECONNECT_INTERVAL_MS) {
      _lastReconnectAttemptMs = now;
      reconnect();
    }
  } else {
    _lastConnectedMs = millis();
    _mqtt.loop();
  }
}

bool MqttManager::isConnected() const {
  return _mqtt.connected();
}

uint32_t MqttManager::secondsSinceConnected() const {
  if (_mqtt.connected()) return 0;
  return (millis() - _lastConnectedMs) / 1000UL;
}

// ── Publish ───────────────────────────────────────────────────────────────────

void MqttManager::publishTelemetry(uint8_t zoneId, float vwc, bool valveOpen,
                                   uint32_t runtimeTodayS, uint32_t runtimeHourS,
                                   uint32_t pulseCount, const char* state) {
  if (!_mqtt.connected()) return;

  char topic[64];
  buildTopic(topic, sizeof(topic), zoneId, MQTT_TOPIC_TELEMETRY);

  JsonDocument doc;
  doc["zone"]             = zoneId;
  doc["vwc"]              = serialized(String(vwc, 1));
  doc["valve_open"]       = valveOpen;
  doc["runtime_today_s"]  = runtimeTodayS;
  doc["runtime_hour_s"]   = runtimeHourS;
  doc["pulse_count"]      = pulseCount;
  doc["state"]            = state;
  doc["ts"]               = millis() / 1000UL;

  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  _mqtt.publish(topic, buf, false);
  ESP_LOGD(TAG, "telemetry zone %d: %s", zoneId, buf);
}

void MqttManager::publishStatus(uint8_t zoneId, const char* status) {
  char topic[64];
  buildTopic(topic, sizeof(topic), zoneId, MQTT_TOPIC_STATUS);
  _mqtt.publish(topic, status, true);  // retained
}

// ── Private ───────────────────────────────────────────────────────────────────

bool MqttManager::reconnect() {
  ESP_LOGI(TAG, "Connecting to MQTT %s:%d …", _broker, _port);

  // Build LWT topic for zone 0 (primary zone used for device-level status)
  char lwtTopic[64];
  buildTopic(lwtTopic, sizeof(lwtTopic), 0, MQTT_TOPIC_STATUS);

  bool ok = _mqtt.connect(_clientId, _user, _password,
                          lwtTopic, 1, true, "offline");
  if (ok) {
    ESP_LOGI(TAG, "MQTT connected");
    subscribeAll();
    // Publish online for all zones
    for (uint8_t z = 0; z < _zoneCount; z++) {
      publishStatus(z, "online");
    }
  } else {
    ESP_LOGW(TAG, "MQTT connect failed, rc=%d", _mqtt.state());
  }
  return ok;
}

void MqttManager::subscribeAll() {
  char topic[64];
  for (uint8_t z = 0; z < _zoneCount; z++) {
    buildTopic(topic, sizeof(topic), z, MQTT_TOPIC_COMMAND);
    _mqtt.subscribe(topic);
    ESP_LOGD(TAG, "Subscribed: %s", topic);

    buildTopic(topic, sizeof(topic), z, MQTT_TOPIC_CONFIG);
    _mqtt.subscribe(topic);
    ESP_LOGD(TAG, "Subscribed: %s", topic);
  }
}

void MqttManager::buildTopic(char* buf, size_t bufLen,
                              uint8_t zoneId, const char* suffix) const {
  snprintf(buf, bufLen, "%s/zone/%d/%s", MQTT_ROOT, zoneId, suffix);
}

void MqttManager::onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (_instance) {
    _instance->handleMessage(topic, payload, length);
  }
}

void MqttManager::handleMessage(char* topic, byte* payload, unsigned int length) {
  ESP_LOGD(TAG, "Received [%s] len=%d", topic, length);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    ESP_LOGW(TAG, "JSON parse error: %s", err.c_str());
    return;
  }

  // Parse zone ID from topic: "<root>/zone/<id>/<suffix>"
  int zoneId = -1;
  char rootPrefix[32];
  int  zid = 0;
  char suffix[32];
  if (sscanf(topic, "%31[^/]/zone/%d/%31s", rootPrefix, &zid, suffix) == 3) {
    zoneId = zid;
  }

  if (zoneId < 0 || zoneId >= _zoneCount) {
    ESP_LOGW(TAG, "Unrecognised topic or zone: %s", topic);
    return;
  }

  if (_commandCb) {
    MqttCommand cmd{ .zoneId = static_cast<uint8_t>(zoneId), .doc = &doc };
    _commandCb(cmd);
  }
}
