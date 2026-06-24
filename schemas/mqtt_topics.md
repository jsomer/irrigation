# MQTT Topic Schema

**Hardware:** Arduino UNO R4 WiFi  
**Broker:** Mosquitto (hosted on Home Assistant server)  
**Protocol:** MQTT 3.1.1

---

## Architecture

One solenoid **valve** is shared across all moisture measurement **sensors**.

**Production control:** Home Assistant runs the drip algorithm when
`irrigation_control_mode` is `auto`. The firmware acts as sensor/actuator with
MQTT-configurable safety backstops. On-device auto-trigger is off by default;
enable only via `firmware_fallback` mode.

```
Sensor 0 (A0) ──┐
Sensor 1 (A1) ──┤── telemetry ──► Home Assistant (drip logic)
...             ┘                      │
                                       │ MQTT pulse / close / configure
                                       ▼
                                  Valve (Pin 5)
```

See [docs/ha_drip_control.md](../docs/ha_drip_control.md) for the HA state machine.

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
| `E001` | Pulse exceeded emergency hard limit (7200 s) |
| `E002` | Hourly runtime budget exhausted (`max_runtime_hour_s`, default 1800 s) |
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
| `configure`   | See configure fields below | Valve timing + runtime budgets + failsafe |

```json
{"action":"pulse"}
{"action":"configure",
 "pulse_duration_s":600,
 "settle_duration_s":1200,
 "max_pulse_duration_s":1200,
 "max_runtime_day_s":7200,
 "max_runtime_hour_s":1800,
 "failsafe_disconnect_s":1800,
 "auto_trigger_enabled":false}
```

| Configure field | Type | Description |
|-----------------|------|-------------|
| `pulse_duration_s` | uint | Irrigation cycle length (valve open) |
| `settle_duration_s` | uint | Gap after cycle before idle |
| `max_pulse_duration_s` | uint | Max single valve-open duration (HA backstop) |
| `max_runtime_day_s` | uint | Daily valve-open budget (rolling 24 h) |
| `max_runtime_hour_s` | uint | Hourly valve-open budget |
| `failsafe_disconnect_s` | uint | Close valve if MQTT lost this long |
| `auto_trigger_enabled` | bool | On-device auto-trigger (default false; HA controls) |

Operational limits are pushed from Home Assistant sliders. Firmware emergency ceilings (2 hr pulse, 2 hr/hr, 8 hr/day) cannot be overridden.

**Emergency limits (firmware only, not MQTT-configurable):**

| Limit | Value |
|-------|-------|
| Emergency max pulse | 7200 s |
| Emergency max runtime / hour | 7200 s |
| Emergency max runtime / day | 28 800 s |
| Min settle gap | 60 s |

#### Sensor Config

**Topic:** `irrigation/sensor/<id>/config`  
**Direction:** HA → Device

| Field       | Type  | Unit | Description                     |
|-------------|-------|------|---------------------------------|
| `resume_vwc`| float | %    | Dry threshold for firmware_fallback auto-trigger (default 35 %) |

```json
{"resume_vwc":35.0}
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
