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

## Export settle snapshots

```bash
python scripts/export_irrigation_cycles.py \
  --db /path/to/home-assistant_v2.db \
  --days 14 \
  --out data/irrigation_settles.csv
```

Legacy `irrigation_cycle_complete` export (historical CSVs only):

```bash
python scripts/export_irrigation_cycles.py --legacy --db /path/to/home-assistant_v2.db
```

Optional moisture history around each settle (requires REST API):

```bash
export HA_URL=http://10.0.4.169:8123
export HA_TOKEN=your_long_lived_token

python scripts/export_irrigation_cycles.py \
  --db /path/to/home-assistant_v2.db \
  --days 14 \
  --out data/irrigation_settles.csv \
  --timeseries-out data/vwc_timeseries.csv \
  --url "$HA_URL" --token "$HA_TOKEN"
```

## Analyze

```bash
python scripts/analyze_irrigation.py \
  --input data/irrigation_settles.csv \
  --out data/analysis_report.json \
  --min-rows 3
```

The analyzer auto-detects settle vs legacy CSV format. Use `--min-rows 3` (default) — after ~1 week you should have enough pulses.

Test with the sample fixture:

```bash
python scripts/analyze_irrigation.py --input data/fixtures/sample_settles.csv
```
