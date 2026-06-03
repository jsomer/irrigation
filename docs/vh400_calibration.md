# VH400 Soil Moisture Calibration

The firmware driver (`firmware/src/sensors/VH400.cpp`) converts the probe's analog
voltage to volumetric water content (VWC %) using Vegetronix's piecewise-linear
formula for **mineral soils**.

## Signal chain

1. **VH400 output:** 0–3 V (probe is powered at 5 V; signal stays within 3 V).
2. **ADC:** 10-bit (0–1023) on the UNO R4 WiFi, reference **3.3 V** (not 5 V).
3. **Voltage:** `V = (adc_count / 1023) × 3.3`
4. **VWC:** piecewise function below, then clamped to 0–100 %.

`Sensor::VCC` in `config.h` must match the ADC reference (3.3 V on UNO R4 WiFi).

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
