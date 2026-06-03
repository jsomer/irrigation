# Hardware Reference

## Bill of Materials

| Qty | Component | Details | Notes |
|-----|-----------|---------|-------|
| 1 | Arduino UNO R4 WiFi | Renesas RA4M1, built-in WiFi (ESP32-S3 co-processor) | Main controller |
| 2 | Vegetronix VH400 | Capacitive soil moisture probe, 0–3 V analog output | One per measurement zone |
| 1 | Solenoid valve | 24 VAC or 12 VDC irrigation valve | Any standard 3/4" irrigation solenoid |
| 1 | Relay module or MOSFET driver | 5 V logic-level trigger; rated for valve voltage/current | See valve driver section below |
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
| `D5` | Relay/MOSFET input | HIGH = valve open |
| `5V` | VH400 Sensor 0 & 1 power (red) | Both sensors share 5 V |
| `GND` | VH400 Sensor 0 & 1 ground (black); relay GND | Common ground |
| `LED_BUILTIN` | On-board LED | Blinks every telemetry interval (alive indicator) |

> **UNO R4 WiFi analog note:** The RA4M1 ADC reference is **3.3 V** regardless
> of the USB/5 V supply rail. The VH400 is powered from 5 V but its signal
> output is 0–3 V, which falls safely within the 0–3.3 V ADC input range — no
> voltage divider is needed. `config.h` sets `Sensor::VCC = 3.3f` and
> `Sensor::ADC_MAX = 1023` (10-bit default resolution) for the calibration math.

---

## VH400 Sensor Wiring

Each VH400 has three wires:

| Wire colour | Connect to |
|-------------|-----------|
| Red | 5 V |
| Black | GND |
| Bare/White | Analog input pin (A0 or A1) |

Bury the full sensing shaft vertically in the soil. The probe needs good
contact along its entire length for accurate readings. Keep signal wires away
from the solenoid valve wiring to avoid interference.

---

## Valve Driver Circuit

The UNO R4 WiFi's digital outputs are **3.3 V logic** with a maximum source/sink
current of about **8 mA per GPIO pin** (RA4M1 datasheet). You must **not** wire
the solenoid coil or a bare relay coil directly to `D5` — that will overload the
pin and can damage the microcontroller.

Always use a relay module with an onboard optocoupler, or a MOSFET driver with a
separate valve power supply, as described below.

### Option A — 5 V Relay Module (simplest)

```
UNO D5 ──► IN  [5V Relay Module]  COM ──► Valve terminal 1
UNO 5V ──► VCC                    NO  ──► Valve power supply +
UNO GND──► GND                    
                                   Valve terminal 2 ──► Valve power supply –/GND
```

Use a relay module with an onboard optocoupler (most blue relay modules).
The coil is driven from the module's VCC, and the UNO logic signal only
drives the optocoupler — no flyback diode needed on the UNO side.

### Option B — MOSFET Driver (DC valves only)

```
UNO D5 ──[1 kΩ]──► Gate  [N-channel MOSFET, e.g. IRL540]
UNO GND ──────────► Source
                    Drain ──► Valve – terminal
                    [1N4007 flyback across valve terminals]
                    Valve power supply + ──► Valve + terminal
                    Valve power supply – ──► UNO GND
```

Use a logic-level MOSFET (gate threshold ≤ 3.3 V). Do **not** use option B
with 24 VAC valves — use a relay or triac driver instead.

---

## Power Budget

| Component | Current |
|-----------|---------|
| UNO R4 WiFi (WiFi active) | ~250 mA |
| VH400 × 2 | ~30 mA total |
| Relay module coil | ~70 mA |
| **Total (USB supply)** | **~350 mA** |

A 5 V / 1 A USB adapter has adequate headroom. The solenoid valve draws
current from its own supply and does not load the UNO's USB rail.

---

## Enclosure & Installation Notes

- Mount the UNO and relay inside a weatherproof enclosure (IP65+) if
  installed outdoors or in a garage subject to moisture.
- Run sensor cables through a cable gland; avoid sharp bends near connectors.
- Keep 5 V sensor wiring and valve AC wiring in separate conduits or at least
  a few centimetres apart to minimise interference on the analog lines.
- Label sensor cables with their zone number before burial to simplify future
  maintenance.
