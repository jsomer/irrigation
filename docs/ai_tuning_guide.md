# AI Tuning Guide — Irrigation Controller

This guide is for AI agents (or humans) analyzing drip irrigation performance and recommending parameter changes.

## System context

- **Hardware:** Netafim Techline, 24 × 0.5 GPH emitters → **12 GPH** total
- **Control:** Home Assistant primary (`input_select.irrigation_control_mode: auto`)
- **Firmware:** Actuator + MQTT-configurable safety backstops; auto-trigger off by default
- **Sensors:** 2 × Vegetronix VH400 (VWC %)

## Data to read

### 1. Cycle completion events (best source)

**Event type:** `irrigation_cycle_complete`

Each event includes moisture before/end/check/+60m, gallons estimated, settings snapshot, and `outcome`.

Query via Home Assistant Logbook, Recorder history, or MCP/API.

See [schemas/irrigation_cycle_log.md](../schemas/irrigation_cycle_log.md) for the full schema.

### 2. Live entities

| Entity | Purpose |
|--------|---------|
| `sensor.irrigation_sensor_0_vwc_resolved` | Current moisture zone 0 |
| `sensor.irrigation_sensor_1_vwc_resolved` | Current moisture zone 1 |
| `sensor.irrigation_operational_status` | idle / irrigating / settling / leak_fault |
| `counter.irrigation_cycles_this_event` | Cycles in current dry event |
| `input_boolean.irrigation_leak_fault` | Leak suspected — auto disabled |

### 3. Snapshot helpers (last cycle)

`input_number.irrigation_cycle_vwc_*` — before, end, check, 60m for each sensor.

## Analysis workflow

1. **Collect** last 10–20 `irrigation_cycle_complete` events.
2. **Compute** per cycle:
   - `delta_check` vs `gallons_estimated` → VWC gain per gallon
   - Time from cycle end until `delta_check` meaningful → sensor lag
   - `s*_vwc_60m` vs `target_vwc_*` → overshoot / migration
3. **Identify patterns:**
   - Low `delta_check` with normal outcome → increase `leak_check_delay_min` or lower `min_vwc_delta`
   - Frequent `leak_suspect` → check physical system before lowering `min_vwc_delta`
   - `target_reached` never occurs in 4 cycles → increase `duration_min` or `max_cycles_per_event`
   - Overshoot at 60m → lower `target_vwc` or `duration_min`
4. **Recommend** slider changes within safe bounds (see schema doc).
5. **Never** recommend disabling leak detection or raising daily limits above 240 min without explicit user approval.

## Example agent prompt

```
Analyze Home Assistant irrigation_cycle_complete events from the last 7 days.
Compute average VWC delta per gallon and sensor response lag.
Recommend changes to irrigation_duration_min, irrigation_settle_min,
irrigation_sensor0_target_vwc, and irrigation_min_vwc_delta.
Stay within safe bounds in schemas/irrigation_cycle_log.md.
Explain reasoning with numbers from the events.
```

## Applying recommendations

All settings are `input_number` / `input_select` entities in the irrigation package. Changes push to firmware automatically via `script.irrigation_sync_firmware`.

After changing settings:

1. Keep mode `manual` for one test cycle.
2. Verify new event in Logbook.
3. Enable `auto` if results look correct.

## Leak detection tuning

Leak logic fires when:

```
gallons_estimated >= min_gallons_for_leak_check
AND max(delta_sensor_0, delta_sensor_1) < min_vwc_delta
```

(at post-settle check, and at mid-cycle with 25% of `min_vwc_delta`)

If false positives occur on slow clay soils:

- Increase `leak_check_delay_min` (e.g. 20 → 30)
- Decrease `min_vwc_delta` slightly (e.g. 1.0 → 0.7)

If false negatives (dry soil after many cycles):

- Increase `min_vwc_delta`
- Decrease `min_gallons_for_leak_check`

## Control modes

| Mode | Behavior |
|------|----------|
| `disabled` | No auto cycles; manual buttons only |
| `manual` | No auto cycles; use for testing |
| `auto` | HA runs drip state machine |
| `firmware_fallback` | On-device `resume_vwc` trigger only (legacy) |

Default after deploy: **`manual`**.
