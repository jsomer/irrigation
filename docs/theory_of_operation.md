# Theory of Operation — Irrigation Controller

## Purpose

The irrigation controller is an Arduino UNO R4 WiFi-based embedded system that
monitors soil moisture across multiple sensor zones and drives a single shared
solenoid valve in metered pulses. It integrates with Home Assistant over MQTT for
monitoring, zone classification, pulsing decisions, and manual override.

**Control authority:** Home Assistant is the authoritative controller for when to
water. The firmware still enforces hard safety limits independently.

---

## Hardware

| Signal | Pin |
|---|---|
| Solenoid valve | D5 (single valve) |
| Sensor 0 VH400 | A0 |
| Sensor 1 VH400 | A1 |
| Status LED | LED_BUILTIN |

See [hardware.md](hardware.md) for wiring, relay/MOSFET driver, and BOM.  
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
| Max runtime / day | 3600 s |
| Min settle gap | 60 s |

**MQTT layer — `MqttManager`**  
WiFi reconnect every 30 s if dropped; MQTT reconnect every 5 s. On connect:
subscribe to command/config topics, publish LWT `online`, publish MQTT Discovery
configs, then telemetry every 10 s.

**On-device auto-trigger (neutralised by HA)**  
Firmware can pulse when any sensor VWC < `resume_vwc`. HA pushes
`resume_vwc: 0.0` on every connect so this path never fires; HA drives pulses
via `valve/command` instead.

---

## Main Loop

1. **WiFi** — `maintainWiFi()` every 30 s.
2. **MQTT** — `mqtt.loop()`.
3. **Sensors** — every 5 s, update `latestVWC[]`, run auto-trigger (inactive when HA sets `resume_vwc = 0`).
4. **Valve** — `valve.update()` state machine.
5. **Failsafe** — close valve if MQTT disconnected ≥ 120 s.
6. **Telemetry** — every 10 s, sensor + valve JSON; blink status LED.

Serial boot waits up to 3 s for a monitor, then continues headless.

---

## Home Assistant — Authoritative Control

### MQTT Discovery entities

Firmware registers entities under device **"Irrigation Controller"**. Home
Assistant prefixes entity IDs with the device slug:

| Purpose | Example entity ID |
|---------|-------------------|
| Sensor 0 VWC | `sensor.irrigation_controller_irrigation_sensor_0_vwc` |
| Sensor 1 VWC | `sensor.irrigation_controller_irrigation_sensor_1_vwc` |
| Valve open | `binary_sensor.irrigation_controller_irrigation_valve` |
| Valve state | `sensor.irrigation_controller_irrigation_valve_state` |
| Error code | `sensor.irrigation_controller_irrigation_error_code` |
| Online | `binary_sensor.irrigation_controller_online` |
| Pulse / Close / Clear fault | `button.irrigation_controller_irrigation_valve_pulse`, etc. |

Package-defined template sensors use shorter IDs (no device prefix), e.g.
`sensor.irrigation_sensor_0_zone`, `sensor.irrigation_pulse_decision`.

Use **Developer Tools → States** and search `irrigation` if IDs differ after an
HA upgrade.

### Five-zone moisture model (per sensor)

Four `input_number` thresholds per sensor (low → high):

```
dry limit ≤ green_low ≤ green_high ≤ high limit
```

| Zone | Colour | Pulsing intent |
|------|--------|----------------|
| VWC ≤ dry limit | red | ON (needs water) |
| dry < VWC < green_low | yellow | ON (trending dry) |
| green_low ≤ VWC ≤ green_high | green | OFF (target) |
| green_high < VWC < high limit | yellow | OFF (trending wet) |
| VWC ≥ high limit | red | OFF (saturated) |

Template sensors `sensor.irrigation_sensor_<n>_zone` expose `state` and `color`.

### Master pulse decision

`sensor.irrigation_pulse_decision` (`on` / `off`) with `reason` attribute.  
**Precedence (dry wins):** any dry → ON; else any wet → OFF; else all green → OFF;
else yellow-low → ON; else yellow-high → OFF.

### Automations (package)

| Automation | Role |
|------------|------|
| Sync Firmware On Connect | `resume_vwc: 0` per sensor + valve timing from sliders |
| Pulse Driver | Every 30 s while decision ON and valve `idle`, send `pulse` |
| Stop On Decision Off | Send `close` when decision turns OFF |
| Apply Valve Config | Push pulse/settle when sliders change |
| Valve Fault / Offline alerts | Notifications (fault includes E001–E004 text) |

Firmware safety limits still apply to every `pulse` request.

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
| `E003` | Daily runtime limit (3600 s/day) |
| `E004` | Pulse request denied |

---

## Scaling to More Sensors

1. Increment `SENSOR_COUNT` in `config.h`; add pin and `VH400` in `main.cpp`; reflash.
2. Discovery registers the new VWC sensor automatically.
3. In `irrigation.yaml`: four limit `input_number`s, zone template sensor, extend
   pulse-decision `zones` list, add `resume_vwc: 0` to Sync Firmware automation.

---

## Safety Summary

1. Hard limits in `config.h` — not overridable remotely.
2. `setParams()` clamps MQTT configure values.
3. MQTT-loss failsafe closes valve after 120 s.
4. Fault latches until `clear_fault`.
5. VWC < 1 % ignored for auto-trigger.
6. Error codes E001–E004 in telemetry and HA notifications.
