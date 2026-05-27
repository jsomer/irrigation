#include "MqttManager.h"
#include "../log.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "MqttManager";

MqttManager* MqttManager::_instance = nullptr;

MqttManager::MqttManager(const char* broker, uint16_t port,
                         const char* user, const char* password,
                         uint8_t sensorCount)
  : _mqtt(_wifiClient)
  , _broker(broker)
  , _port(port)
  , _user(user)
  , _password(password)
  , _clientId(nullptr)
  , _sensorCount(sensorCount)
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

bool MqttManager::isConnected() {
  return _mqtt.connected();
}

uint32_t MqttManager::secondsSinceConnected() {
  if (_mqtt.connected()) return 0;
  return (millis() - _lastConnectedMs) / 1000UL;
}

// ── Publish ───────────────────────────────────────────────────────────────────

void MqttManager::publishSensorTelemetry(uint8_t sensorId, float vwc) {
  if (!_mqtt.connected()) return;

  char topic[64];
  sensorTopic(topic, sizeof(topic), sensorId, MQTT_TOPIC_TELEMETRY);

  JsonDocument doc;
  doc["sensor"] = sensorId;
  doc["vwc"]    = serialized(String(vwc, 1));
  doc["ts"]     = millis() / 1000UL;

  char buf[128];
  serializeJson(doc, buf, sizeof(buf));
  _mqtt.publish(topic, buf, false);
  LOG_D(TAG, "sensor %d telemetry: %s", sensorId, buf);
}

void MqttManager::publishValveTelemetry(bool valveOpen, uint32_t runtimeTodayS,
                                        uint32_t runtimeHourS, uint32_t pulseCount,
                                        const char* state, const char* faultReason) {
  if (!_mqtt.connected()) return;

  char topic[64];
  valveTopic(topic, sizeof(topic), MQTT_TOPIC_TELEMETRY);

  JsonDocument doc;
  doc["valve_open"]      = valveOpen;
  doc["runtime_today_s"] = runtimeTodayS;
  doc["runtime_hour_s"]  = runtimeHourS;
  doc["pulse_count"]     = pulseCount;
  doc["state"]           = state;
  if (faultReason)       doc["fault_reason"] = faultReason;
  doc["ts"]              = millis() / 1000UL;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));
  _mqtt.publish(topic, buf, false);
  LOG_D(TAG, "valve telemetry: %s", buf);
}

void MqttManager::publishValveStatus(const char* status) {
  char topic[64];
  valveTopic(topic, sizeof(topic), MQTT_TOPIC_STATUS);
  _mqtt.publish(topic, status, true);  // retained
}

// ── Private ───────────────────────────────────────────────────────────────────

bool MqttManager::reconnect() {
  LOG_I(TAG, "Connecting to MQTT %s:%d ...", _broker, _port);

  char lwtTopic[64];
  valveTopic(lwtTopic, sizeof(lwtTopic), MQTT_TOPIC_STATUS);

  bool ok = _mqtt.connect(_clientId, _user, _password,
                          lwtTopic, 1, true, "offline");
  if (ok) {
    LOG_I(TAG, "MQTT connected");
    subscribeAll();
    publishValveStatus("online");
  } else {
    LOG_W(TAG, "MQTT connect failed, rc=%d", _mqtt.state());
  }
  return ok;
}

void MqttManager::subscribeAll() {
  char topic[64];

  // Valve command topic
  valveTopic(topic, sizeof(topic), MQTT_TOPIC_COMMAND);
  _mqtt.subscribe(topic);
  LOG_D(TAG, "Subscribed: %s", topic);

  // Per-sensor config topics
  for (uint8_t s = 0; s < _sensorCount; s++) {
    sensorTopic(topic, sizeof(topic), s, MQTT_TOPIC_CONFIG);
    _mqtt.subscribe(topic);
    LOG_D(TAG, "Subscribed: %s", topic);
  }
}

void MqttManager::sensorTopic(char* buf, size_t len,
                               uint8_t sensorId, const char* suffix) const {
  snprintf(buf, len, "%s/sensor/%d/%s", MQTT_ROOT, sensorId, suffix);
}

void MqttManager::valveTopic(char* buf, size_t len, const char* suffix) const {
  snprintf(buf, len, "%s/valve/%s", MQTT_ROOT, suffix);
}

void MqttManager::onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (_instance) _instance->handleMessage(topic, payload, length);
}

void MqttManager::handleMessage(char* topic, byte* payload, unsigned int length) {
  LOG_D(TAG, "Received [%s] len=%d", topic, length);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    LOG_W(TAG, "JSON parse error: %s", err.c_str());
    return;
  }

  if (!_commandCb) return;

  // irrigation/valve/command
  char valveCmd[64];
  valveTopic(valveCmd, sizeof(valveCmd), MQTT_TOPIC_COMMAND);
  if (strcmp(topic, valveCmd) == 0) {
    MqttCommand cmd{ .target = MqttCommandTarget::VALVE, .sensorId = 0, .doc = &doc };
    _commandCb(cmd);
    return;
  }

  // irrigation/sensor/<id>/config
  for (uint8_t s = 0; s < _sensorCount; s++) {
    char sensorCfg[64];
    sensorTopic(sensorCfg, sizeof(sensorCfg), s, MQTT_TOPIC_CONFIG);
    if (strcmp(topic, sensorCfg) == 0) {
      MqttCommand cmd{ .target = MqttCommandTarget::SENSOR, .sensorId = s, .doc = &doc };
      _commandCb(cmd);
      return;
    }
  }

  LOG_W(TAG, "Unrecognised topic: %s", topic);
}
