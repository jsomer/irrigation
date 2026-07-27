#include "MqttManager.h"
#include "../log.h"
#include <cstring>
#include <cstdio>
#include <cctype>

static const char* TAG = "MqttManager";

MqttManager* MqttManager::_instance = nullptr;

MqttManager::MqttManager(const char* broker, uint16_t port,
                         const char* user, const char* password,
                         const char* instanceId, const char* instanceName,
                         uint8_t sensorCount)
  : _mqtt(_wifiClient)
  , _broker(broker)
  , _port(port)
  , _user(user)
  , _password(password)
  , _instanceId(instanceId)
  , _instanceName(instanceName)
  , _sensorCount(sensorCount)
  , _lastConnectedMs(0)
  , _lastReconnectAttemptMs(0)
  , _hasEverConnected(false)
{
  _instance = this;
  _normalizedInstanceId[0] = '\0';
  _mqttRoot[0] = '\0';
  _clientId[0] = '\0';
  _haNodeId[0] = '\0';
}

void MqttManager::begin() {
  size_t out = 0;
  for (size_t in = 0; _instanceId[in] && out < sizeof(_normalizedInstanceId) - 1; in++) {
    char c = static_cast<char>(tolower(static_cast<unsigned char>(_instanceId[in])));
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
      _normalizedInstanceId[out++] = c;
    } else {
      _normalizedInstanceId[out++] = '_';
      LOG_W(TAG, "Invalid instance ID character replaced with '_'");
    }
  }
  _normalizedInstanceId[out] = '\0';
  if (out == 0) {
    snprintf(_normalizedInstanceId, sizeof(_normalizedInstanceId), "controller");
    LOG_E(TAG, "Empty instance ID; using 'controller'");
  }

  snprintf(_mqttRoot, sizeof(_mqttRoot), MQTT_ROOT_PREFIX "/%s", _normalizedInstanceId);
  snprintf(_clientId, sizeof(_clientId), MQTT_ROOT_PREFIX "-%s", _normalizedInstanceId);
  snprintf(_haNodeId, sizeof(_haNodeId), MQTT_ROOT_PREFIX "_%s", _normalizedInstanceId);

  LOG_I(TAG, "Instance=%s root=%s client=%s",
        _normalizedInstanceId, _mqttRoot, _clientId);
  _lastConnectedMs = millis();
  _mqtt.setServer(_broker, _port);
  _mqtt.setCallback(MqttManager::onMqttMessage);
  _mqtt.setBufferSize(1536);  // discovery payloads include default_entity_id
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
  if (!_hasEverConnected) return 0;
  return (millis() - _lastConnectedMs) / 1000UL;
}

// ── Discovery ─────────────────────────────────────────────────────────────────

void MqttManager::publishDiscovery() {
  if (!_mqtt.connected()) return;
  LOG_I(TAG, "Publishing MQTT discovery configs");

  for (uint8_t sensorId = 0; sensorId < _sensorCount; sensorId++) {
    discoverSensorVwc(sensorId);
  }
  discoverControllerOnline();
  discoverValveOpen();
  discoverValveState();
  discoverValveErrorCode();

  discoverValveCounter("Valve Pulse Count",
                       "valve_pulse_count",
                       "pulse_count", "total_increasing", "",
                       "mdi:counter");

  discoverValveCounter("Pulse Elapsed Seconds",
                       "pulse_elapsed_s",
                       "pulse_elapsed_s", "measurement", "s",
                       "mdi:timer-sand");

  discoverValveFaultReason();
  discoverValveConfigured();
  discoverValveConfigSource();

  discoverButton("Valve Close",  "valve_close",
                 "close",       "mdi:valve-closed");
  discoverButton("Clear Fault",  "clear_fault",
                 "clear_fault", "mdi:alert-remove-outline");
}

void MqttManager::discoverSensorVwc(uint8_t sensorId) {
  char localId[32], localName[32], identity[256], avail[192], device[256];
  snprintf(localId, sizeof(localId), "sensor_%d_vwc", sensorId);
  snprintf(localName, sizeof(localName), "Sensor %d VWC", sensorId);
  entityIdentityBlock(identity, sizeof(identity), "sensor", localId, localName);
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));

  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/sensor/%d/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.vwc }}\","
      "\"unit_of_measurement\":\"%%\","
      "\"state_class\":\"measurement\","
      "\"icon\":\"mdi:water-percent\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, sensorId, avail, device);

  publishDiscoveryMsg("sensor", localId, payload);
}

void MqttManager::discoverControllerOnline() {
  char identity[256], device[256];
  entityIdentityBlock(identity, sizeof(identity), "binary_sensor",
                      "controller_online", "Controller Online");
  deviceBlock(device, sizeof(device));

  char payload[768];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/valve/" MQTT_TOPIC_STATUS "\","
      "\"payload_on\":\"online\","
      "\"payload_off\":\"offline\","
      "\"device_class\":\"connectivity\","
      "%s"
    "}",
    identity, _mqttRoot, device);

  publishDiscoveryMsg("binary_sensor", "controller_online", payload);
}

void MqttManager::discoverValveOpen() {
  char identity[256], avail[192], device[256];
  entityIdentityBlock(identity, sizeof(identity), "binary_sensor", "valve", "Valve");
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));

  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.valve_open }}\","
      "\"payload_on\":\"true\","
      "\"payload_off\":\"false\","
      "\"device_class\":\"opening\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, avail, device);

  publishDiscoveryMsg("binary_sensor", "valve", payload);
}

void MqttManager::discoverValveState() {
  char identity[256], avail[192], device[256];
  entityIdentityBlock(identity, sizeof(identity), "sensor",
                      "valve_state", "Valve State");
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));

  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.state }}\","
      "\"icon\":\"mdi:valve\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, avail, device);

  publishDiscoveryMsg("sensor", "valve_state", payload);
}

void MqttManager::discoverValveCounter(const char* name, const char* objectId,
                                       const char* field, const char* stateClass,
                                       const char* unit, const char* icon) {
  char identity[256], avail[192], device[256];
  entityIdentityBlock(identity, sizeof(identity), "sensor", objectId, name);
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));

  char unitField[48] = "";
  if (unit && unit[0]) {
    snprintf(unitField, sizeof(unitField), "\"unit_of_measurement\":\"%s\",", unit);
  }

  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.%s }}\","
      "%s"
      "\"state_class\":\"%s\","
      "\"icon\":\"%s\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, field,
    unitField, stateClass, icon,
    avail, device);

  publishDiscoveryMsg("sensor", objectId, payload);
}


void MqttManager::discoverValveFaultReason() {
  char identity[256], avail[192], device[256];
  entityIdentityBlock(identity, sizeof(identity), "sensor",
                      "fault_reason", "Fault Reason");
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));
  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.fault_reason | default('') }}\","
      "\"icon\":\"mdi:alert-outline\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, avail, device);

  publishDiscoveryMsg("sensor", "fault_reason", payload);
}

void MqttManager::discoverValveConfigured() {
  char identity[256], avail[192], device[256];
  entityIdentityBlock(identity, sizeof(identity), "binary_sensor",
                      "firmware_configured", "Firmware Configured");
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));
  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.configured }}\","
      "\"icon\":\"mdi:check-circle-outline\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, avail, device);

  publishDiscoveryMsg("binary_sensor", "firmware_configured", payload);
}

void MqttManager::discoverValveConfigSource() {
  char identity[256], avail[192], device[256];
  entityIdentityBlock(identity, sizeof(identity), "sensor",
                      "config_source", "Config Source");
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));
  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.config_source }}\","
      "\"icon\":\"mdi:database-outline\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, avail, device);

  publishDiscoveryMsg("sensor", "config_source", payload);
}

void MqttManager::discoverValveErrorCode() {
  char identity[256], avail[192], device[256];
  entityIdentityBlock(identity, sizeof(identity), "sensor",
                      "error_code", "Error Code");
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));
  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"state_topic\":\"%s/valve/" MQTT_TOPIC_TELEMETRY "\","
      "\"value_template\":\"{{ value_json.error_code }}\","
      "\"icon\":\"mdi:alert-circle-outline\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, avail, device);

  publishDiscoveryMsg("sensor", "error_code", payload);
}

void MqttManager::discoverButton(const char* name, const char* objectId,
                                 const char* action, const char* icon) {
  char identity[256], avail[192], device[256];
  entityIdentityBlock(identity, sizeof(identity), "button", objectId, name);
  availabilityBlock(avail, sizeof(avail));
  deviceBlock(device, sizeof(device));

  // payload_press is a JSON string embedded inside the outer JSON object.
  // Inner quotes must be escaped: {"action":"pulse"} → {\"action\":\"pulse\"}
  char escapedPayload[64];
  snprintf(escapedPayload, sizeof(escapedPayload),
           "{\\\"action\\\":\\\"%s\\\"}", action);

  char payload[896];
  snprintf(payload, sizeof(payload),
    "{"
      "%s,"
      "\"command_topic\":\"%s/valve/" MQTT_TOPIC_COMMAND "\","
      "\"payload_press\":\"%s\","
      "\"icon\":\"%s\","
      "%s,"
      "%s"
    "}",
    identity, _mqttRoot, escapedPayload, icon,
    avail, device);

  publishDiscoveryMsg("button", objectId, payload);
}

void MqttManager::publishDiscoveryMsg(const char* domain, const char* objectId,
                                      const char* payload) {
  char topic[144];
  snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config",
           domain, _haNodeId, objectId);
  bool ok = _mqtt.publish(topic, payload, true);  // retained
  if (!ok) {
    LOG_W(TAG, "Discovery publish failed for %s (payload len=%d)",
             objectId, strlen(payload));
  } else {
    LOG_D(TAG, "Discovery: %s", topic);
  }
}

// ── Telemetry publish ─────────────────────────────────────────────────────────

void MqttManager::publishSensorTelemetry(uint8_t sensorId, float vwc, float voltage) {
  if (!_mqtt.connected()) return;

  char topic[64];
  sensorTopic(topic, sizeof(topic), sensorId, MQTT_TOPIC_TELEMETRY);

  JsonDocument doc;
  doc["sensor"]  = sensorId;
  doc["vwc"]     = serialized(String(vwc, 1));
  doc["voltage"] = serialized(String(voltage, 2));
  doc["ts"]      = millis() / 1000UL;

  char buf[128];
  serializeJson(doc, buf, sizeof(buf));
  _mqtt.publish(topic, buf, false);
  LOG_D(TAG, "sensor %d telemetry: %s", sensorId, buf);
}

void MqttManager::publishValveTelemetry(bool valveOpen, uint32_t pulseCount,
                                        uint32_t actualPulseS, uint32_t pulseElapsedS,
                                        const char* state,
                                        const char* errorCode,
                                        const char* faultReason,
                                        bool configured,
                                        const char* configSource) {
  if (!_mqtt.connected()) return;

  char topic[64];
  valveTopic(topic, sizeof(topic), MQTT_TOPIC_TELEMETRY);

  JsonDocument doc;
  doc["valve_open"]       = valveOpen;
  doc["pulse_count"]      = pulseCount;
  doc["actual_pulse_s"]   = actualPulseS;
  doc["pulse_elapsed_s"]  = pulseElapsedS;
  doc["state"]            = state;
  doc["error_code"]       = errorCode ? errorCode : "none";
  doc["configured"]       = configured;
  doc["config_source"]    = configSource ? configSource : "none";
  if (faultReason)       doc["fault_reason"] = faultReason;
  doc["ts"]               = millis() / 1000UL;

  char buf[320];
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
    _hasEverConnected = true;
    _lastConnectedMs = millis();
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
}

void MqttManager::sensorTopic(char* buf, size_t len,
                               uint8_t sensorId, const char* suffix) const {
  snprintf(buf, len, "%s/sensor/%d/%s", _mqttRoot, sensorId, suffix);
}

void MqttManager::valveTopic(char* buf, size_t len, const char* suffix) const {
  snprintf(buf, len, "%s/valve/%s", _mqttRoot, suffix);
}

void MqttManager::entityId(char* buf, size_t len, const char* localId) const {
  snprintf(buf, len, "%s_%s", _haNodeId, localId);
}

// Entity display names are local only (e.g. "Sensor 0 VWC"). Prefixing them with
// IRRIGATION_INSTANCE_NAME caused HA to slugify "Raised Bed Sensor 0 VWC" and then
// prepend the device slug again → raised_bed_raised_bed_…. Device.name already
// carries the instance label. default_entity_id forces irrigation_<id>_* entity IDs
// on modern HA (object_id alone is deprecated / often ignored).
void MqttManager::entityIdentityBlock(char* buf, size_t len, const char* domain,
                                      const char* localId,
                                      const char* localName) const {
  char id[80];
  entityId(id, sizeof(id), localId);
  snprintf(buf, len,
    "\"name\":\"%s\","
    "\"object_id\":\"%s\","
    "\"default_entity_id\":\"%s.%s\","
    "\"unique_id\":\"%s\"",
    localName, id, domain, id, id);
}

void MqttManager::deviceBlock(char* buf, size_t len) const {
  snprintf(buf, len,
    "\"device\":{"
      "\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\","
      "\"model\":\"UNO R4 WiFi\","
      "\"manufacturer\":\"Arduino\""
    "}",
    _haNodeId, _instanceName);
}

void MqttManager::availabilityBlock(char* buf, size_t len) const {
  snprintf(buf, len,
    "\"availability_topic\":\"%s/valve/" MQTT_TOPIC_STATUS "\","
    "\"payload_available\":\"online\","
    "\"payload_not_available\":\"offline\"",
    _mqttRoot);
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

  LOG_W(TAG, "Unrecognised topic: %s", topic);
}
