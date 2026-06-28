# Smart Irrigation Controller

Arduino UNO R4 WiFi soil-moisture-driven irrigation system with MQTT telemetry,
Home Assistant integration, and an AI parameter-tuning layer.

## Documentation

**Start here:** [docs/system_spec.md](docs/system_spec.md) — what the system is for, moisture windows, alarms, setting tiers.

| Layer | Doc | Purpose |
|-------|-----|---------|
| L0 | [docs/system_spec.md](docs/system_spec.md) | North-star specification |
| L1 | [docs/operator_guide.md](docs/operator_guide.md) | Dashboard, alarms, troubleshooting |
| L2 | [docs/ha_drip_control.md](docs/ha_drip_control.md) | HA drip algorithm, automations |
| L2 | [docs/theory_of_operation.md](docs/theory_of_operation.md) | Firmware, MQTT, entity IDs |
| L3 | [docs/ai_tuning_guide.md](docs/ai_tuning_guide.md) | AI agent tuning workflow |
| L3 | [docs/data_extraction.md](docs/data_extraction.md) | Export cycles from Home Assistant |
| — | [docs/feature_inventory.md](docs/feature_inventory.md) | Every entity classified A–F |
| — | [docs/audit_report.md](docs/audit_report.md) | Doc gaps, phase-2 simplification backlog |
| — | [schemas/mqtt_topics.md](schemas/mqtt_topics.md) | MQTT topic reference |
| — | [schemas/irrigation_cycle_log.md](schemas/irrigation_cycle_log.md) | Cycle event schema |
| — | [homeassistant/SETUP_AFTER_RESTORE.md](homeassistant/SETUP_AFTER_RESTORE.md) | Post-restore HA checklist |

## Architecture

```
VH400 Sensors → UNO R4 WiFi → MQTT → Home Assistant (drip control, dashboards)
                                          ↕
                                    AI Analysis Layer
                                          ↕ (recommend only)
                                    UNO R4 WiFi (valve + hard limits)
```

**One solenoid valve** is shared across all sensor zones. Moisture differences
between sensors inform physical sprinkler/soaker-hose positioning rather than
which valve to open.

**What it does.** Maintain soil moisture within a programmable window (resume →
target) using metered valve pulses; alarm if water runs without moisture gain;
log cycles for offline AI tuning. See [docs/system_spec.md](docs/system_spec.md).

**Control authority.** Home Assistant runs the drip algorithm when
`irrigation_control_mode` is `auto` ([docs/ha_drip_control.md](docs/ha_drip_control.md)).
The firmware acts as sensor/actuator with MQTT-configurable safety backstops.
On-device auto-trigger is **off by default**; `firmware_fallback` is emergency-only.

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

## Safety Limits

Operational limits are **Home Assistant sliders** pushed via MQTT `configure`.
Firmware enforces budgets (E002/E003) and a hardware stuck-valve cap (E001, 24 h).

| Limit | Package default | Notes |
|---|---|---|
| Duration (pulse) | 60 min | Per-cycle run length |
| Max single run | 360 min | Firmware cap per pulse; set ≥ duration |
| Per hour | 60 min | **Cumulative** valve-open time in rolling hour |
| Per day | 720 min | Cumulative daily budget |
| MQTT failsafe close | 30 min | Close valve if MQTT lost |

See [docs/system_spec.md](docs/system_spec.md) for alarms. See
[docs/ai_tuning_guide.md](docs/ai_tuning_guide.md) for tuning.

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

Use a **single** package file only — do not use `!include_dir_named packages/`.

Deploy the package with `./scripts/deploy-ha.sh` (prints File Editor steps) or
copy `homeassistant/packages/irrigation.yaml` into HA `config/packages/` manually,
then restart HA.

Full post-restore checklist: [homeassistant/SETUP_AFTER_RESTORE.md](homeassistant/SETUP_AFTER_RESTORE.md)

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

# Set per-sensor dry threshold (used by firmware_fallback auto-trigger)
service: mqtt.publish
data:
  topic: irrigation/sensor/0/config
  payload: '{"resume_vwc": 35.0}'
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
    main.cpp              setup/loop, optional auto-trigger, failsafe
    sensors/
      VH400.h/.cpp        ADC read + Vegetronix piecewise calibration
    irrigation/
      ValveController.h/.cpp  pulse/settle state machine + safety enforcement
    mqtt/
      MqttManager.h/.cpp  MQTT connect/reconnect, pub/sub, discovery, JSON
schemas/
  mqtt_topics.md          MQTT topic reference + JSON schemas
  irrigation_cycle_log.md cycle completion event schema
homeassistant/
  packages/
    irrigation.yaml       HA package: input helpers, template sensors, automations
  dashboards/
    irrigation.yaml       Lovelace dashboard YAML
  SETUP_AFTER_RESTORE.md  post-backup HA checklist
scripts/
  deploy-ha.sh            deploy package to HA (Samba or instructions)
  export_irrigation_cycles.py  export cycle events + optional VWC history
  analyze_irrigation.py   metrics, leak simulation, HA dashboard push
docs/
  system_spec.md          North-star: purpose, moisture windows, alarms
  operator_guide.md       Dashboard map, troubleshooting
  feature_inventory.md    Entity/script classification (A–F)
  audit_report.md         Doc gaps, phase-2 simplification backlog
  hardware.md             Bill of materials, pin connections, wiring diagrams
  vh400_calibration.md    ADC reference, piecewise VWC formulas
  theory_of_operation.md  Firmware, MQTT, entity IDs
  ha_drip_control.md      HA drip algorithm, leak detection, automations
  ai_tuning_guide.md      AI agent tuning workflow
  data_extraction.md      HA export pipeline for analysis
.github/workflows/
  firmware-build.yml      CI: debug + release firmware builds
```
