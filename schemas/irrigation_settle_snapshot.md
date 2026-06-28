# Irrigation Settle Snapshot Schema

Events fired when the valve completes settle and returns to `idle` (`settling` → `idle`).

See [ha_drip_control.md](../docs/ha_drip_control.md) and [data_extraction.md](../docs/data_extraction.md).

## Event type

```
irrigation_settle_snapshot
```

## `event_data` fields

| Field | Type | Description |
|-------|------|-------------|
| `recorded_at` | ISO string | When snapshot was taken |
| `sensors` | list | Enabled slots only; each item below |
| `actual_pulse_s` | int | Firmware-reported pulse duration |
| `requested_pulse_s` | int | HA-requested pulse duration |

Each entry in `sensors`:

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Sensor slot 0–5 |
| `vwc` | float | Calibrated VWC at settle end |
| `vwc_before` | float | Calibrated VWC at pulse start |
| `delta` | float | `vwc - vwc_before` |

## Recorder helpers (history)

| Entity | Description |
|--------|-------------|
| `input_number.irrigation_last_settle_sN_vwc` | Post-settle VWC per slot |
| `input_number.irrigation_pulse_start_sN_vwc` | Pulse-start VWC per slot |
| `input_datetime.irrigation_last_settle_at` | Last snapshot time |
| `sensor.irrigation_sensor_N_vwc` | Continuous moisture (calibrated) |
| `sensor.irrigation_actual_pulse_s` | Last completed pulse length |

## Analysis note

Collect at least **~1 week** of auto cycles before tuning thresholds or pulse sizing.
Export via recorder SQLite or history API once enough `irrigation_settle_snapshot` events exist.

Legacy `irrigation_cycle_complete` schema: [irrigation_cycle_log.md](irrigation_cycle_log.md).
