#include "MqttManager.h"
#include "../log.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "MqttManager";

// Shared device block appended to every discovery payload.
// Produces a single "Irrigation Controller" device in HA's device registry.
static const char DEVICE_BLOCK[] =
  "\"device\":{"
    "\"identifiers\":[\"irrigation_uno_r4\"],"
    "\"name\":\"Irrigation Controller\","
    "\"model\":\"UNO R4 WiFi\","
    "\"manufacturer\":\"Arduino\""
  "}";

// Availability fields shared by all MQTT entities.
static const char AVAIL_BLOCK[] =
  "\"availability_topic\":\"" MQTT_ROOT "/valve/" MQTT_TOPIC_STATUS "\","
  "\"payload_available\":\"online\","
  "\"payload_not_available\":\"offline\"";

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
  _mqtt.setBufferSize(768);   // large enough for discovery payloads (~600 B)
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

// ── Discovery ─────────────────────────────────────────────────────────────────

void MqttManager::publishDiscovery() {
  if (!_mqtt.connected()) return;
  LOG_I(TAG, "Publishing MQTT discovery configs");

  for (uint8_t s = 0; s < _sensorCount; s++) {
    discoverSensorVwc(s);
  }

  discoverControllerOnline();
  discoverValveOpen();
  discoverValveState();

  discoverValveCounter("Irrigation Valve Runtime Today",
                       "irrigation_valve_runtime_today",
                       "runtime_today_s", "total_increasing", "s",
                       "mdi:timer-outline");

  discoverValveCounter("Irrigation Valve Runtime Hour",
                       "irrigation_valve_runtime_hour",
                       "runtime_hour_s", "measurement", "s",
                       "mdi:timer-outline");

  discoverValveCounter("Irrigation Valve Pulse Count",
                       "irrigation_valve_pulse_count",
                       "pulse_count", "total_increasing", "",
                       "mdi:counter");

  discoverValveErrorCode();

  discoverButton("Irrigation Valve Pulse",  "irrigation_valve_pulse",
                 "pulse",       "mdi:water");
  discoverButton("Irrigation Valve Close",  "irrigation_valve_close",
                 "close",       "mdi:valve-closed");
  discoverButton("Irrigation Clear Fault",  "irrigation_clear_fault",
                 "clear_fault", "mdi:alert-remove-outline");
}

void MqttManager::discoverSensorVwc(uint8_t sensorId) {
  char payload[640];
  snprintf(payload, sizeof(payload),
    "{"
      "\"name\":\"Irrigation Sensor %d VWC\","
      "\"object_id\":\"irrigation_sensor_%d_vwc\","
      "\"unique_id\":\"irrigation_sensor_%d_vwc\","
      "\"state_topic\":\"" MQTT_ROOT "/sensor/%d/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.vwc }}\","
      "\"unit_of_measurement\":\"%%\","
      "\"state_class\":\"measurement\","
      "\"icon\":\"mdi:water-percent\","
      "%s,"
      "%s"
    "}",
    sensorId, sensorId, sensorId, sensorId,
    AVAIL_BLOCK, DEVICE_BLOCK);

  char objectId[40];
  snprintf(objectId, sizeof(objectId), "irrigation_sensor_%d_vwc", sensorId);
  publishDiscoveryMsg("sensor", objectId, payload);
}

void MqttManager::discoverControllerOnline() {
  char payload[512];
  snprintf(payload, sizeof(payload),
    "{"
      "\"name\":\"Irrigation Controller Online\","
      "\"object_id\":\"irrigation_controller_online\","
      "\"unique_id\":\"irrigation_controller_online\","
      "\"state_topic\":\"" MQTT_ROOT "/valve/" MQTT_TOPIC_STATUS "\","
      "\"payload_on\":\"online\","
      "\"payload_off\":\"offline\","
      "\"device_class\":\"connectivity\","
      "%s"
    "}",
    DEVICE_BLOCK);

  publishDiscoveryMsg("binary_sensor", "irrigation_controller_online", payload);
}

void MqttManager::discoverValveOpen() {
  char payload[600];
  snprintf(payload, sizeof(payload),
    "{"
      "\"name\":\"Irrigation Valve\","
      "\"object_id\":\"irrigation_valve\","
      "\"unique_id\":\"irrigation_valve\","
      "\"state_topic\":\"" MQTT_ROOT "/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.valve_open }}\","
      "\"payload_on\":\"true\","
      "\"payload_off\":\"false\","
      "\"device_class\":\"opening\","
      "%s,"
      "%s"
    "}",
    AVAIL_BLOCK, DEVICE_BLOCK);

  publishDiscoveryMsg("binary_sensor", "irrigation_valve", payload);
}

void MqttManager::discoverValveState() {
  char payload[600];
  snprintf(payload, sizeof(payload),
    "{"
      "\"name\":\"Irrigation Valve State\","
      "\"object_id\":\"irrigation_valve_state\","
      "\"unique_id\":\"irrigation_valve_state\","
      "\"state_topic\":\"" MQTT_ROOT "/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.state }}\","
      "\"icon\":\"mdi:valve\","
      "%s,"
      "%s"
    "}",
    AVAIL_BLOCK, DEVICE_BLOCK);

  publishDiscoveryMsg("sensor", "irrigation_valve_state", payload);
}

void MqttManager::discoverValveCounter(const char* name, const char* objectId,
                                       const char* field, const char* stateClass,
                                       const char* unit, const char* icon) {
  char unitField[48] = "";
  if (unit && unit[0]) {
    snprintf(unitField, sizeof(unitField), "\"unit_of_measurement\":\"%s\",", unit);
  }

  char payload[640];
  snprintf(payload, sizeof(payload),
    "{"
      "\"name\":\"%s\","
      "\"object_id\":\"%s\","
      "\"unique_id\":\"%s\","
      "\"state_topic\":\"" MQTT_ROOT "/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.%s }}\","
      "%s"
      "\"state_class\":\"%s\","
      "\"icon\":\"%s\","
      "%s,"
      "%s"
    "}",
    name, objectId, objectId, field,
    unitField, stateClass, icon,
    AVAIL_BLOCK, DEVICE_BLOCK);

  publishDiscoveryMsg("sensor", objectId, payload);
}

void MqttManager::discoverValveErrorCode() {
  char payload[600];
  snprintf(payload, sizeof(payload),
    "{"
      "\"name\":\"Irrigation Error Code\","
      "\"object_id\":\"irrigation_error_code\","
      "\"unique_id\":\"irrigation_error_code\","
      "\"state_topic\":\"" MQTT_ROOT "/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.error_code }}\","
      "\"icon\":\"mdi:alert-circle-outline\","
      "%s,"
      "%s"
    "}",
    AVAIL_BLOCK, DEVICE_BLOCK);

  publishDiscoveryMsg("sensor", "irrigation_error_code", payload);
}

void MqttManager::discoverButton(const char* name, const char* objectId,
                                 const char* action, const char* icon) {
  // payload_press is a JSON string embedded inside the outer JSON object.
  // Inner quotes must be escaped: {"action":"pulse"} → {\"action\":\"pulse\"}
  char escapedPayload[64];
  snprintf(escapedPayload, sizeof(escapedPayload),
           "{\\\"action\\\":\\\"%s\\\"}", action);

  char payload[600];
  snprintf(payload, sizeof(payload),
    "{"
      "\"name\":\"%s\","
      "\"object_id\":\"%s\","
      "\"unique_id\":\"%s\","
      "\"command_topic\":\"" MQTT_ROOT "/valve/" MQTT_TOPIC_COMMAND "\","
      "\"payload_press\":\"%s\","
      "\"icon\":\"%s\","
      "%s,"
      "%s"
    "}",
    name, objectId, objectId, escapedPayload, icon,
    AVAIL_BLOCK, DEVICE_BLOCK);

  publishDiscoveryMsg("button", objectId, payload);
}

void MqttManager::publishDiscoveryMsg(const char* domain, const char* objectId,
                                      const char* payload) {
  char topic[96];
  snprintf(topic, sizeof(topic), "homeassistant/%s/%s/config", domain, objectId);
  bool ok = _mqtt.publish(topic, payload, true);  // retained
  if (!ok) {
    LOG_W(TAG, "Discovery publish failed for %s (payload len=%d)",
             objectId, strlen(payload));
  } else {
    LOG_D(TAG, "Discovery: %s", topic);
  }
}

// ── Telemetry publish ─────────────────────────────────────────────────────────

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
                                        const char* state,
                                        const char* errorCode,
                                        const char* faultReason) {
  if (!_mqtt.connected()) return;

  char topic[64];
  valveTopic(topic, sizeof(topic), MQTT_TOPIC_TELEMETRY);

  JsonDocument doc;
  doc["valve_open"]      = valveOpen;
  doc["runtime_today_s"] = runtimeTodayS;
  doc["runtime_hour_s"]  = runtimeHourS;
  doc["pulse_count"]     = pulseCount;
  doc["state"]           = state;
  doc["error_code"]      = errorCode ? errorCode : "none";
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
    publishDiscovery();
  } else {
    LOG_W(TAG, "MQTT connect failed, rc=%d", _mqtt.state());
  }
  return ok;
}

void MqttManager::subscribeAll() {
  char topic[64];

  valveTopic(topic, sizeof(topic), MQTT_TOPIC_COMMAND);
  _mqtt.subscribe(topic);
  LOG_D(TAG, "Subscribed: %s", topic);

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

  char valveCmd[64];
  valveTopic(valveCmd, sizeof(valveCmd), MQTT_TOPIC_COMMAND);
  if (strcmp(topic, valveCmd) == 0) {
    MqttCommand cmd{ .target = MqttCommandTarget::VALVE, .sensorId = 0, .doc = &doc };
    _commandCb(cmd);
    return;
  }

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
