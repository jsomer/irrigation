# Theory of Operation — Irrigation Controller

## Purpose

The irrigation controller is an Arduino UNO R4 WiFi-based embedded system that monitors soil moisture in two zones and drives solenoid valves to deliver water in metered pulses. It integrates with Home Assistant over MQTT for remote monitoring, manual control, and AI-assisted parameter tuning.

---

## Hardware

| Signal | Pin |
|---|---|
| Zone 0 solenoid | D5 |
| Zone 1 solenoid | D6 |
| Zone 0 VH400 sensor | A0 |
| Zone 1 VH400 sensor | A1 |
| Status LED | LED_BUILTIN |

Solenoid outputs are active-high digital signals. The VH400 is a capacitive soil moisture probe with a 0–3 V analog output; its voltage is converted to volumetric water content (VWC %) using a 5-segment piecewise-linear calibration per the Vegetronix datasheet.

---

## Firmware Architecture

The firmware is structured in three layers under a single Arduino `loop()`.

**Sensor layer — `VH400`**
Reads the analog input, averages 16 samples with 200 µs inter-sample delay, applies the piecewise calibration, and clamps the result to 0–100 %. Readings older than 30 s are marked stale.

**Zone layer — `ZoneController`**
Each zone runs a four-state machine:

```
IDLE → PULSING → SETTLING → IDLE
             ↘            ↗
              FAULT (explicit clear required)
```

A `requestPulse()` call is accepted only from `IDLE` and only if all safety budget checks pass. The controller tracks runtime in rolling hourly and daily windows; both windows reset autonomously. All parameters passed in via MQTT are clamped to hard limits defined in `config.h` before being applied — these limits cannot be overridden at runtime.

| Safety limit | Value |
|---|---|
| Max single pulse | 120 s |
| Max runtime / hour | 600 s |
| Max runtime / day | 3600 s |
| Min settle gap | 60 s |

**MQTT layer — `MqttManager`**
Manages WiFi association and broker connection with 5 s auto-reconnect. Publishes telemetry JSON every 10 s per zone and a retained `status` message used as a Last Will Testament for device-level offline detection. Subscribes to `command` and `config` topics per zone.

---

## Main Loop

Each `loop()` iteration executes five tasks in order:

1. **MQTT keepalive** — processes inbound messages and maintains broker connection.
2. **Sensor read** (every 5 s) — updates `latestVWC[]` for both zones, then runs the auto-trigger check.
3. **Zone tick** — advances each zone's state machine.
4. **Failsafe check** — if MQTT has been disconnected for ≥ 120 s, all open valves are force-closed.
5. **Telemetry publish** (every 10 s) — sends zone telemetry JSON to MQTT and blinks the status LED.

---

## Auto-trigger Logic

After each sensor read, the controller compares each zone's VWC against its configured `resumeVWC` threshold. If the zone is `IDLE` and VWC has fallen below the threshold, `requestPulse()` is called. The zone controller then enforces all safety limits before opening the valve. Home Assistant and an AI layer can also issue explicit pulses via the `command` topic, or update thresholds via the `config` topic.

---

## MQTT Protocol

Topics follow the pattern `irrigation/zone/<id>/<suffix>`.

| Topic | Direction | Content |
|---|---|---|
| `.../telemetry` | Device → HA | VWC, valve state, runtime counters, pulse count, state, fault reason |
| `.../status` | Device → HA | `online` / `offline` (retained, LWT) |
| `.../command` | HA → Device | `pulse`, `close`, `clear_fault` |
| `.../config` | HA → Device | `pulse_duration_s`, `settle_duration_s`, `target_vwc`, `resume_vwc` |

---

## Home Assistant Integration

`irrigation.yaml` is a complete HA package providing:

- **Sensor entities** per zone: VWC, state, fault reason, runtime today, runtime hour, pulse count, status
- **Binary sensor entities** per zone: valve open/closed, online/offline connectivity
- **Button entities** per zone: Pulse, Close, Clear Fault
- **Input number helpers** per zone: pulse duration, settle duration, resume VWC threshold, target VWC — sliders on the Lovelace dashboard that automatically publish `configure` messages to the firmware via MQTT on change (1 s debounce)
- **Template sensors** per zone: formatted m:ss display for pulse and settle durations
- **Automations**: fault alert notifications per zone, offline alert after 30 s MQTT loss, and configure-publish automations triggered by slider changes

The Lovelace dashboard provides a moisture history graph, current VWC glance card, per-zone threshold and timing controls, and a measurement interval section (firmware update pending).

---

## Safety Summary

Safety is enforced in firmware and cannot be bypassed by any external system:

1. Hard limits are compile-time constants — no runtime path can change them.
2. `setParams()` silently clamps all incoming values before storing them.
3. The failsafe closes valves on loss of MQTT connectivity regardless of zone state.
4. Fault state latches the zone closed until an explicit `clear_fault` command is received.
