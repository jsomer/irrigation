# Theory of Operation — Irrigation Controller

## Purpose

The irrigation controller is an Arduino UNO R4 WiFi-based embedded system that
monitors soil moisture across multiple sensor zones and drives a single shared
solenoid valve in metered pulses. It integrates with Home Assistant over MQTT for
monitoring, configuration, manual override, and alerts.

**Control authority:** Home Assistant runs the drip irrigation algorithm when
`irrigation_control_mode` is `auto`. Firmware acts as sensor/actuator with
MQTT-configurable safety backstops. On-device auto-trigger is **disabled by
default** (`auto_trigger_enabled: false`); enable only via `firmware_fallback` mode.

---

## Hardware

| Signal | Pin |
|---|---|
| Solenoid valve | D5 (single valve) |
| Sensor 0 VH400 | A0 |
| Sensor 1 VH400 | A1 |
| Status LED | LED_BUILTIN |

See [hardware.md](hardware.md) for wiring (optocoupler relay module, < 5 mA IN), and BOM.  
See [vh400_calibration.md](vh400_calibration.md) for ADC and VWC conversion.

---

## Firmware Architecture

Three layers under a single Arduino `loop()`.

**Sensor layer — `VH400`**  
Averages 16 ADC samples, converts voltage to VWC % (3.3 V ADC reference), clamps
0–100 %. Readings **< 1 %** are invalid (disconnected probe).

**Valve layer — `ValveController`**  
Single shared valve; state machine: `IDLE → PULSING → SETTLING → IDLE`, with
`FAULT` on safety violations (requires `clear_fault`).

| Safety limit | Value |
|---|---|
| Max single run (MQTT `max_pulse_duration_s`) | HA slider (default 20 min) |
| Max runtime / hour (MQTT `max_runtime_hour_s`) | HA slider (default 30 min) |
| Max runtime / day (MQTT `max_runtime_day_s`) | HA slider (default 120 min) |
| Emergency pulse cap (firmware) | 7200 s |
| Min settle gap | 60 s |
| MQTT failsafe (MQTT `failsafe_disconnect_s`) | HA slider (default 30 min) |

**MQTT layer — `MqttManager`**  
WiFi reconnect every 30 s if dropped; MQTT reconnect every 5 s. On connect:
subscribe to command/config topics, publish LWT `online`, publish MQTT Discovery
configs, then telemetry every 10 s.

**On-device auto-trigger**  
Optional. Disabled by default (`auto_trigger_enabled: false`). When enabled
(via `firmware_fallback` mode), firmware pulses when any valid sensor VWC drops
below its `resume_vwc` (default 35 %). HA pushes `resume_vwc` on connect and
when sliders change.

During PULSING, HA can send `{"action":"close"}` for early stop (max-VWC logic).

---

## Main Loop

1. **WiFi** — `maintainWiFi()` every 30 s.
2. **MQTT** — `mqtt.loop()`.
3. **Sensors** — every 5 s, update `latestVWC[]`, run auto-trigger if enabled.
4. **Valve** — `valve.update()` state machine.
5. **Failsafe** — close valve if MQTT disconnected ≥ `failsafe_disconnect_s` (default 1800 s).
6. **Telemetry** — every 10 s, sensor + valve JSON; blink status LED.

Sensor read and telemetry intervals are compile-time constants in `config.h`
(`READ_INTERVAL_MS` = 5 s, `TELEMETRY_INTERVAL_MS` = 10 s). Changing them
requires a reflash.

Serial boot waits up to 3 s for a monitor, then continues headless.

---

## Home Assistant Integration

### MQTT Discovery entities

Firmware registers entities under device **"Irrigation Controller"** using short
`object_id` values. Typical entity IDs:

| Purpose | Entity ID |
|---------|-----------|
| Sensor 0 VWC | `sensor.irrigation_sensor_0_vwc` |
| Sensor 1 VWC | `sensor.irrigation_sensor_1_vwc` |
| Valve open | `binary_sensor.irrigation_valve` |
| Valve state | `sensor.irrigation_valve_state` |
| Error code | `sensor.irrigation_error_code` |
| Online | `binary_sensor.irrigation_controller_online` |
| Pulse / Close / Clear fault | `button.irrigation_valve_pulse`, etc. |

If you previously used an older firmware build, stale entities with the longer
`irrigation_controller_irrigation_*` prefix may remain in HA. Delete those from
**Settings → Devices & services → MQTT → Entities** after reflashing.

Use **Developer Tools → States** and search `irrigation` if IDs differ after an
HA upgrade.

The package also defines `*_resolved` template sensors that bridge short and
legacy long entity IDs. Automations use resolved entities; see
[ha_drip_control.md](ha_drip_control.md).

### Package helpers (`irrigation.yaml`)

| Helper | Role |
|--------|------|
| `input_number.irrigation_duration_min` | Pulse length (pushed to firmware) |
| `input_number.irrigation_settle_min` | Settle gap (pushed to firmware) |
| `input_number.irrigation_sensor0_resume_vwc` | Dry threshold sensor 0 |
| `input_number.irrigation_sensor1_resume_vwc` | Dry threshold sensor 1 |
| `input_number.irrigation_sensor0_target_vwc` | Target moisture sensor 0 |
| `input_number.irrigation_sensor1_target_vwc` | Target moisture sensor 1 |
| `input_number.irrigation_sensor0_max_vwc` | Max moisture / early stop sensor 0 |
| `input_number.irrigation_sensor1_max_vwc` | Max moisture / early stop sensor 1 |
| `input_select.irrigation_control_mode` | `disabled` / `manual` / `auto` / `firmware_fallback` |

### Home Assistant drip control

HA runs the drip algorithm, leak detection, and cycle logging when control mode
is `auto`. See [ha_drip_control.md](ha_drip_control.md) for the full script and
automation catalog.

Firmware safety limits still apply to every pulse, whether HA-triggered,
auto-triggered, or manual.

---

## MQTT Protocol

| Topic | Direction | Content |
|---|---|---|
| `irrigation/sensor/<id>/telemetry` | Device → HA | `{sensor, vwc, ts}` |
| `irrigation/sensor/<id>/config` | HA → Device | `{resume_vwc}` |
| `irrigation/valve/telemetry` | Device → HA | `{valve_open, state, error_code, runtime_*, pulse_count, ts}` |
| `irrigation/valve/command` | HA → Device | `pulse`, `close`, `clear_fault`, `configure` |
| `irrigation/valve/status` | Device → HA | `online` / `offline` (LWT) |

**Error codes** (`error_code` in valve telemetry):

| Code | Meaning |
|------|---------|
| `none` | Healthy |
| `E001` | Pulse exceeded emergency hard limit (7200 s) |
| `E002` | Hourly runtime limit (`max_runtime_hour_s`, default 1800 s) |
| `E003` | Daily runtime limit (configured cap) |
| `E004` | Pulse request denied |

---

## Scaling to More Sensors

1. Increment `SENSOR_COUNT` in `config.h`; add pin and `VH400` in `main.cpp`; reflash.
2. Discovery registers the new VWC sensor automatically.
3. In `irrigation.yaml`: add a `resume_vwc` `input_number`, an Apply Sensor N
   Config automation, and extend Sync Firmware On Connect.

---

## Safety Summary

1. Hard limits in `config.h` — not overridable remotely (7200 s pulse/hour, 28800 s/day).
2. `setParams()` clamps MQTT configure values to emergency ceilings.
3. MQTT-loss failsafe closes valve after `failsafe_disconnect_s` (default 1800 s).
4. Fault latches until `clear_fault`.
5. VWC < 1 % ignored for auto-trigger; HA blocks auto start on invalid readings.
6. Error codes E001–E004 in telemetry.
7. HA adds a stuck-valve overrun backstop (`max_single_run_min + 2 min`) — see [ha_drip_control.md](ha_drip_control.md).
