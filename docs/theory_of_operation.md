# Theory of Operation — Irrigation Controller

Firmware and MQTT reference. For system purpose see [system_spec.md](system_spec.md). For HA drip logic see [ha_drip_control.md](ha_drip_control.md).

## Purpose

Arduino UNO R4 WiFi monitors soil moisture and drives a single solenoid valve in
metered pulses. Home Assistant owns all irrigation decisions; firmware executes
pulses and enforces emergency hardware limits.

**Control authority:** HA sends `configure` (settle, max pulse, failsafe) and
`pulse` (duration per cycle). Firmware boots unconfigured until HA configure or
valid persisted flash config. There is no on-device auto-trigger.

---

## Hardware

| Signal | Pin |
|---|---|
| Solenoid valve | D5 (single valve) |
| Sensor 0 VH400 | A0 |
| Sensor 1 VH400 | A1 |
| Sensor 2 VH400 | A2 |
| Sensor 3 VH400 | A3 |
| Sensor 4 VH400 | A4 |
| Sensor 5 VH400 | A5 |
| Status LED | LED_BUILTIN |

See [hardware.md](hardware.md) and [vh400_calibration.md](vh400_calibration.md).

---

## Firmware Architecture

**Sensor layer — `VH400`**  
16-sample ADC average → VWC %. Readings **< 1 %** are invalid (disconnected probe).

**Valve layer — `ValveController`**  
State machine: `IDLE → PULSING → SETTLING → IDLE`, with `FAULT` on E001.

| Limit | Source | Value |
|---|---|---|
| Max single pulse | MQTT `max_pulse_duration_s` | HA slider |
| Settle gap | MQTT `settle_duration_s` | HA slider (floor 60 s) |
| Stuck-valve cap | Firmware | 86 400 s (24 h) → E001 |
| MQTT failsafe | MQTT `failsafe_disconnect_s` | HA slider |

`configure` params persist to flash (NVS) and reload on boot. Telemetry includes
`configured`, `config_source` (`none` / `persisted` / `ha`).

Pulse denied (busy, unconfigured, zero duration) returns `false` from
`requestPulse()` — no fault latched, no error code emitted.

**MQTT layer — `MqttManager`**  
WiFi reconnect every 30 s; MQTT reconnect every 5 s. On connect: subscribe valve
command, LWT `online`, MQTT Discovery, telemetry every 10 s.

Failsafe disconnect timer starts only **after the first successful MQTT connect**.
If the broker is unreachable at boot, failsafe does not run.

During PULSING, HA can send `{"action":"close"}` for early stop.

---

## Main Loop

1. `maintainWiFi()` every 30 s
2. `mqtt.loop()`
3. Sensor read every 5 s → `latestVWC[]`
4. `valve.update()` state machine
5. Failsafe — close valve if MQTT lost ≥ `failsafe_disconnect_s` (only after first connect)
6. Telemetry every 10 s; blink status LED

Intervals: `READ_INTERVAL_MS` = 5 s, `TELEMETRY_INTERVAL_MS` = 10 s in `config.h`.

---

## MQTT Discovery entities

Device **"Irrigation Controller"**:

| Purpose | Entity ID |
|---------|-----------|
| Sensor 0–5 VWC | `sensor.irrigation_sensor_N_vwc` |
| Valve open (relay) | `binary_sensor.irrigation_valve` |
| Valve state | `sensor.irrigation_valve_state` |
| Actual pulse seconds | `sensor.irrigation_actual_pulse_s` |
| Pulse elapsed seconds | `sensor.irrigation_pulse_elapsed_s` |
| Pulse count | `sensor.irrigation_valve_pulse_count` |
| Error code | `sensor.irrigation_error_code` |
| Fault reason | `sensor.irrigation_fault_reason` |
| Configured | `binary_sensor.irrigation_firmware_configured` |
| Config source | `sensor.irrigation_config_source` |
| Controller online | `binary_sensor.irrigation_controller_online` |
| Close valve | `button.irrigation_valve_close` |
| Clear fault | `button.irrigation_clear_fault` |

Package (`irrigation.yaml` + `irrigation_sensors.yaml`) duplicates core valve entities and adds HA helpers.
See [ha_drip_control.md](ha_drip_control.md).

---

## MQTT Protocol

| Topic | Direction | Content |
|---|---|---|
| `irrigation/sensor/<id>/telemetry` | Device → HA | `{sensor, vwc, ts}` |
| `irrigation/valve/telemetry` | Device → HA | valve state, pulse metrics, faults |
| `irrigation/valve/command` | HA → Device | `pulse`, `close`, `clear_fault`, `configure` |
| `irrigation/valve/status` | Device → HA | `online` / `offline` (LWT) |

Full field list: [schemas/mqtt_topics.md](../schemas/mqtt_topics.md).

**Error codes:**

| Code | Meaning |
|------|---------|
| `none` | Healthy (also when pulse denied — denial is not a fault) |
| `E001` | Pulse exceeded hardware stuck-valve limit (86 400 s) |

---

## Safety Summary

1. Operational limits from HA `configure`; persisted to flash.
2. Hardware stuck-valve cap — 86 400 s (E001).
3. Min settle floor — 60 s in firmware.
4. MQTT-loss failsafe closes open valve after `failsafe_disconnect_s` (post-first-connect only).
5. Fault latches until `clear_fault`.
6. VWC < 1 % ignored by HA freshness checks.

---

## Sensor slots (0–5)

Firmware reads all six analog inputs (A0–A5) and publishes `irrigation/sensor/<id>/telemetry` for each. Home Assistant uses `input_boolean.irrigation_sensor_N_enabled` to include or exclude slots from auto logic without reflashing.

To add a probe: wire to the next free pin, enable the slot in HA, set thresholds. Gaps are allowed (e.g. S0 and S2 enabled, S1 disabled).
