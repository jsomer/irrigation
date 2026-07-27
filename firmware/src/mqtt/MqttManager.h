#pragma once
#include <Arduino.h>
#include <WiFiS3.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <functional>
#include "../config.h"

// ── Command types ─────────────────────────────────────────────────────────────

enum class MqttCommandTarget : uint8_t {
  VALVE,   // irrigation/<instance_id>/valve/command
};

struct MqttCommand {
  MqttCommandTarget target;
  uint8_t           sensorId;   // valid only when target == SENSOR
  JsonDocument*     doc;        // owned by MqttManager; valid only during callback
};

using CommandCallback = std::function<void(const MqttCommand&)>;

// ── MqttManager ───────────────────────────────────────────────────────────────

class MqttManager {
public:
  MqttManager(const char* broker, uint16_t port,
              const char* user, const char* password,
              const char* instanceId, const char* instanceName,
              uint8_t sensorCount);

  void begin();

  // Call every loop iteration. Handles reconnect + PubSubClient::loop().
  void loop();

  bool isConnected();

  // Seconds since last successful MQTT connection. Used for failsafe logic.
  uint32_t secondsSinceConnected();

  void setCommandCallback(CommandCallback cb);

  // Publish MQTT Discovery configs for all entities.
  // Called automatically after each successful connect.
  void publishDiscovery();

  // Publish VWC and signal voltage for one sensor zone.
  void publishSensorTelemetry(uint8_t sensorId, float vwc, float voltage);

  // Publish valve state telemetry.
  void publishValveTelemetry(bool valveOpen, uint32_t pulseCount,
                             uint32_t actualPulseS, uint32_t pulseElapsedS,
                             const char* state,
                             const char* errorCode   = "none",
                             const char* faultReason = nullptr,
                             bool configured         = false,
                             const char* configSource = "none");

  // Publish valve online/offline status (also used as LWT).
  void publishValveStatus(const char* status);

private:
  WiFiClient    _wifiClient;
  PubSubClient  _mqtt;

  const char*   _broker;
  uint16_t      _port;
  const char*   _user;
  const char*   _password;
  const char*   _instanceId;
  const char*   _instanceName;
  char          _normalizedInstanceId[33];
  char          _mqttRoot[64];
  char          _clientId[64];
  char          _haNodeId[48];
  uint8_t       _sensorCount;

  CommandCallback _commandCb;
  unsigned long   _lastConnectedMs;
  unsigned long   _lastReconnectAttemptMs;
  bool            _hasEverConnected;

  static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

  bool reconnect();
  void subscribeAll();

  // Topic builders
  void sensorTopic(char* buf, size_t len, uint8_t sensorId, const char* suffix) const;
  void valveTopic (char* buf, size_t len, const char* suffix) const;
  // unique_id / object_id / default_entity_id stem: irrigation_<instance>_<local>
  void entityId(char* buf, size_t len, const char* localId) const;
  // Discovery identity JSON fields (name is local-only; device carries instance name)
  void entityIdentityBlock(char* buf, size_t len, const char* domain,
                           const char* localId, const char* localName) const;
  void deviceBlock(char* buf, size_t len) const;
  void availabilityBlock(char* buf, size_t len) const;

  // Discovery helpers
  void discoverSensorVwc(uint8_t sensorId);
  void discoverValveOpen();
  void discoverValveState();
  void discoverValveCounter(const char* name, const char* objectId,
                            const char* field, const char* stateClass,
                            const char* unit, const char* icon);
  void discoverValveErrorCode();
  void discoverValveFaultReason();
  void discoverValveConfigured();
  void discoverValveConfigSource();
  void discoverControllerOnline();
  void discoverButton(const char* name, const char* objectId,
                      const char* action, const char* icon);
  void publishDiscoveryMsg(const char* domain, const char* objectId,
                           const char* payload);

  static void onMqttMessage(char* topic, byte* payload, unsigned int length);
  static MqttManager* _instance;

  void handleMessage(char* topic, byte* payload, unsigned int length);
};
