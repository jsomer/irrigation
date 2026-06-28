# Irrigation Data Extraction

Export historical settle snapshots and moisture time series from Home Assistant for offline analysis.

**When to start:** after **~1 week** of normal auto irrigation so you have enough pulse/settle cycles to compare moisture deltas, pulse lengths, and threshold behavior.

## Prerequisites

1. **Long-lived access token** — Profile → Security → Long-Lived Access Tokens
2. **Recorder retention** — default HA recorder keeps history; 90+ days recommended
3. **Events** — each completed pulse+settle fires `irrigation_settle_snapshot` (see [schemas/irrigation_settle_snapshot.md](../schemas/irrigation_settle_snapshot.md))

Optional: pin irrigation entities so they are never purged early:

```yaml
recorder:
  include:
    entities:
      - sensor.irrigation_sensor_0_vwc
      - sensor.irrigation_sensor_1_vwc
      - sensor.irrigation_valve_state
      - input_number.irrigation_last_settle_s0_vwc
      - input_number.irrigation_last_settle_s1_vwc
```

Do not commit tokens or database copies to git.

## Primary data source: `irrigation_settle_snapshot`

Fired by `script.irrigation_log_settle_snapshot` when valve state goes `settling` → `idle`.

Example `event_data`:

```json
{
  "recorded_at": "2026-06-25T14:30:00",
  "sensors": [
    {"id": 0, "vwc": 42.1, "vwc_before": 35.2, "delta": 6.9},
    {"id": 1, "vwc": 44.0, "vwc_before": 36.8, "delta": 7.2}
  ],
  "actual_pulse_s": 1800,
  "requested_pulse_s": 1800
}
```

### SQLite query (recorder)

```sql
SELECT e.time_fired, ed.shared_data
FROM events e
JOIN event_data ed ON e.data_id = ed.data_id
WHERE e.event_type = 'irrigation_settle_snapshot'
  AND e.time_fired >= ?
ORDER BY e.time_fired;
```

Database path: `/config/home-assistant_v2.db` (copy via Samba or SSH).

## Moisture time series

Use **Developer Tools → History** or REST:

```
GET /api/history/period/{start}?filter_entity_id=sensor.irrigation_sensor_0_vwc,sensor.irrigation_sensor_1_vwc,sensor.irrigation_valve_state
```

Enable only the sensor slots you use (`input_boolean.irrigation_sensor_N_enabled`).

## Legacy export script

`scripts/export_irrigation_cycles.py` and `scripts/analyze_irrigation.py` target the **old** `irrigation_cycle_complete` event format. They remain for historical CSVs only.

After ~1 week of operation, plan to either:

- Export `irrigation_settle_snapshot` rows manually from SQLite, or
- Extend the export script for the new event schema (future work).

## Output files (convention)

| File | Contents |
|------|----------|
| `data/irrigation_settles.csv` | One row per settle snapshot (when exported) |
| `data/vwc_timeseries.csv` | Timestamped VWC + valve state |
| `data/analysis_report.json` | Analysis output (future / legacy script) |

`data/*.csv` and `data/*.json` are gitignored.

## Next step — after ~1 week

1. Confirm auto mode has run multiple full pulse → settle → idle cycles.
2. Export settle events and sensor history from recorder.
3. Review per-cycle `delta` vs `requested_pulse_s` and adjust min/target/max thresholds in HA if needed.

See [ha_drip_control.md](ha_drip_control.md) for control-loop behavior.
