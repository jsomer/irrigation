# Hardware Reference

## Bill of Materials

| Qty | Component | Details | Notes |
|-----|-----------|---------|-------|
| 1 | Arduino UNO R4 WiFi | Renesas RA4M1, built-in WiFi (ESP32-S3 co-processor) | Main controller |
| 2–6 | Vegetronix VH400 | Capacitive soil moisture probe, 0–3 V analog output | One per measurement zone (up to 6 on A0–A5) |
| 1 | Solenoid valve | 24 VAC or 12 VDC irrigation valve | Any standard 3/4" irrigation solenoid |
| 1 | 5 V relay module (optocoupler input) | Logic IN on `D5`; coil powered from module VCC | Trigger current **< 5 mA** — see valve driver section |
| 1 | Power supply (valve) | Match valve voltage (24 VAC adapter or 12 VDC supply) | Separate from UNO supply |
| 1 | USB power supply | 5 V, ≥ 1 A | Powers the UNO R4 WiFi |
| — | Hookup wire | 22 AWG stranded recommended | |
| — | Weatherproof enclosure | IP65 or better if outdoors | |

---

## Pin Connections

| UNO R4 WiFi Pin | Connected To | Notes |
|-----------------|--------------|-------|
| `A0` | VH400 Sensor 0 signal wire | Analog input; 0–3 V |
| `A1` | VH400 Sensor 1 signal wire | Analog input; 0–3 V |
| `A2` | VH400 Sensor 2 signal wire | Analog input; 0–3 V (optional) |
| `A3` | VH400 Sensor 3 signal wire | Analog input; 0–3 V (optional) |
| `A4` | VH400 Sensor 4 signal wire | Analog input; 0–3 V (optional) |
| `A5` | VH400 Sensor 5 signal wire | Analog input; 0–3 V (optional) |
| `D5` | Relay module IN (optocoupler) | HIGH = valve open; GPIO sees < 5 mA |
| `5V` | VH400 sensor power (red) | All sensors share 5 V — star or daisy-chain with adequate gauge |
| `GND` | VH400 sensor ground (black); relay GND | Common ground |
| `LED_BUILTIN` | On-board LED | Blinks every telemetry interval (alive indicator) |

> **UNO R4 WiFi analog note:** `analogRead()` uses the **5 V** default reference
> (full scale 0–1023 = 0–5 V). The VH400 signal is 0–3 V, safely within range.
> `config.h` sets `Sensor::VCC = 5.0f` for the voltage conversion. Do **not** use
> 3.3 V here unless you also call `analogReference()` to match.

---

## VH400 Sensor Wiring

Each VH400 has three wires. **Vegetronix standard colours:**

| Wire colour | Connect to |
|-------------|-----------|
| Red | 5 V |
| **Black** | **Analog input pin (A0–A5 per sensor slot)** — 0–3 V output |
| **Bare / white** | **GND** (common with UNO GND) |

> **Important:** Black is the **signal** (0–3 V). Bare is **ground**. Do not swap them.
> A DMM on Black↔Bare at the probe should match UNO GND↔A0 (or A1…) within ~0.05 V.

Bury the full sensing shaft vertically in the soil. The probe needs good
contact along its entire length for accurate readings. Keep signal wires away
from the solenoid valve wiring to avoid interference.

---

## Valve Driver — Optocoupler Relay Module (standard)

This project drives the solenoid through a **5 V relay module with an optocoupler
input** (typical single-channel blue module). The UNO does not switch the solenoid
or relay coil directly.

| Interface | Specification |
|-----------|---------------|
| Control signal | `D5` → module **IN** (3.3 V logic, active HIGH = valve open) |
| Optocoupler input current | **< 5 mA** (within RA4M1 GPIO limit ~8 mA) |
| Module VCC | UNO **5 V** (powers relay coil on the module) |
| Module GND | Common with UNO GND |
| Solenoid switching | Module **COM** / **NO** (or NC) + separate valve supply |

The GPIO pin only energises the optocoupler LED inside the module. The relay coil
(~70 mA) is supplied from the module's VCC terminal, not from `D5`. Do **not**
connect a solenoid coil or bare relay coil directly to a GPIO pin.

### Wiring

```
UNO D5  ──► IN   [5 V relay module, optocoupler input]  COM ──► Solenoid terminal 1
UNO 5V  ──► VCC                                       NO  ──► Valve supply +
UNO GND ──► GND
                                                        Solenoid terminal 2 ──► Valve supply –/GND
```

Firmware: `Pin::VALVE = 5` in `config.h`; `digitalWrite(HIGH)` energises IN and
closes the relay contact to open the valve path.

### Alternate (not used in this build)

A logic-level MOSFET can drive **DC-only** solenoids without a relay. **24 VAC**
irrigation valves require a relay or triac driver — use the optocoupler relay
module above.

---

## Power Budget

| Component | Current |
|-----------|---------|
| UNO R4 WiFi (WiFi active) | ~250 mA |
| VH400 × 6 (max) | ~42 mA total (~7 mA each) |
| Relay module coil (from 5 V VCC) | ~70 mA |
| GPIO `D5` optocoupler input | < 5 mA |
| **Total (USB supply, 6 probes)** | **~360 mA** |

A 5 V / 1 A USB adapter has adequate headroom. The solenoid draws from the
separate valve supply; only the relay module coil loads the UNO 5 V rail.

---

## Enclosure & Installation Notes

- Mount the UNO and relay inside a weatherproof enclosure (IP65+) if
  installed outdoors or in a garage subject to moisture.
- Run sensor cables through a cable gland; avoid sharp bends near connectors.
- Keep 5 V sensor wiring and valve AC wiring in separate conduits or at least
  a few centimetres apart to minimise interference on the analog lines.
- Label sensor cables with their zone number before burial to simplify future
  maintenance.
