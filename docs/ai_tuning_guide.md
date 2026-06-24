# AI Tuning Guide — Irrigation Controller

This guide is for AI agents (or humans) analyzing drip irrigation performance and recommending parameter changes.

For drip algorithm context (control modes, leak logic, automations), see [ha_drip_control.md](ha_drip_control.md).

## System context

- **Hardware:** Netafim Techline, 24 × 0.5 GPH emitters → **12 GPH** total
- **Control:** Home Assistant primary (`input_select.irrigation_control_mode: auto`)
- **Firmware:** Actuator + MQTT-configurable safety backstops; auto-trigger off by default
- **Sensors:** 2 × Vegetronix VH400 (VWC %)
- **Placement:** Sensor 0 is **far** from emitters (slow, drives target); Sensor 1 is **near** (fast, saturation risk)

## Data export and analysis pipeline

Home Assistant MCP exposes live state only — historical tuning uses exported files.

### Step 1 — Export cycles

```bash
export HA_URL=http://10.0.4.169:8123
export HA_TOKEN=your_long_lived_token

# Recommended: copy home-assistant_v2.db locally
python scripts/export_irrigation_cycles.py \
  --db /path/to/home-assistant_v2.db \
  --days 90 \
  --out data/irrigation_cycles.csv \
  --apply-to-ha --url "$HA_URL" --token "$HA_TOKEN"

# Optional: high-resolution VWC curves for lag analysis
python scripts/export_irrigation_cycles.py \
  --db /path/to/home-assistant_v2.db \
  --url "$HA_URL" --token "$HA_TOKEN" \
  --days 90 \
  --out data/irrigation_cycles.csv \
  --timeseries-out data/vwc_timeseries.csv
```

See [data_extraction.md](data_extraction.md) for API details and SQLite fallback.

### Step 2 — Analyze

```bash
python scripts/analyze_irrigation.py \
  --cycles data/irrigation_cycles.csv \
  --timeseries data/vwc_timeseries.csv \
  --out data/analysis_report.json \
  --apply-to-ha --url "$HA_URL" --token "$HA_TOKEN"
```

Review `data/analysis_report.json` and the **Data Analysis** dashboard tab (`irrigation-analysis`).

### Step 3 — Apply manually

Copy suggested values from `input_number.irrigation_analysis_rec_*` to the live sliders on the Irrigation tab. Test one cycle in **manual** mode before enabling **auto**.

## Data to read

### 1. Cycle completion events (best source)

**Event type:** `irrigation_cycle_complete`

Each event includes moisture before/end/check/+60m, actual vs estimated gallons, settings snapshot, sensor roles, and `outcome`.

See [schemas/irrigation_cycle_log.md](../schemas/irrigation_cycle_log.md) for the full schema.

### 2. Live entities

| Entity | Purpose |
|--------|---------|
| `sensor.irrigation_sensor_0_vwc_resolved` | Far-zone moisture |
| `sensor.irrigation_sensor_1_vwc_resolved` | Near-zone moisture |
| `sensor.irrigation_operational_status` | idle / irrigating / settling / leak_fault |
| `counter.irrigation_cycles_this_event` | Cycles in current dry event |
| `input_boolean.irrigation_leak_fault` | Leak suspected — auto disabled |

### 3. Snapshot helpers (last cycle)

`input_number.irrigation_cycle_vwc_*` — before, end, check, 60m for each sensor.

## Analysis workflow

1. **Export** ≥10 cycles via `export_irrigation_cycles.py`.
2. **Run** `analyze_irrigation.py` for metrics, dry-event grouping, and leak-parameter simulation.
3. **Compute** per cycle (S0 focus):
   - `s0_delta_check` / `gallons_actual` → far-zone VWC gain per gallon
   - `s0_vwc_60m` vs `target_vwc_0` → target attainment and lag
   - `s1_vwc_60m` vs `target_vwc_1` → near-zone overshoot
4. **Identify patterns:**
   - Low S0 `delta_check` with `normal` outcome → increase `leak_check_delay_min`
   - `leak_suspect` with S0 rising by 60m → false positive from slow infiltration
   - `target_reached` never in 4 cycles → increase `duration_min` or `max_cycles_per_event`
   - S1 at `max_vwc` while S0 dry → decrease `duration_min`, increase `settle_min`
5. **Verify** recommendations against `leak_simulation` counts in the report.
6. **Recommend** slider changes within safe bounds (see schema doc).
7. **Never** recommend disabling leak detection or raising daily limits above 240 min without explicit user approval.

## Example agent prompt

```
1. Export irrigation data:
   python scripts/export_irrigation_cycles.py --db <path> --days 90 --out data/irrigation_cycles.csv

2. Analyze:
   python scripts/analyze_irrigation.py --cycles data/irrigation_cycles.csv --out data/analysis_report.json

3. Read analysis_report.json. Sensor 0 is far from emitters (slow); Sensor 1 is near (saturation risk).

4. Recommend irrigation_duration_min, irrigation_settle_min,
   irrigation_leak_check_delay_min, and irrigation_min_vwc_delta.
   Stay within safe bounds in schemas/irrigation_cycle_log.md.

5. Confirm leak_simulation.false_positives_at_current vs at_recommended.
   Explain reasoning with numbers from the cycles. Do not disable leak detection.

6. Optional: --apply-to-ha to update dashboard summary entities.
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
gallons_actual >= min_gallons_for_leak_check
AND max(delta_sensor_0, delta_sensor_1) < min_vwc_delta
```

(at post-settle check, and at mid-cycle with 25% of `min_vwc_delta`)

Post-cycle evaluation uses **actual** pulse gallons when `irrigation_cycle_actual_pulse_s` is recorded.

If false positives occur on slow far-zone (S0) soils:

- Increase `leak_check_delay_min` (e.g. 20 → 30)
- Decrease `min_vwc_delta` slightly (e.g. 1.0 → 0.7) only if simulation supports it

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
