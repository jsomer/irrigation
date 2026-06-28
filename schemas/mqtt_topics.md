# MQTT Topic Schema

**Hardware:** Arduino UNO R4 WiFi  
**Broker:** Mosquitto on Home Assistant

---

## Architecture

Home Assistant reads moisture telemetry and sends pulse commands. Firmware executes pulse → settle → idle.

```
Sensor 0..5 (A0–A5) ── telemetry ──► Home Assistant
                                      │
                                      │ pulse / configure / close
                                      ▼
                                 Valve (Pin 5)
```

---

## Valve telemetry

**Topic:** `irrigation/valve/telemetry`  
**Interval:** ~10 s

| Field | Type | Description |
|-------|------|-------------|
| `valve_open` | bool | Relay energized |
| `state` | string | `idle` / `pulsing` / `settling` / `fault` |
| `actual_pulse_s` | uint32 | Seconds of last completed pulse |
| `pulse_elapsed_s` | uint32 | Seconds elapsed in current pulse (0 if not pulsing) |
| `pulse_count` | uint32 | Pulses since boot |
| `error_code` | string | `none`, `E001` |
| `fault_reason` | string | Present when `state=fault` |
| `configured` | bool | Params loaded from HA or flash |
| `config_source` | string | `none` / `persisted` / `ha` |
| `ts` | uint32 | Uptime seconds |

**Error codes:**

| Code | Meaning |
|------|---------|
| `E001` | Hardware stuck-valve limit (86 400 s) |

---

## Valve command

**Topic:** `irrigation/valve/command`

| Action | Fields | Effect |
|--------|--------|--------|
| `pulse` | `pulse_duration_s` (required) | Start pulse for requested seconds (clamped to max pulse / hardware cap) |
| `close` | — | Force valve closed → idle |
| `clear_fault` | — | Clear fault → idle |
| `configure` | `settle_duration_s`, `max_pulse_duration_s`, `failsafe_disconnect_s` | Persist operational limits |

```json
{"action":"pulse","pulse_duration_s":1800}
{"action":"configure","settle_duration_s":1200,"max_pulse_duration_s":3600,"failsafe_disconnect_s":1800}
```

**Hardware-only limits (not MQTT-configurable):**

| Limit | Value |
|-------|-------|
| Stuck-valve absolute max open | 86 400 s |
| Min settle gap | 60 s |

---

## Sensor telemetry

**Topic:** `irrigation/sensor/<id>/telemetry`

```json
{"sensor":0,"vwc":18.3,"voltage":1.42,"ts":1024}
{"sensor":4,"vwc":22.1,"voltage":1.58,"ts":2048}
```

| Field | Type | Description |
|-------|------|-------------|
| `sensor` | uint8 | Sensor index (0–5) |
| `vwc` | float | Raw VWC % from firmware VH400 curve |
| `voltage` | float | Averaged signal voltage at ADC pin (V) |
| `ts` | uint32 | Uptime seconds |

---

## Valve status (LWT)

**Topic:** `irrigation/valve/status` — retained `online` / `offline`

---

## Home Assistant entities

Package defines core entities in `irrigation.yaml` and `irrigation_sensors.yaml`; firmware discovery adds matching IDs:

| Entity | Source |
|--------|--------|
| `input_boolean.irrigation_sensor_N_enabled` | Package (HA logical enable) |
| `input_text.irrigation_sensor_N_label` | Package (optional display name) |
| `sensor.irrigation_sensor_N_vwc` | HA template (calibrated) |
| `sensor.irrigation_sensor_N_vwc_raw` | HA template (firmware VWC) |
| `sensor.irrigation_sensor_N_voltage` | HA template (signal V at ADC) |
| `sensor.irrigation_valve_state` | Package + discovery |
| `binary_sensor.irrigation_valve` | Package + discovery |
| `sensor.irrigation_actual_pulse_s` | Package + discovery |
| `sensor.irrigation_pulse_elapsed_s` | Discovery |
| `sensor.irrigation_valve_pulse_count` | Discovery |
| `sensor.irrigation_error_code` | Package + discovery |
| `sensor.irrigation_fault_reason` | Discovery |
| `binary_sensor.irrigation_firmware_configured` | Discovery |
| `sensor.irrigation_config_source` | Discovery |
| `binary_sensor.irrigation_controller_online` | Package + discovery |
| `button.irrigation_valve_close` | Discovery |
| `button.irrigation_clear_fault` | Discovery |

Pulse is requested by HA scripts (`irrigation_request_pulse`), not a discovery button.
