#pragma once
#include <Arduino.h>
#include <WiFiS3.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <functional>
#include "../config.h"

// Payload delivered to the command handler.
struct MqttCommand {
  uint8_t   zoneId;
  JsonDocument* doc;  // owned by MqttManager; valid only during callback
};

using CommandCallback = std::function<void(const MqttCommand&)>;

class MqttManager {
public:
  MqttManager(const char* broker, uint16_t port,
              const char* user, const char* password,
              uint8_t zoneCount);

  void begin(const char* clientId);

  // Call every loop iteration. Handles reconnect + PubSubClient::loop().
  void loop();

  bool isConnected();

  // Seconds since last successful MQTT connection. Used for failsafe logic.
  uint32_t secondsSinceConnected();

  void setCommandCallback(CommandCallback cb);

  // Publish sensor + zone telemetry for one zone.
  void publishTelemetry(uint8_t zoneId, float vwc, bool valveOpen,
                        uint32_t runtimeTodayS, uint32_t runtimeHourS,
                        uint32_t pulseCount, const char* state,
                        const char* faultReason = nullptr);

  // Publish zone online/offline status (also used as LWT).
  void publishStatus(uint8_t zoneId, const char* status);

private:
  WiFiClient    _wifiClient;
  PubSubClient  _mqtt;

  const char*   _broker;
  uint16_t      _port;
  const char*   _user;
  const char*   _password;
  const char*   _clientId;
  uint8_t       _zoneCount;

  CommandCallback _commandCb;
  unsigned long   _lastConnectedMs;
  unsigned long   _lastReconnectAttemptMs;

  static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

  bool reconnect();
  void subscribeAll();
  void buildTopic(char* buf, size_t bufLen,
                  uint8_t zoneId, const char* suffix) const;

  // Static trampoline for PubSubClient callback
  static void onMqttMessage(char* topic, byte* payload, unsigned int length);
  static MqttManager* _instance;

  void handleMessage(char* topic, byte* payload, unsigned int length);
};
