#include "ConfigStore.h"
#include <Preferences.h>
#include <cstddef>
#include "../log.h"

static const char* TAG = "ConfigStore";
static const char* NVS_NAMESPACE = "irrigation";
static const char* NVS_KEY       = "cfg";

uint32_t ConfigStore::computeCrc(const PersistedConfig& cfg) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&cfg);
  size_t len = offsetof(PersistedConfig, crc);
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}

void ConfigStore::valveParamsToPersisted(const ValveParams& p, PersistedConfig& cfg) {
  cfg.settleDurationS   = p.settleDurationS;
  cfg.maxPulseDurationS = p.maxPulseDurationS;
}

void ConfigStore::persistedToValveParams(const PersistedConfig& cfg, ValveParams& p) {
  p.settleDurationS   = cfg.settleDurationS;
  p.maxPulseDurationS = cfg.maxPulseDurationS;
}

bool ConfigStore::load(PersistedConfig& out) const {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    LOG_W(TAG, "NVS open failed");
    return false;
  }

  size_t len = prefs.getBytesLength(NVS_KEY);
  if (len != sizeof(PersistedConfig)) {
    prefs.end();
    LOG_I(TAG, "No persisted config (len=%u)", (unsigned)len);
    return false;
  }

  prefs.getBytes(NVS_KEY, &out, sizeof(PersistedConfig));
  prefs.end();

  if (out.magic != CONFIG_MAGIC || out.version != CONFIG_VERSION) {
    LOG_W(TAG, "Persisted config magic/version mismatch");
    return false;
  }

  uint32_t expected = computeCrc(out);
  if (out.crc != expected) {
    LOG_W(TAG, "Persisted config CRC mismatch");
    return false;
  }

  if (!out.configured) {
    LOG_W(TAG, "Persisted config not marked configured");
    return false;
  }

  LOG_I(TAG, "Loaded persisted config");
  return true;
}

bool ConfigStore::save(const PersistedConfig& cfg) const {
  PersistedConfig toWrite = cfg;
  toWrite.magic   = CONFIG_MAGIC;
  toWrite.version = CONFIG_VERSION;
  toWrite.crc     = computeCrc(toWrite);

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    LOG_E(TAG, "NVS open for write failed");
    return false;
  }

  size_t written = prefs.putBytes(NVS_KEY, &toWrite, sizeof(PersistedConfig));
  prefs.end();

  if (written != sizeof(PersistedConfig)) {
    LOG_E(TAG, "NVS write failed");
    return false;
  }

  LOG_I(TAG, "Config saved to flash");
  return true;
}
