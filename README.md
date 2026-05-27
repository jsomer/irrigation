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

**One solenoid valve** is shared across all zones. The valve fires automatically when
*any* sensor VWC drops below that sensor's configured dry threshold. Moisture
differences between sensors inform physical sprinkler/soaker-hose positioning.

## Hardware

| Component | Part |
|---|---|
| Microcontroller | Arduino UNO R4 WiFi |
| Soil moisture | Vegetronix VH400 (0–3 V analog out, one per zone) |
| Valve driver | 5 V relay module or MOSFET driver (single valve) |

### Pin Assignments (configurable in `firmware/src/config.h`)

| Function | Pin |
|---|---|
| VH400 Sensor 0 | A0 |
| VH400 Sensor 1 | A1 |
| Solenoid Valve | D5 |
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

| Topic | Direction | Content |
|---|---|---|
| `irrigation/sensor/<id>/telemetry` | Device → HA | VWC reading per sensor |
| `irrigation/sensor/<id>/config` | HA → Device | Per-sensor dry threshold (`resume_vwc`) |
| `irrigation/valve/telemetry` | Device → HA | Valve state, runtime counters, fault |
| `irrigation/valve/command` | HA → Device | `pulse`, `close`, `clear_fault`, `configure` |
| `irrigation/valve/status` | Device → HA | `online` / `offline` (retained, LWT) |

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
ready-to-use HA package defining sensors, binary sensors, buttons, input helpers, and automations.

**To enable the package**, add this to your `configuration.yaml`:

```yaml
homeassistant:
  packages:
    irrigation: !include packages/irrigation.yaml
```

Then copy `homeassistant/packages/irrigation.yaml` into your HA `config/packages/` directory
and restart HA.

### Dashboard

A Lovelace dashboard YAML is maintained in the project. It provides:

- **Moisture history graph** — 48-hour VWC trend for all sensors
- **Current moisture glance** — live VWC reading per sensor
- **Per-sensor dry threshold sliders** — `resume_vwc` changes are automatically published to the firmware via MQTT
- **Valve timing controls** — pulse duration and settle wait sliders (shared across all sensors)
- **Measurement interval controls** — sensor read and telemetry publish intervals

### Sending commands from HA

```yaml
# Trigger a pulse
service: mqtt.publish
data:
  topic: irrigation/valve/command
  payload: '{"action": "pulse"}'

# Update valve timing (clamped to hard safety limits on the controller)
service: mqtt.publish
data:
  topic: irrigation/valve/command
  payload: '{"action": "configure", "pulse_duration_s": 30, "settle_duration_s": 300}'

# Update sensor dry threshold
service: mqtt.publish
data:
  topic: irrigation/sensor/0/config
  payload: '{"resume_vwc": 25.0}'
```

## Scaling to More Sensors

1. Increment `SENSOR_COUNT` in `firmware/src/config.h`.
2. Add a `Pin::SENSOR_N` constant and extend `SENSOR_PINS` in `main.cpp`.
3. Re-flash the firmware.
4. Add corresponding sensor entity entries in `homeassistant/packages/irrigation.yaml`.

## Project Structure

```
firmware/
  platformio.ini
  src/
    config.h              pins, topics, safety limits, defaults
    secrets.h             WiFi + MQTT credentials (gitignored)
    secrets.h.example     template — commit this, not secrets.h
    log.h                 Serial logging macros
    main.cpp              setup/loop, auto-trigger, failsafe
    sensors/
      VH400.h/.cpp        ADC read + Vegetronix piecewise calibration
    irrigation/
      ValveController.h/.cpp  pulse/settle state machine + safety enforcement
    mqtt/
      MqttManager.h/.cpp  MQTT connect/reconnect, pub/sub, JSON parsing
schemas/
  mqtt_topics.md          MQTT topic reference + JSON schemas
homeassistant/
  packages/
    irrigation.yaml       HA package: sensors, buttons, input helpers, automations
docs/
  theory_of_operation.md  detailed firmware and protocol design notes
```
