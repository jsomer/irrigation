# MQTT Topic Schema

**Hardware:** Arduino UNO R4 WiFi  
**Broker:** Mosquitto (hosted on Home Assistant server)  
**Protocol:** MQTT 3.1.1

---

## Architecture

One solenoid **valve** is shared across all moisture measurement **sensors**.

**Production control:** Firmware auto-triggers when any sensor VWC drops below its
`resume_vwc` threshold. Home Assistant pushes threshold and timing values from
sliders, provides monitoring and manual override, and syncs config on connect.

```
Sensor 0 (A0) ──┐
Sensor 1 (A1) ──┤── firmware auto-trigger ──► Valve (Pin 5)
...             ┘         ▲
                          │ MQTT configure / manual pulse
                     Home Assistant
```

---

## Topics

### Published by UNO R4 WiFi

#### Sensor Telemetry

**Topic:** `irrigation/sensor/<id>/telemetry`  
**Retained:** No  
**QoS:** 0  
**Interval:** Every `TELEMETRY_INTERVAL_MS` (default 10 s; compile-time constant)

| Field    | Type   | Unit | Description                        |
|----------|--------|------|------------------------------------|
| `sensor` | uint8  | —    | Sensor index (0-based)             |
| `vwc`    | float  | %    | Volumetric water content           |
| `ts`     | uint32 | s    | Uptime timestamp (millis/1000)     |

```json
{"sensor":0,"vwc":18.3,"ts":1024}
```

#### Valve Telemetry

**Topic:** `irrigation/valve/telemetry`  
**Retained:** No  
**QoS:** 0  
**Interval:** Same as sensor telemetry

| Field             | Type   | Unit | Description                        |
|-------------------|--------|------|------------------------------------|
| `valve_open`      | bool   | —    | Current relay state                |
| `runtime_today_s` | uint32 | s    | Valve-open seconds today           |
| `runtime_hour_s`  | uint32 | s    | Valve-open seconds this hour       |
| `pulse_count`     | uint32 | —    | Total pulses since boot            |
| `state`           | string | —    | `idle` / `pulsing` / `settling` / `fault` |
| `error_code`      | string | —    | `none` when healthy; see table below when faulted |
| `fault_reason`    | string | —    | Human-readable detail; present only when `state=fault` |
| `ts`              | uint32 | s    | Uptime timestamp                   |

**Error codes:**

| Code | Condition |
|------|-----------|
| `none` | No fault |
| `E001` | Pulse exceeded 120 s hard limit (hit during active pulse) |
| `E002` | Hourly runtime budget exhausted (600 s/hr) |
| `E003` | Daily runtime budget exhausted (configured `max_runtime_day_s`) |
| `E004` | Pulse request denied (unexpected state) |

```json
{"valve_open":false,"runtime_today_s":210,"runtime_hour_s":210,"pulse_count":7,"state":"settling","error_code":"none","ts":2117}
```

Fault example:
```json
{"valve_open":false,"runtime_today_s":620,"runtime_hour_s":620,"pulse_count":21,"state":"fault","error_code":"E002","fault_reason":"hourly runtime limit exceeded","ts":4301}
```

#### Valve Status (LWT)

**Topic:** `irrigation/valve/status`  
**Retained:** Yes  
**QoS:** 1  
**Values:** `online` | `offline`

Published `online` on connect; broker publishes `offline` via Last Will on disconnect.

---

### Subscribed by UNO R4 WiFi

#### Valve Command

**Topic:** `irrigation/valve/command`  
**Direction:** HA → Device

All payloads are JSON. The `action` field selects the operation.

| Action        | Extra fields                                      | Effect                                   |
|---------------|---------------------------------------------------|------------------------------------------|
| `pulse`       | —                                                 | Immediately start a pulse cycle          |
| `close`       | —                                                 | Force valve closed                       |
| `clear_fault` | —                                                 | Clear FAULT state                        |
| `configure`   | `pulse_duration_s`, `settle_duration_s`, `max_runtime_day_s` | Valve timing + daily open-time budget |

```json
{"action":"pulse"}
{"action":"configure","pulse_duration_s":30,"settle_duration_s":300,"max_runtime_day_s":3600}
```

`max_runtime_day_s` is the maximum total valve-open seconds per rolling 24 h window
(default 3600 s / 60 min; configurable via MQTT, hard cap 28 800 s).

**Safety limits (firmware-enforced, cannot be overridden):**

| Limit                     | Value |
|---------------------------|-------|
| Max pulse duration        | 120 s |
| Max runtime / hour        | 600 s |
| Max runtime / day (configurable) | 60 s – 28 800 s |
| Min settle gap            | 60 s  |
| Failsafe disconnect close | 120 s |

#### Sensor Config

**Topic:** `irrigation/sensor/<id>/config`  
**Direction:** HA → Device

| Field       | Type  | Unit | Description                     |
|-------------|-------|------|---------------------------------|
| `resume_vwc`| float | %    | Auto-trigger dry threshold      |

```json
{"resume_vwc":25.0}
```

---

## Home Assistant entity IDs (MQTT Discovery)

Discovery registers entities under device **Irrigation Controller** using
short `object_id` values that become the entity ID directly:

| `object_id` (firmware) | Entity ID |
|------------------------|-----------|
| `irrigation_sensor_0_vwc` | `sensor.irrigation_sensor_0_vwc` |
| `irrigation_sensor_1_vwc` | `sensor.irrigation_sensor_1_vwc` |
| `irrigation_valve_state` | `sensor.irrigation_valve_state` |
| `irrigation_valve` | `binary_sensor.irrigation_valve` |
| `irrigation_error_code` | `sensor.irrigation_error_code` |
| `irrigation_controller_online` | `binary_sensor.irrigation_controller_online` |
| `irrigation_valve_pulse` | `button.irrigation_valve_pulse` |

After upgrading from an older build, remove stale entities that used the longer
`irrigation_controller_irrigation_*` prefix.

---

## Scaling

To add a third (or more) sensor:

1. Increment `SENSOR_COUNT` in `config.h`.
2. Add `Pin::SENSOR_N` and a `VH400` instance in `main.cpp`.
3. Re-flash the firmware — Discovery registers `irrigation_sensor_<n>_vwc`.
4. Extend `homeassistant/packages/irrigation.yaml` (resume_vwc slider, apply
   automation, sync-on-connect).
