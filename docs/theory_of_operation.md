# Theory of Operation — Irrigation Controller

## Purpose

The irrigation controller is an Arduino UNO R4 WiFi-based embedded system that
monitors soil moisture across multiple sensor zones and drives a single shared
solenoid valve in metered pulses. It integrates with Home Assistant over MQTT for
monitoring, configuration, manual override, and alerts.

**Control authority:** Firmware decides when to water. When any sensor VWC drops
below its `resume_vwc` threshold and the valve is idle, the device requests a
pulse automatically. Home Assistant pushes threshold and timing sliders to the
device and can override with manual pulse / close commands. Firmware still
enforces hard safety limits independently.

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
| Max single pulse | 120 s |
| Max runtime / hour | 600 s |
| Max runtime / day | Configurable via MQTT (default 3600 s / 60 min) |
| Min settle gap | 60 s |

**MQTT layer — `MqttManager`**  
WiFi reconnect every 30 s if dropped; MQTT reconnect every 5 s. On connect:
subscribe to command/config topics, publish LWT `online`, publish MQTT Discovery
configs, then telemetry every 10 s.

**On-device auto-trigger**  
Firmware pulses when any sensor VWC < `resume_vwc` (default 25 %). HA pushes
slider values on connect and when sliders change.

---

## Main Loop

1. **WiFi** — `maintainWiFi()` every 30 s.
2. **MQTT** — `mqtt.loop()`.
3. **Sensors** — every 5 s, update `latestVWC[]`, run auto-trigger.
4. **Valve** — `valve.update()` state machine.
5. **Failsafe** — close valve if MQTT disconnected ≥ 120 s.
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

### Package helpers (`irrigation.yaml`)

| Helper | Role |
|--------|------|
| `input_number.irrigation_pulse_duration` | Pulse length (pushed to firmware) |
| `input_number.irrigation_settle_duration` | Settle gap (pushed to firmware) |
| `input_number.irrigation_sensor0_resume_vwc` | Dry threshold sensor 0 |
| `input_number.irrigation_sensor1_resume_vwc` | Dry threshold sensor 1 |

### Automations (package)

| Automation | Role |
|------------|------|
| Sync Firmware On Connect | Push resume_vwc + valve timing from sliders on HA start / controller online |
| Apply Valve Config | Push pulse/settle when timing sliders change |
| Apply Sensor N Config | Push resume_vwc when threshold sliders change |
| Valve Fault / Offline alerts | Notifications |

Firmware safety limits still apply to every pulse, whether auto-triggered or
manual.

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
| `E001` | Pulse exceeded 120 s hard limit |
| `E002` | Hourly runtime limit (600 s/hr) |
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

1. Hard limits in `config.h` — not overridable remotely.
2. `setParams()` clamps MQTT configure values.
3. MQTT-loss failsafe closes valve after 120 s.
4. Fault latches until `clear_fault`.
5. VWC < 1 % ignored for auto-trigger.
6. Error codes E001–E004 in telemetry.
