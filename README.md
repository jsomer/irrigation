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
| — | [docs/firmware_deploy.md](docs/firmware_deploy.md) | Flash UNO via VS Code + PlatformIO |

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

Full guide: [docs/firmware_deploy.md](docs/firmware_deploy.md).

```bash
# 1. Install PlatformIO (VS Code extension or CLI)

# 2. Copy secrets template
cp firmware/src/secrets.h.example firmware/src/secrets.h
# Edit: WiFi, MQTT, IRRIGATION_INSTANCE_ID, IRRIGATION_INSTANCE_NAME

# 3. Build and upload (UNO connected via USB)
cd firmware
pio run -e uno_r4_wifi -t upload

# 4. Monitor serial output
pio device monitor
```

On boot you should see `MQTT connected` and telemetry under
`irrigation/<instance_id>/…`.

## MQTT Topics

See [schemas/mqtt_topics.md](schemas/mqtt_topics.md) for the multi-controller schema.

Quick reference (`<root>` = `irrigation/<instance_id>`):

| Topic | Direction | Content |
|---|---|---|
| `<root>/sensor/<n>/telemetry` | Device → HA | VWC reading per sensor |
| `<root>/valve/telemetry` | Device → HA | Valve state, pulse metrics, fault |
| `<root>/valve/command` | HA → Device | `pulse`, `close`, `clear_fault`, `configure` |
| `<root>/valve/status` | Device → HA | `online` / `offline` (retained, LWT) |

## Safety Limits

Operational limits are **Home Assistant sliders** pushed via MQTT `configure`.
Firmware enforces a hardware stuck-valve cap (E001, 24 h) and the configured
max pulse / settle / MQTT failsafe.

| Limit | Typical HA default | Notes |
|---|---|---|
| Min / max pulse | 3–60 min | Variable pulse from moisture deficit |
| Settle | 20 min | Firmware floor 60 s |
| MQTT failsafe close | 30 min | Close valve if MQTT lost (after first connect) |
| Stuck-valve cap | 24 h | Hardware E001 |

See [docs/system_spec.md](docs/system_spec.md) for alarms.

## Home Assistant Integration

On every MQTT connect the firmware publishes **MQTT Discovery** under
`homeassistant/<domain>/irrigation_<instance_id>/…`, grouped as one HA device
per controller.

Instance-scoped packages (helpers, drip automations, calibrated VWC) are
**generated** from templates:

```bash
# Edit homeassistant/instances.yaml and homeassistant/templates/, then:
python3 scripts/render_ha_instances.py
./scripts/deploy-ha.sh
```

**configuration.yaml** (example for `raised_bed` + `vegetable_garden`):

```yaml
homeassistant:
  packages:
    irrigation_raised_bed: !include packages/irrigation_raised_bed.yaml
    irrigation_sensors_raised_bed: !include packages/irrigation_sensors_raised_bed.yaml
    irrigation_vegetable_garden: !include packages/irrigation_vegetable_garden.yaml
    irrigation_sensors_vegetable_garden: !include packages/irrigation_sensors_vegetable_garden.yaml
```

Full checklist: [homeassistant/SETUP_AFTER_RESTORE.md](homeassistant/SETUP_AFTER_RESTORE.md)

**Mosquitto add-on settings** (recommended):
- Start on boot: **on**
- Watchdog: **on**

### Dashboard

Paste each `homeassistant/dashboards/irrigation_<id>.yaml` into its own Lovelace
dashboard via the raw configuration editor (never into `config/packages/`).

Tabs: Status, Moisture, Controls, Sensors, Advanced — entity IDs are
instance-scoped (`irrigation_<id>_…`).

### Sending commands from HA

Prefer dashboard buttons / scripts (`script.irrigation_<id>_request_pulse`, etc.).

```yaml
service: mqtt.publish
data:
  topic: irrigation/raised_bed/valve/command
  payload: '{"action":"pulse","pulse_duration_s":1800}'
```

## Scaling to More Sensors

Firmware already publishes sensors 0–5 (A0–A5). Enable slots in HA; no reflash
needed to add a probe on a free pin.

### Entity IDs

Discovery and packages use `irrigation_<instance_id>_<local_id>`, e.g.
`sensor.irrigation_raised_bed_sensor_0_vwc`.
See [docs/theory_of_operation.md](docs/theory_of_operation.md).

## Project Structure

```
firmware/
  platformio.ini
  src/
    config.h              pins, MQTT_ROOT_PREFIX, hardware limits
    secrets.h.example     WiFi, MQTT, IRRIGATION_INSTANCE_ID / NAME
    main.cpp
    mqtt/MqttManager.*    instance-scoped topics + discovery
homeassistant/
  instances.yaml          controllers to generate
  templates/              unscoped sources (edit these)
  packages/               generated irrigation_<id>.yaml + sensors
  dashboards/             generated irrigation_<id>.yaml
  SETUP_AFTER_RESTORE.md
scripts/
  render_ha_instances.py  generate packages/dashboards
  deploy-ha.sh            render + copy packages to HA
  export_irrigation_cycles.py
  analyze_irrigation.py
schemas/
  mqtt_topics.md          multi-controller MQTT contract
docs/
  firmware_deploy.md      VS Code + PlatformIO flash guide
  theory_of_operation.md
  ha_drip_control.md
.github/workflows/
  firmware-build.yml      CI: debug + release firmware builds
```
