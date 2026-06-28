# Irrigation Cycle Log Schema

> **Legacy.** Cycle logging, AI tuning, and `irrigation_cycle_complete` events were
> removed in the simplified architecture (2025). This schema documents the old
> format for historical CSV exports only. See [ha_drip_control.md](../docs/ha_drip_control.md)
> for the current control loop.

Events and entities written for AI-assisted tuning of drip irrigation parameters.

**System context:** [docs/system_spec.md](../docs/system_spec.md)

## Primary data source: `irrigation_cycle_complete` events

Fired by `script.irrigation_log_cycle_complete` after each post-settle evaluation.

### Event type

```
irrigation_cycle_complete
```

### `event_data` fields

#### Minimum fields for AI (required for tuning decisions)

| Field | Type | Description |
|-------|------|-------------|
| `cycle_id` | ISO string | Unique id (timestamp) |
| `ended_at` | ISO string | When evaluation completed |
| `cycle_started_at` | ISO string | When `irrigation_begin_cycle` ran |
| `duration_min_setting` | int | Slider at cycle time |
| `actual_pulse_s` | int | Measured valve-open seconds |
| `gallons_actual` | float | `actual_pulse_s / 60 × (system_gph / 60)` |
| `early_stop` | bool | Pulse ended before `duration_min_setting` |
| `outcome` | string | `normal` / `target_reached` / `leak_suspect` |
| `cycles_this_event` | int | Counter for current dry event |
| `resume_vwc_0` / `resume_vwc_1` | float | Low thresholds at cycle time |
| `target_vwc_0` / `target_vwc_1` | float | Target thresholds at cycle time |
| `s0_vwc_before` | float | Sensor 0 VWC before cycle |
| `s0_vwc_check` | float | Sensor 0 VWC at leak-check delay |
| `s0_delta_check` | float | `s0_vwc_check - s0_vwc_before` |
| `s1_vwc_before` | float | Sensor 1 VWC before cycle |
| `s1_vwc_check` | float | Sensor 1 VWC at leak-check delay |
| `s1_delta_check` | float | `s1_vwc_check - s1_vwc_before` |
| `s0_role` / `s1_role` | string | `far` / `near` |

#### Optional diagnostic fields

| Field | Type | Description |
|-------|------|-------------|
| `duration_min` | int | Configured irrigation run length (may match setting) |
| `gallons_estimated` | float | Estimate from configured duration |
| `runtime_today_s` | int | Valve-open seconds today |
| `settle_min_setting` | int | Slider at cycle time |
| `system_gph` | float | Slider at cycle time |
| `max_vwc_0` / `max_vwc_1` | float | Early-stop thresholds |
| `s0_vwc_end` | float | Sensor 0 VWC when valve closed |
| `s1_vwc_end` | float | Sensor 1 VWC when valve closed |
| `s0_vwc_60m` | float | Sensor 0 VWC 60 min after cycle |
| `s1_vwc_60m` | float | Sensor 1 VWC 60 min after cycle |
| `s1_*` (end, 60m) | float | Near-zone lag / overshoot analysis |

#### Full field list (legacy reference)

| Field | Type | Description |
|-------|------|-------------|
| `cycle_id` | ISO string | Unique id (timestamp) |
| `ended_at` | ISO string | When evaluation completed |
| `cycle_started_at` | ISO string | When `irrigation_begin_cycle` ran |
| `duration_min` | int | Configured irrigation run length |
| `duration_min_setting` | int | Slider at cycle time |
| `actual_pulse_s` | int | Measured valve-open seconds |
| `gallons_estimated` | float | `duration_min × (system_gph / 60)` |
| `gallons_actual` | float | `actual_pulse_s / 60 × (system_gph / 60)` |
| `early_stop` | bool | Pulse ended before `duration_min_setting` |
| `outcome` | string | See outcomes below |
| `cycles_this_event` | int | Counter for current dry event |
| `runtime_today_s` | int | Valve-open seconds today |
| `settle_min_setting` | int | Slider at cycle time |
| `system_gph` | float | Slider at cycle time |
| `resume_vwc_0` / `resume_vwc_1` | float | Low thresholds |
| `target_vwc_0` / `target_vwc_1` | float | Target thresholds |
| `max_vwc_0` / `max_vwc_1` | float | Early-stop thresholds |
| `s0_role` / `s1_role` | string | `far` / `near` (S0 far from emitters) |
| `s0_vwc_before` | float | Sensor 0 VWC before cycle |
| `s0_vwc_end` | float | Sensor 0 VWC when valve closed |
| `s0_vwc_check` | float | Sensor 0 VWC at leak-check delay |
| `s0_vwc_60m` | float | Sensor 0 VWC 60 min after cycle |
| `s0_delta_check` | float | `s0_vwc_check - s0_vwc_before` |
| `s1_*` | float | Same fields for sensor 1 |

### Outcomes

| Value | Meaning |
|-------|---------|
| `normal` | Cycle completed; moisture gain acceptable or below leak-check volume |
| `target_reached` | Both sensors at or above target VWC |
| `leak_suspect` | Water delivered without minimum VWC increase |

## Querying from Home Assistant

### Logbook

Settings → Logbook → filter `irrigation_cycle_complete`

### Export scripts (recommended)

```bash
python scripts/export_irrigation_cycles.py --db /path/to/home-assistant_v2.db --days 90
python scripts/analyze_irrigation.py --cycles data/irrigation_cycles.csv
```

See [docs/data_extraction.md](../docs/data_extraction.md).

### Recorder / API

```yaml
# Developer Tools → Statistics / History
# Entities to include in long-term analysis:
sensor.irrigation_sensor_0_vwc_resolved
sensor.irrigation_sensor_1_vwc_resolved
sensor.irrigation_valve_state_resolved
sensor.irrigation_runtime_today_resolved
input_number.irrigation_duration_min
input_number.irrigation_settle_min
```

### Example derived metrics (for AI agents)

```
vwc_per_gallon = s0_delta_check / gallons_actual
sensor_lag_minutes = time when S0 delta first exceeds 0.5% minus valve close
overshoot = s1_vwc_60m - target_vwc_1
gallons_needed_s0 = (target_vwc_0 - s0_vwc_before) / median(s0_vwc_per_gal_60m)
```

## Cycle snapshot helpers

Written during each cycle (visible on dashboard):

| Entity | When set |
|--------|----------|
| `input_text.irrigation_cycle_started_at` | Cycle start |
| `input_number.irrigation_cycle_actual_pulse_s` | Valve closes (pulsing → settling) |
| `input_number.irrigation_cycle_vwc_before_*` | Cycle start |
| `input_number.irrigation_cycle_vwc_end_*` | Valve closes |
| `input_number.irrigation_cycle_vwc_check_*` | After leak-check delay |
| `input_number.irrigation_cycle_vwc_60m_*` | 60 min after cycle |

## Analysis dashboard helpers

Written by `scripts/analyze_irrigation.py --apply-to-ha`:

| Entity | Purpose |
|--------|---------|
| `input_datetime.irrigation_analysis_last_export` | Last CSV export |
| `input_datetime.irrigation_analysis_last_run` | Last analysis run |
| `input_text.irrigation_analysis_summary` | Short summary for dashboard |
| `input_number.irrigation_analysis_rec_*` | Suggested slider values (not applied automatically) |

## Tunable entities (AI may recommend changes)

Package `initial:` values (see `homeassistant/packages/irrigation.yaml`):

| Entity | Default | Role |
|--------|---------|------|
| `input_number.irrigation_duration_min` | 60 | Irrigation cycle length |
| `input_number.irrigation_settle_min` | 20 | Wait before re-evaluation |
| `input_number.irrigation_sensor0_resume_vwc` | 35 | Start threshold |
| `input_number.irrigation_sensor0_target_vwc` | 44 | Stop threshold |
| `input_number.irrigation_min_vwc_delta` | 0.1 | Leak detection sensitivity |
| `input_number.irrigation_leak_check_delay_min` | 30 | Post-irrigation measurement delay |
| `input_number.irrigation_max_cycles_per_event` | 10 | Max cycles per dry spell |

## Safe bounds for automated recommendations

| Parameter | Min | Max |
|-----------|-----|-----|
| duration_min | 3 | 360 |
| settle_min | 10 | 45 |
| resume_vwc | 25 | 45 |
| target_vwc | resume + 5 | 55 |
| min_vwc_delta | 0.3 | 3.0 |
| leak_check_delay_min | 15 | 45 |
