# VH400 Soil Moisture Calibration

The firmware driver (`firmware/src/sensors/VH400.cpp`) converts the probe's analog
voltage to volumetric water content (VWC %) using Vegetronix's piecewise-linear
formula for **mineral soils**.

## Signal chain

1. **VH400 output:** 0–3 V (probe is powered at 5 V; signal stays within 3 V).
2. **ADC:** 10-bit (0–1023) on the UNO R4 WiFi, default reference **5 V** (`analogRead` full scale).
3. **Voltage:** `V = (adc_count / 1023) × 5.0`
4. **VWC:** piecewise function below, then clamped to 0–100 %.

`Sensor::VCC` in `config.h` must match the ADC reference (5.0 V on UNO R4 WiFi default).

## Piecewise calibration (V → VWC %)

Source: Vegetronix VH400 application note (mineral soil).

| Voltage range (V) | Formula (VWC %) |
|-------------------|-----------------|
| V < 1.1 | `10 × V − 1` |
| 1.1 ≤ V < 1.3 | `25 × V − 17.5` |
| 1.3 ≤ V < 1.82 | `48.08 × V − 47.5` |
| 1.82 ≤ V < 2.2 | `26.32 × V − 7.89` |
| V ≥ 2.2 | `62.5 × V − 87.5` |

Implementation matches `VH400::voltageToVWC()` in firmware.

## Averaging

Each reading averages **16** ADC samples with **200 µs** between samples
(`Sensor::AVERAGING_SAMPLES` in `config.h`).

## Adapting for other soils or sensors

- **Different soil type:** Vegetronix publishes alternate curves for organic
  media; replace the breakpoints and slopes in `voltageToVWC()`.
- **Field tuning:** Compare probe readings to gravimetric soil tests, then
  adjust thresholds in Home Assistant (`input_number` dry/green/high limits),
  not necessarily the VH400 curve.
- **Different probe:** Do not reuse this table; use the manufacturer's
  voltage-to-moisture chart for that sensor.

## Invalid readings

Firmware treats VWC **< 1 %** as invalid (disconnected or floating analog pin)
and excludes it from the on-device auto-trigger. Home Assistant zone logic uses
`unknown` when the MQTT sensor is unavailable.

## Per-probe calibration (Home Assistant)

The VH400 curve is a generic mineral-soil approximation. Individual probes often
need a baseline shift or scale factor. Home Assistant stores these per sensor:

- **Raw:** `sensor.irrigation_sensor_N_vwc_raw` (firmware MQTT value)
- **Calibrated:** `sensor.irrigation_sensor_N_vwc` = `raw × scale + offset`

Use **Align S0 → S1** on the dashboard when both probes are in the same known
moisture to match readings, then deploy probes to their final locations and
adjust min/target thresholds if needed.

### Parity verification

Firmware uses the **same** VH400 driver, ADC reference (5 V default), sample count (16),
and piecewise curve for both probes — only the pin (A0 vs A1) differs.

1. Reset calibration (Scale=1, Offset=0 on both).
2. Compare **Raw VWC** and **Signal V** on the dashboard Calibration cards.
3. **Swap test:** swap **Black (signal) wires** at A0/A1 only; Bare stays on GND.
   If the low reading follows the probe, the probe or cable is at fault; if it
   stays on the same slot, check that analog pin's wire.
4. MQTT publishes `voltage` alongside `vwc` for side-by-side diagnosis in HA.
