# Smart Irrigation Controller

ESP32-based soil-moisture-driven irrigation system with MQTT telemetry,
Home Assistant integration, and an AI parameter-tuning layer.

## Architecture

```
VH400 Sensors → ESP32 → MQTT → Home Assistant (dashboards/history)
                                     ↕
                               AI Analysis Layer
                                     ↕ (recommend only)
                               ESP32 (enforces hard limits)
```

## Hardware

| Component | Part |
|---|---|
| Microcontroller | ESP32 DevKit v1 |
| Soil moisture | Vegetronix VH400 (0–3 V analog out) |
| Valve driver | 5 V relay module or MOSFET driver |

### Pin Assignments (configurable in `firmware/src/config.h`)

| Function | GPIO |
|---|---|
| VH400 Zone 0 | 34 |
| VH400 Zone 1 | 35 |
| Valve Zone 0 | 26 |
| Valve Zone 1 | 27 |
| Status LED | 2 (built-in) |

## Firmware Setup

```bash
# 1. Install PlatformIO (VS Code extension or CLI)

# 2. Copy secrets template
cp firmware/src/secrets.h.example firmware/src/secrets.h
# Edit secrets.h with your WiFi + MQTT credentials

# 3. Build and upload
cd firmware
pio run -e esp32dev -t upload

# 4. Monitor serial output
pio device monitor
```

## MQTT Topics

See [schemas/mqtt_topics.md](schemas/mqtt_topics.md) for the full schema.

Quick reference:
- **Telemetry**: `irrigation/zone/<id>/telemetry` — VWC, valve state, runtime counters
- **Commands**: `irrigation/zone/<id>/command` — `pulse`, `close`, `clear_fault`
- **Config**: `irrigation/zone/<id>/config` — AI/HA parameter recommendations
- **Status**: `irrigation/zone/<id>/status` — `online` / `offline` (retained, LWT)

## Safety Limits (hard-coded, never overridden by MQTT)

| Limit | Value |
|---|---|
| Max single pulse duration | 120 s |
| Max runtime per hour | 600 s (10 min) |
| Max runtime per day | 3600 s (1 hr) |
| Minimum settle time between pulses | 60 s |
| Failsafe: close valve on MQTT loss after | 120 s |

## Project Structure

```
firmware/
  platformio.ini
  src/
    config.h              pins, topics, safety limits, defaults
    secrets.h             WiFi + MQTT credentials (gitignored)
    secrets.h.example     template — commit this, not secrets.h
    main.cpp              setup/loop, auto-trigger, failsafe
    sensors/
      VH400.h/.cpp        ADC read + Vegetronix piecewise calibration
    irrigation/
      ZoneController.h/.cpp  pulse/settle state machine + safety enforcement
    mqtt/
      MqttManager.h/.cpp  MQTT connect/reconnect, pub/sub, JSON parsing
schemas/
  mqtt_topics.md          MQTT topic reference + JSON schemas
homeassistant/
  packages/               HA YAML package files (TBD)
```
