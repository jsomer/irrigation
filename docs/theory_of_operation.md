# Theory of Operation — Irrigation Controller

## Purpose

The irrigation controller is an Arduino UNO R4 WiFi-based embedded system that
monitors soil moisture across multiple sensor zones and drives a single shared
solenoid valve in metered pulses. It integrates with Home Assistant over MQTT for
remote monitoring, manual control, and AI-assisted parameter tuning.

---

## Hardware

| Signal | Pin |
|---|---|
| Solenoid valve | D5 (single valve) |
| Sensor 0 VH400 | A0 |
| Sensor 1 VH400 | A1 |
| Status LED | LED_BUILTIN |

Solenoid output is an active-high digital signal. The VH400 is a capacitive soil
moisture probe with a 0–3 V analog output; its voltage is converted to volumetric
water content (VWC %) using a 5-segment piecewise-linear calibration per the
Vegetronix datasheet.

---

## Firmware Architecture

The firmware is structured in three layers under a single Arduino `loop()`.

**Sensor layer — `VH400`**  
Reads the analog input, averages 16 samples with 200 µs inter-sample delay,
applies the piecewise calibration, and clamps the result to 0–100 %. A reading
below 1 % is treated as invalid (disconnected or floating probe) and excluded
from the auto-trigger check.

**Valve layer — `ValveController`**  
A single controller manages the shared valve through a four-state machine:

```
IDLE → PULSING → SETTLING → IDLE
           ↘            ↗
            FAULT (explicit clear required)
```

A `requestPulse()` call is accepted only from `IDLE` and only if all safety budget
checks pass. The controller tracks runtime in rolling hourly and daily windows;
both windows reset autonomously. All parameters passed in via MQTT are clamped to
hard limits defined in `config.h` before being applied — these limits cannot be
overridden at runtime.

| Safety limit | Value |
|---|---|
| Max single pulse | 120 s |
| Max runtime / hour | 600 s |
| Max runtime / day | 3600 s |
| Min settle gap | 60 s |

**MQTT layer — `MqttManager`**  
Manages WiFi association and broker connection with 5 s auto-reconnect. On each
successful connect it:

1. Subscribes to `irrigation/valve/command` and `irrigation/sensor/<id>/config`.
2. Publishes a retained `online` message to `irrigation/valve/status`.
3. Publishes **MQTT Discovery** configs to `homeassistant/<domain>/<id>/config`
   (retained) so HA auto-creates and groups all entities under a single
   "Irrigation Controller" device — no manual YAML entity definitions required.

Telemetry is published every 10 s: one `sensor/<id>/telemetry` message per sensor
and one `valve/telemetry` message.

---

## Main Loop

Each `loop()` iteration executes five tasks in order:

1. **MQTT keepalive** — processes inbound messages and maintains broker connection.
2. **Sensor read** (every 5 s) — updates `latestVWC[]` for all sensors, skipping
   any reading < 1 % (disconnected probe), then runs the auto-trigger check.
3. **Valve tick** — advances the valve state machine.
4. **Failsafe check** — if MQTT has been disconnected for ≥ 120 s, the valve is
   force-closed if open.
5. **Telemetry publish** (every 10 s) — sends one `sensor/<id>/telemetry` message
   per sensor and one `valve/telemetry` message; blinks the status LED.

---

## Auto-trigger Logic

After each sensor read, the controller compares every valid sensor VWC against its
individually configured `resumeVWC` threshold. If *any* sensor reads below
threshold and the valve is currently `IDLE`, `requestPulse()` is called
immediately. The valve controller then enforces all safety limits before opening
the valve.

The single-valve, multi-sensor design means:

- Moisture differences between sensors are **informational** — they guide where to
  physically reposition sprinklers or soaker hose, not which valve to open.
- Home Assistant and an AI layer can issue explicit pulses via `valve/command`, or
  update per-sensor thresholds via `sensor/<id>/config`.

---

## MQTT Protocol

| Topic | Direction | Content |
|---|---|---|
| `irrigation/sensor/<id>/telemetry` | Device → HA | `{sensor, vwc, ts}` |
| `irrigation/sensor/<id>/config` | HA → Device | `{resume_vwc}` |
| `irrigation/valve/telemetry` | Device → HA | `{valve_open, state, error_code, runtime_today_s, runtime_hour_s, pulse_count, ts}` |
| `irrigation/valve/command` | HA → Device | `{action: pulse\|close\|clear_fault\|configure}` |
| `irrigation/valve/status` | Device → HA | `online` / `offline` (retained, LWT) |
| `homeassistant/<domain>/<id>/config` | Device → HA | MQTT Discovery payloads (retained) |

---

## Home Assistant Integration

On every MQTT connect the firmware publishes MQTT Discovery configs that register:

- **Sensor entities** per sensor zone: VWC %
- **Valve entities**: open/closed binary sensor, state string, error code, runtime counters, pulse count
- **Connectivity entity**: online/offline binary sensor (from LWT)
- **Button entities**: Pulse, Close, Clear Fault

All entities appear under a single **"Irrigation Controller"** device in HA's
device registry.

`irrigation.yaml` is a supporting HA package providing:

- **`input_number` helpers**: pulse duration, settle duration (shared valve
  timing), per-sensor `resume_vwc` dry thresholds — sliders automatically publish
  configure messages to the firmware via MQTT on change (1 s debounce)
- **Template sensors**: formatted m:ss display for pulse and settle durations
- **Automations**: fault alert, offline alert (after 2 min), configure-publish
  automations triggered by slider changes

The Lovelace dashboard (`homeassistant/dashboards/irrigation.yaml`) provides a
moisture history graph, current VWC gauges, zone/pulsing status, valve status
with error code, manual controls, moisture limits, and configuration sliders.
A conditional fault-alert card appears automatically when an error code is active.

---

## Scaling to More Sensors

To add a third (or more) sensor:

1. Increment `SENSOR_COUNT` in `config.h`.
2. Add `Pin::SENSOR_N` and extend `SENSOR_PINS[]` in `main.cpp`.
3. Re-flash the firmware — Discovery automatically registers the new sensor in HA.
4. Add a `resume_vwc` `input_number` helper and config-push automation in
   `irrigation.yaml` for the new sensor index.

No valve hardware changes are required; the single valve serves all sensor zones.

---

## Safety Summary

Safety is enforced in firmware and cannot be bypassed by any external system:

1. Hard limits are compile-time constants in `config.h` — no runtime path can
   change them.
2. `ValveController::setParams()` silently clamps all incoming values before
   storing them.
3. The failsafe closes the valve on loss of MQTT connectivity regardless of state.
4. Fault state latches the valve closed until an explicit `clear_fault` command is
   received.
5. Sensor readings below 1 % VWC are treated as invalid and never trigger the
   auto-pulse, preventing spurious watering from disconnected probes.
6. Structured error codes (E001–E004) are published in valve telemetry on every
   fault, giving a precise machine-readable cause for dashboard display and
   future AI analysis.
