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

**One solenoid valve** is shared across all sensor zones. Moisture differences
between sensors inform physical sprinkler/soaker-hose positioning rather than
which valve to open.

**Control authority.** Firmware auto-triggers when any sensor VWC drops below its
`resume_vwc` dry threshold. Home Assistant pushes threshold and timing sliders to
the device, provides monitoring and manual override (`pulse` / `close`), and
syncs config when the controller reconnects. Firmware hard safety limits stay
active as an independent backstop.

## Hardware

| Component | Part |
|---|---|
| Microcontroller | Arduino UNO R4 WiFi |
| Soil moisture | Vegetronix VH400 (0–3 V analog out, one per zone) |
| Valve driver | 5 V relay module, optocoupler input (< 5 mA trigger on D5) |

### Pin Assignments (configurable in `firmware/src/config.h`)

| Function | Pin |
|---|---|
| VH400 Sensor 0 | A0 |
| VH400 Sensor 1 | A1 |
| Solenoid (via relay IN) | D5 |
| Status LED | LED_BUILTIN |

## Firmware Setup

```bash
# 1. Install PlatformIO (VS Code extension or CLI)

# 2. Copy secrets template
cp firmware/src/secrets.h.example firmware/src/secrets.h
# Edit secrets.h with your WiFi + MQTT credentials

# 3. Build and upload (UNO connected via USB)
cd firmware
pio run -e uno_r4_wifi -t upload

# 4. Monitor serial output
pio device monitor
```

On boot you should see `MQTT connected` and telemetry log lines every 10 s.
Topics use `irrigation/sensor/<id>/…` and `irrigation/valve/…`.

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
| Max runtime per day | Configurable via MQTT (default 60 min, max 480 min); firmware hard cap 8 hr |
| Minimum settle time between pulses | 60 s |
| Failsafe: close valve on MQTT loss after | 120 s |

## Home Assistant Integration

On every MQTT connect the firmware publishes **MQTT Discovery** configs to
`homeassistant/<domain>/<id>/config`. HA auto-creates all sensor, binary sensor,
and button entities and groups them under a single **"Irrigation Controller"**
device — no manual entity YAML required.

`homeassistant/packages/irrigation.yaml` provides the remaining HA-native
elements: `input_number` sliders for valve timing and per-sensor dry thresholds,
template sensors for human-readable duration display, and automations that push
slider values to the firmware and sync on reconnect.

**To enable the package**, add this to your `configuration.yaml`:

```yaml
homeassistant:
  packages:
    irrigation: !include packages/irrigation.yaml
```

Then copy `homeassistant/packages/irrigation.yaml` into your HA `config/packages/`
directory and restart HA.

**Mosquitto add-on settings** (recommended):
- Start on boot: **on**
- Watchdog: **on** — auto-restarts the broker if it stops unexpectedly

### Dashboard

Copy `homeassistant/dashboards/irrigation.yaml` into a new HA dashboard via the
raw config editor. It provides:

- **Moisture history graph** — 48-hour VWC trend for all sensors
- **Current moisture gauges** — live VWC per sensor
- **Valve status** — state, pulse count, runtime today/hour
- **Manual controls** — Pulse, Force Close, Clear Fault buttons
- **Valve timing sliders** — pulse duration and settle wait (pushed to firmware automatically)
- **Dry threshold sliders** — per-sensor `resume_vwc` (pushed to firmware automatically)
- **Measurement intervals** — fixed firmware constants (5 s read / 10 s telemetry); change in `config.h` and reflash

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
  payload: '{"action": "configure", "pulse_duration_s": 30, "settle_duration_s": 300, "max_runtime_day_s": 3600}'

# Set per-sensor dry threshold (auto-trigger fires when VWC drops below this)
service: mqtt.publish
data:
  topic: irrigation/sensor/0/config
  payload: '{"resume_vwc": 25.0}'
```

## Scaling to More Sensors

1. Increment `SENSOR_COUNT` in `firmware/src/config.h`.
2. Add a `Pin::SENSOR_N` constant and a `VH400` entry in `firmware/src/main.cpp`.
3. Re-flash the firmware — MQTT Discovery automatically registers the new sensor entity in HA.
4. In `homeassistant/packages/irrigation.yaml`: add a `resume_vwc` `input_number`,
   an Apply Sensor N Config automation, and extend Sync Firmware On Connect.

No valve hardware changes are required.

### Entity IDs (MQTT Discovery)

Discovery entities use short IDs such as `sensor.irrigation_sensor_0_vwc`,
`binary_sensor.irrigation_valve`, and `sensor.irrigation_valve_state`. If you
upgraded from an older build, delete stale `irrigation_controller_irrigation_*`
entities from **Settings → Devices & services → MQTT**. See
[docs/theory_of_operation.md](docs/theory_of_operation.md) for the full table.

## Project Structure

```
firmware/
  platformio.ini          board, libraries (PubSubClient, ArduinoJson), build envs
  src/
    config.h              pins, #define MQTT topics, safety limits, defaults
    secrets.h             WiFi + MQTT credentials (gitignored)
    secrets.h.example     template — commit this, not secrets.h
    log.h                 Serial logging macros
    main.cpp              setup/loop, auto-trigger, failsafe
    sensors/
      VH400.h/.cpp        ADC read + Vegetronix piecewise calibration
    irrigation/
      ValveController.h/.cpp  pulse/settle state machine + safety enforcement
    mqtt/
      MqttManager.h/.cpp  MQTT connect/reconnect, pub/sub, discovery, JSON
schemas/
  mqtt_topics.md          MQTT topic reference + JSON schemas
homeassistant/
  packages/
    irrigation.yaml       HA package: input helpers, template sensors, automations
  dashboards/
    irrigation.yaml       Lovelace dashboard YAML
docs/
  hardware.md             Bill of materials, pin connections, wiring diagrams
  vh400_calibration.md    ADC reference, piecewise VWC formulas
  theory_of_operation.md  Firmware, HA control model, MQTT, entity IDs
```
