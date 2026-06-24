# Irrigation Data Extraction

Export historical cycle events and moisture time series from Home Assistant for offline analysis.

## Prerequisites

1. **Long-lived access token** — Profile → Security → Long-Lived Access Tokens
2. **Recorder retention** — default HA recorder keeps history; use ≥90 days for meaningful analysis
3. **Cycle events** — each completed irrigation cycle fires `irrigation_cycle_complete` (see [schemas/irrigation_cycle_log.md](../schemas/irrigation_cycle_log.md))

Optional: pin irrigation entities in `configuration.yaml` so they are never purged early:

```yaml
recorder:
  include:
    entities:
      - sensor.irrigation_sensor_0_vwc_resolved
      - sensor.irrigation_sensor_1_vwc_resolved
      - sensor.irrigation_valve_state_resolved
```

Do not commit tokens or database copies to git.

## Method 1 — SQLite (recommended)

The recorder stores full `event_data` in `home-assistant_v2.db`.

1. Copy the database from the HA host (Samba, SSH, or backup):
   - Typical path: `/config/home-assistant_v2.db`
2. Export cycles:

```bash
python scripts/export_irrigation_cycles.py \
  --db /path/to/home-assistant_v2.db \
  --days 90 \
  --out data/irrigation_cycles.csv
```

Query used internally:

```sql
SELECT e.time_fired, ed.shared_data
FROM events e
JOIN event_data ed ON e.data_id = ed.data_id
WHERE e.event_type = 'irrigation_cycle_complete'
  AND e.time_fired >= ?
ORDER BY e.time_fired;
```

## Method 2 — REST logbook API

Works without copying the database. Some HA versions omit full `event_data` in logbook responses — if rows are empty or sparse, use Method 1.

```bash
export HA_URL=http://10.0.4.169:8123
export HA_TOKEN=your_long_lived_token

python scripts/export_irrigation_cycles.py \
  --days 90 \
  --out data/irrigation_cycles.csv
```

API call:

```
GET /api/logbook/{start_iso}?end_time={end_iso}
Authorization: Bearer {token}
```

Filter entries where `name` or `context_event_type` is `irrigation_cycle_complete`.

## Method 3 — VWC time series (optional)

High-resolution moisture curves around each cycle (for lag analysis). Requires REST API:

```bash
python scripts/export_irrigation_cycles.py \
  --db /path/to/home-assistant_v2.db \
  --url "$HA_URL" --token "$HA_TOKEN" \
  --days 90 \
  --out data/irrigation_cycles.csv \
  --timeseries-out data/vwc_timeseries.csv
```

Uses `GET /api/history/period` for each cycle window (−30 min to +90 min from `cycle_started_at`).

Entities:

- `sensor.irrigation_sensor_0_vwc_resolved` (far from emitters)
- `sensor.irrigation_sensor_1_vwc_resolved` (near emitters)
- `sensor.irrigation_valve_state_resolved`

## Output files

| File | Contents |
|------|----------|
| `data/irrigation_cycles.csv` | One row per cycle + derived columns |
| `data/vwc_timeseries.csv` | Timestamped VWC/valve states per cycle |
| `data/analysis_report.json` | Produced by `scripts/analyze_irrigation.py` |

`data/*.csv` and `data/*.json` are gitignored. A sample fixture lives at `data/fixtures/sample_cycles.csv`.

## Next step — analyze

```bash
python scripts/analyze_irrigation.py \
  --cycles data/irrigation_cycles.csv \
  --out data/analysis_report.json

# Push summary to HA dashboard helpers (optional):
python scripts/analyze_irrigation.py \
  --cycles data/irrigation_cycles.csv \
  --apply-to-ha --url "$HA_URL" --token "$HA_TOKEN"
```

See [ai_tuning_guide.md](ai_tuning_guide.md) for the agent workflow.
