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

**Control authority.** The firmware can self-trigger when any sensor drops below
its dry threshold, but the Home Assistant package makes HA the authoritative
controller: it pushes `resume_vwc = 0.0` to every sensor (disabling the on-device
auto-trigger, since no real reading can fall below 0) and then drives the valve
exclusively via `pulse` / `close` commands based on a five-zone moisture model.
The firmware's hard safety limits stay active as an independent backstop.

### Moisture zones (per sensor)

Each sensor has four configurable thresholds, ordered low → high:

```
 RED (dry) | YELLOW (low) | GREEN (target) | YELLOW (high) | RED (saturated)
          dry           green_low       green_high        high
         limit                                            limit
```

| Zone condition | Colour | Pulsing intent |
|---|---|---|
| `VWC ≤ dry limit` | red | ON (needs water) |
| `dry < VWC < green_low` | yellow | ON (trending dry) |
| `green_low ≤ VWC ≤ green_high` | green | OFF (in target) |
| `green_high < VWC < high limit` | yellow | OFF (trending wet) |
| `VWC ≥ high limit` | red | OFF (saturated) |

**Pulsing decision precedence** (single shared valve, **dry wins**):

1. Any sensor dry → **ON**
2. Else any sensor saturated → **OFF**
3. Else all sensors green → **OFF**
4. Else a yellow-low sensor → **ON**; a yellow-high sensor → **OFF**

The current decision and the human-readable reason are exposed as
`sensor.irrigation_pulse_decision` (state `on`/`off`, with a `reason` attribute)
and shown on the dashboard.

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

On every MQTT connect the firmware publishes **MQTT Discovery** configs to
`homeassistant/<domain>/<id>/config`. HA auto-creates all sensor, binary sensor,
and button entities and groups them under a single **"Irrigation Controller"**
device — no manual entity YAML required.

`homeassistant/packages/irrigation.yaml` provides the remaining HA-native
elements: `input_number` sliders for valve timing and per-sensor moisture limits
(dry / green-band low / green-band high / high), template sensors that classify
each sensor's zone + colour and compute the master pulse decision, and the
automations that disable the firmware auto-trigger, drive pulses, and force-close
the valve.

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
- **Current moisture gauges** — live VWC per sensor (built-in gauge; reddens only at the low end)
- **Zone & pulsing status** — per-sensor five-zone colour (🟢/🟡/🔴) plus the current pulsing state and the reason for it
- **Valve status** — state, pulse decision + reason, pulse count, runtime today/hour
- **Manual controls** — Pulse, Force Close, Clear Fault buttons
- **Valve timing sliders** — pulse duration and settle wait (pushed to firmware automatically)
- **Moisture limit sliders** — per-sensor dry / green-low / green-high / high limits that drive the zone + pulsing logic
- **Measurement intervals** — fixed firmware constants (5 s read / 10 s telemetry); change in `config.h` and reflash

> The colour chip uses emoji so it works with zero HACS dependencies. To colour
> the gauges themselves red at *both* ends, swap them for a custom card
> (Mushroom, button-card, or apexcharts-card) driven by the
> `sensor.irrigation_sensor_<n>_zone` `color` attribute.

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

# Sensor dry threshold on the device is held at 0 by HA (auto-trigger disabled).
# HA owns the dry/high/green limits; the device only enforces hard safety caps.
service: mqtt.publish
data:
  topic: irrigation/sensor/0/config
  payload: '{"resume_vwc": 0.0}'
```

## Scaling to More Sensors

1. Increment `SENSOR_COUNT` in `firmware/src/config.h`.
2. Add a `Pin::SENSOR_N` constant and a `VH400` entry in `firmware/src/main.cpp`.
3. Re-flash the firmware — MQTT Discovery automatically registers the new sensor entity in HA.
4. In `homeassistant/packages/irrigation.yaml`: add the four limit `input_number`s
   (`dry` / `green_low` / `green_high` / `wet`) and an `Irrigation Sensor N Zone`
   template sensor, extend the `zones` list in `Irrigation Pulse Decision`, and
   add `resume_vwc: 0.0` to the **Sync Firmware On Connect** automation.

No valve hardware changes are required.

### Entity IDs (MQTT Discovery)

Discovery entities are grouped under device **Irrigation Controller** and use IDs
such as `sensor.irrigation_controller_irrigation_sensor_0_vwc`. Package template
sensors use shorter IDs (`sensor.irrigation_sensor_0_zone`, etc.). See
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
