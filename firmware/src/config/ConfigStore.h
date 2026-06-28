#pragma once
#include <Arduino.h>
#include "../config.h"
#include "../irrigation/ValveController.h"

constexpr uint32_t CONFIG_MAGIC   = 0x49525247;  // 'IRRG'
constexpr uint8_t  CONFIG_VERSION = 2;

enum class ConfigSource : uint8_t {
  NONE = 0,
  PERSISTED,
  HA,
};

struct PersistedConfig {
  uint32_t magic;
  uint8_t  version;
  uint8_t  configured;
  uint8_t  _pad[2];
  uint16_t settleDurationS;
  uint16_t maxPulseDurationS;
  uint32_t failsafeDisconnectS;
  uint32_t crc;
};

class ConfigStore {
public:
  bool load(PersistedConfig& out) const;
  bool save(const PersistedConfig& cfg) const;
  static uint32_t computeCrc(const PersistedConfig& cfg);
  static void valveParamsToPersisted(const ValveParams& p, PersistedConfig& cfg);
  static void persistedToValveParams(const PersistedConfig& cfg, ValveParams& p);
};

inline const char* configSourceStr(ConfigSource s) {
  switch (s) {
    case ConfigSource::HA:        return "ha";
    case ConfigSource::PERSISTED: return "persisted";
    default:                      return "none";
  }
}
