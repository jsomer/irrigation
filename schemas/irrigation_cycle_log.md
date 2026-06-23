# Irrigation Cycle Log Schema

Events and entities written for AI-assisted tuning of drip irrigation parameters.

## Primary data source: `irrigation_cycle_complete` events

Fired by `script.irrigation_log_cycle_complete` after each post-settle evaluation.

### Event type

```
irrigation_cycle_complete
```

### `event_data` fields

| Field | Type | Description |
|-------|------|-------------|
| `cycle_id` | ISO string | Unique id (timestamp) |
| `ended_at` | ISO string | When evaluation completed |
| `duration_min` | int | Irrigation run length |
| `gallons_estimated` | float | `duration_min × (system_gph / 60)` |
| `outcome` | string | See outcomes below |
| `cycles_this_event` | int | Counter for current dry event |
| `runtime_today_s` | int | Valve-open seconds today |
| `duration_min_setting` | int | Slider at cycle time |
| `settle_min_setting` | int | Slider at cycle time |
| `system_gph` | float | Slider at cycle time |
| `resume_vwc_0` / `resume_vwc_1` | float | Low thresholds |
| `target_vwc_0` / `target_vwc_1` | float | Target thresholds |
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
vwc_per_gallon = s0_delta_check / gallons_estimated
sensor_lag_minutes = time when delta first exceeds 0.5% minus cycle end time
overshoot = max(s0_vwc_60m, s1_vwc_60m) - target_vwc
```

## Cycle snapshot helpers

Written during each cycle (visible on dashboard):

| Entity | When set |
|--------|----------|
| `input_number.irrigation_cycle_vwc_before_*` | Cycle start |
| `input_number.irrigation_cycle_vwc_end_*` | Valve closes |
| `input_number.irrigation_cycle_vwc_check_*` | After leak-check delay |
| `input_number.irrigation_cycle_vwc_60m_*` | 60 min after cycle |

## Tunable entities (AI may recommend changes)

| Entity | Default | Role |
|--------|---------|------|
| `input_number.irrigation_duration_min` | 10 | Irrigation cycle length |
| `input_number.irrigation_settle_min` | 20 | Wait before re-evaluation |
| `input_number.irrigation_sensor0_resume_vwc` | 35 | Start threshold |
| `input_number.irrigation_sensor0_target_vwc` | 44 | Stop threshold |
| `input_number.irrigation_min_vwc_delta` | 1.0 | Leak detection sensitivity |
| `input_number.irrigation_leak_check_delay_min` | 20 | Post-irrigation measurement delay |
| `input_number.irrigation_max_cycles_per_event` | 4 | Max cycles per dry spell |

## Safe bounds for automated recommendations

| Parameter | Min | Max |
|-----------|-----|-----|
| duration_min | 3 | 30 |
| settle_min | 10 | 45 |
| resume_vwc | 25 | 45 |
| target_vwc | resume + 5 | 55 |
| min_vwc_delta | 0.3 | 3.0 |
| leak_check_delay_min | 15 | 45 |
