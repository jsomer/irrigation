# MQTT Topic Schema

**Hardware:** Arduino UNO R4 WiFi  
**Broker:** Mosquitto (hosted on Home Assistant server)  
**Protocol:** MQTT 3.1.1

---

## Architecture

One solenoid **valve** is shared across all moisture measurement **sensors**.  
The valve fires automatically when *any* sensor VWC drops below that sensor's
configured `resume_vwc` threshold.  Differences in moisture between sensors
inform physical sprinkler/soaker-hose positioning.

```
Sensor 0 (A0) ──┐
Sensor 1 (A1) ──┤── auto-trigger logic ──► Valve (Pin 5)
...             ┘
```

---

## Topics

### Published by UNO R4 WiFi

#### Sensor Telemetry

**Topic:** `irrigation/sensor/<id>/telemetry`  
**Retained:** No  
**QoS:** 0  
**Interval:** Every `TELEMETRY_INTERVAL_MS` (default 10 s)

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
| `E003` | Daily runtime budget exhausted (3600 s/day) |
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
| `configure`   | `pulse_duration_s`, `settle_duration_s`           | Update valve timing parameters           |

```json
{"action":"pulse"}
{"action":"configure","pulse_duration_s":30,"settle_duration_s":300}
```

**Safety limits (firmware-enforced, cannot be overridden):**

| Limit                     | Value |
|---------------------------|-------|
| Max pulse duration        | 120 s |
| Max runtime / hour        | 600 s |
| Max runtime / day         | 3600 s |
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

## Scaling

To add a third (or more) sensor:

1. Increment `SENSOR_COUNT` in `config.h`.
2. Add a `Pin::SENSOR_N` constant and extend the `SENSOR_PINS` array in `main.cpp`.
3. Re-flash the firmware.
4. Home Assistant automatically picks up the new `irrigation/sensor/2/telemetry`
   topic — add corresponding entity entries in `irrigation.yaml`.
