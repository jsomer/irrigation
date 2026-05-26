# Smart Irrigation Controller

Arduino UNO R4 WiFi soil-moisture-driven irrigation system with MQTT telemetry,
Home Assistant integration, and an AI parameter-tuning layer.

## Architecture

```
VH400 Sensors → UNO R4 WiFi → MQTT → Home Assistant (dashboards/history)
                                           ↕
                                     AI Analysis Layer
                                           ↕ (recommend only)
                                     UNO R4 WiFi (enforces hard limits)
```

## Hardware

| Component | Part |
|---|---|
| Microcontroller | Arduino UNO R4 WiFi |
| Soil moisture | Vegetronix VH400 (0–3 V analog out) |
| Valve driver | 5 V relay module or MOSFET driver |

### Pin Assignments (configurable in `firmware/src/config.h`)

| Function | Pin |
|---|---|
| VH400 Zone 0 | A0 |
| VH400 Zone 1 | A1 |
| Valve Zone 0 | D5 |
| Valve Zone 1 | D6 |
| Status LED | LED_BUILTIN |

## Firmware Setup

```bash
# 1. Install PlatformIO (VS Code extension or CLI)

# 2. Copy secrets template
cp firmware/src/secrets.h.example firmware/src/secrets.h
# Edit secrets.h with your WiFi + MQTT credentials

# 3. Build and upload
cd firmware
pio run -e uno_r4_wifi -t upload

# 4. Monitor serial output
pio device monitor
```

## MQTT Topics

See [schemas/mqtt_topics.md](schemas/mqtt_topics.md) for the full schema.

Quick reference:
- **Telemetry**: `irrigation/zone/<id>/telemetry` — VWC, valve state, runtime counters, fault reason
- **Commands**: `irrigation/zone/<id>/command` — `pulse`, `close`, `clear_fault`, `configure`
- **Status**: `irrigation/zone/<id>/status` — `online` / `offline` (retained, LWT)

## Safety Limits (hard-coded, never overridden by MQTT)

| Limit | Value |
|---|---|
| Max single pulse duration | 120 s |
| Max runtime per hour | 600 s (10 min) |
| Max runtime per day | 3600 s (1 hr) |
| Minimum settle time between pulses | 60 s |
| Failsafe: close valve on MQTT loss after | 120 s |

## Home Assistant Integration

See [homeassistant/packages/irrigation.yaml](homeassistant/packages/irrigation.yaml) for a
ready-to-use HA package that defines sensors, binary sensors, buttons, and automations for both zones.

**To enable the package**, add this to your `configuration.yaml`:

```yaml
homeassistant:
  packages:
    irrigation: !include packages/irrigation.yaml
```

Then copy `homeassistant/packages/irrigation.yaml` into your HA `config/packages/` directory
and restart HA.

### Sending commands from HA

```yaml
# Trigger a pulse
service: mqtt.publish
data:
  topic: irrigation/zone/0/command
  payload: '{"action": "pulse"}'

# Update AI-recommended parameters (clamped to safety limits on the controller)
service: mqtt.publish
data:
  topic: irrigation/zone/0/command
  payload: '{"action": "configure", "pulse_duration_s": 30, "settle_duration_s": 300, "target_vwc": 40.0, "resume_vwc": 25.0}'
```

## Project Structure

```
firmware/
  platformio.ini
  src/
    config.h              pins, topics, safety limits, defaults
    secrets.h             WiFi + MQTT credentials (gitignored)
    secrets.h.example     template — commit this, not secrets.h
    log.h                 Serial logging macros (drop-in for esp_log)
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
  packages/
    irrigation.yaml       HA MQTT sensors, buttons, and automations
```
