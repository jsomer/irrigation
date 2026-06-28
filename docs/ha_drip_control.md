# Home Assistant Drip Control

Simple moisture-driven irrigation. Firmware runs the valve state machine; Home Assistant decides when to pulse.

See [system_spec.md](system_spec.md) for purpose and [theory_of_operation.md](theory_of_operation.md) for firmware detail.

---

## Division of responsibility

| Layer | Responsibility |
|-------|----------------|
| **Firmware** | Accept `pulse_duration_s`, open valve, close after duration, SETTLING, IDLE, emergency limits (max pulse, 24 h stuck-valve), MQTT failsafe |
| **Home Assistant** | Read sensors, block on stale/max/min moisture, compute pulse length, request pulse when valve is `idle` |

Home Assistant does **not** add a settle delay. Firmware `settling` phase covers post-pulse wait.

Minimum gap between auto pulses = firmware settle time only (valve must return to `idle` before HA requests the next pulse). There is no additional HA `last_changed` settle guard.

---

## Auto-start rule

When mode is `auto`, valve is `idle`, controller online, and at least one sensor slot is enabled:

| Condition | Action |
|-----------|--------|
| No sensors enabled | Do not water |
| Any **enabled** sensor unavailable or stale (> 90 s) | Do not water |
| Any **enabled** sensor ≥ `max_vwc` | Do not water |
| All **enabled** sensors ≥ `min_vwc` | Do not water |
| At least one **enabled** sensor < `min_vwc` | **Request pulse** |

Freshness uses MQTT sensor `last_updated` vs 90 s threshold. Disabled slots are ignored entirely.

---

## Enabling sensor slots

Firmware publishes all six analog pins (A0–A5). Home Assistant controls which slots participate in auto logic:

| Helper | Default | Purpose |
|--------|---------|---------|
| `input_boolean.irrigation_sensor_N_enabled` | on for N=0,1; off for 2–5 | Include slot in auto, freshness, pulse sizing |
| `input_text.irrigation_sensor_N_label` | `Sensor N` | Optional dashboard label |

**Add a probe:** wire VH400 to the next free pin (A0–A5), turn on `irrigation_sensor_N_enabled`, set min/target/max.

**Remove a probe:** turn off `irrigation_sensor_N_enabled` (no reflash required).

Deploy both `irrigation.yaml` and `irrigation_sensors.yaml` to `config/packages/`.

---

## Per-probe calibration

Firmware publishes **raw** VH400 VWC from the factory curve. Each probe can differ; HA applies per-sensor calibration before auto logic:

```
calibrated = clamp(raw × scale + offset, 0, 100)
```

| Helper | Default | Purpose |
|--------|---------|---------|
| `sensor.irrigation_sensor_N_vwc_raw` | — | Unadjusted firmware reading |
| `sensor.irrigation_sensor_N_vwc` | — | Calibrated value (used everywhere) |
| `input_number.irrigation_sensorN_vwc_scale` | 1.0 | Gain |
| `input_number.irrigation_sensorN_vwc_offset` | 0 | Baseline shift (%) |

**Side-by-side alignment:** bury both probes in the same moist soil, wait ~1 min, tap **Calibrate S0 to Match S1** (or the reverse). That sets offset so calibrated readings match at the current moisture. Fine-tune scale if response shape still differs.

Scripts: `irrigation_calibrate_sensor_to_reference` (parameterized), `irrigation_calibrate_sensor0_to_sensor1`, `irrigation_calibrate_sensor1_to_sensor0`, `irrigation_reset_sensor_calibration`, `irrigation_reset_sensorN_calibration` (aliases for 0/1).

---

## Variable pulse length

On each pulse request, HA computes moisture deficit toward target for each **enabled** sensor and uses the larger deficit:

```
deficit = max((target - vwc) / (target - min), 0)   capped at 1.0
pulse_min = min_pulse + (max_pulse - min_pulse) × deficit
```

Published as `{"action":"pulse","pulse_duration_s":…}`.

---

## Scripts

| Script | Purpose |
|--------|---------|
| `irrigation_sync_firmware` | MQTT `configure` — settle, max pulse, failsafe |
| `irrigation_request_pulse` | Sync + compute duration + MQTT `pulse` |
| `irrigation_force_close` | MQTT `close` |
| `irrigation_clear_fault` | MQTT `clear_fault` |

---

## Automations

| Automation | Purpose |
|------------|---------|
| Irrigation Sync On Connect / Start | Push configure after boot or MQTT reconnect |
| Irrigation Auto Request Pulse | When block reason = `Ready`, every 5 min |
| Irrigation Stop On Max VWC | Force close if any **enabled** sensor ≥ max while pulsing |
| Irrigation Valve Fault Alert | Notification on `fault` state |
| Irrigation Record Pulse Start Moisture | Store VWC for enabled slots when pulse begins |
| Irrigation Log Settle Snapshot | Record enabled slots at end of settle (`settling` → `idle`) |

---

## Settle snapshot (analysis)

When the valve completes settle and returns to `idle`, HA records moisture for all **enabled** slots:

| Output | Purpose |
|--------|---------|
| `input_number.irrigation_last_settle_sN_vwc` | Last post-settle reading per slot (recorder history) |
| `input_datetime.irrigation_last_settle_at` | Timestamp of last snapshot |
| Event `irrigation_settle_snapshot` | Full payload for export/analysis |
| Logbook entry | Human-readable summary with deltas |

Event fields: `sensors` (list of `{id, vwc, vwc_before, delta}` for enabled slots), `actual_pulse_s`, `requested_pulse_s`, `recorded_at`. Legacy per-slot helpers S0–S5 remain for dashboard/history.

Pulse-start moisture is captured when valve enters `pulsing` so deltas reflect the full pulse + settle cycle. Early force-close during pulse (no settle) does not emit a snapshot.

**Analysis:** collect **~1 week** of auto cycles, then export `irrigation_settle_snapshot` events and sensor history — see [data_extraction.md](data_extraction.md) and [schemas/irrigation_settle_snapshot.md](../schemas/irrigation_settle_snapshot.md).

---

## MQTT commands

```json
{"action":"configure","settle_duration_s":1200,"max_pulse_duration_s":3600,"failsafe_disconnect_s":1800}
{"action":"pulse","pulse_duration_s":1800}
{"action":"close"}
{"action":"clear_fault"}
```

Full schema: [schemas/mqtt_topics.md](../schemas/mqtt_topics.md).
