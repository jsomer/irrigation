# Home Assistant Drip Control

Simple moisture-driven irrigation. Firmware runs the valve state machine; Home Assistant decides when to pulse.

See [system_spec.md](system_spec.md) for purpose and [theory_of_operation.md](theory_of_operation.md) for firmware detail.

**Multi-controller:** every entity, helper, script, and MQTT topic is scoped by
`IRRIGATION_INSTANCE_ID`. Below, `<id>` means that instance (e.g. `raised_bed`).
Normative contract: [mqtt_topics.md](../schemas/mqtt_topics.md).
Packages are generated from `homeassistant/templates/` via
`scripts/render_ha_instances.py`.

---

## Division of responsibility

| Layer | Responsibility |
|-------|----------------|
| **Firmware** | Accept `pulse_duration_s`, open valve, close after duration, SETTLING, IDLE, emergency limits (max pulse, 24 h stuck-valve), MQTT failsafe |
| **Home Assistant** | Read sensors, block on stale/max/min moisture, compute pulse length, request pulse when valve is `idle` |

Home Assistant does **not** add a settle delay. Firmware `settling` phase covers post-pulse wait.

Minimum gap between auto pulses = firmware settle time only (valve must return to `idle` before HA requests the next pulse). There is no additional HA `last_changed` settle guard.

Each instance is independent: separate valve, sensors, helpers, automations, and history.

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

Freshness uses per-sensor MQTT “seen” heartbeats (90 s). Disabled slots are ignored entirely.

---

## Enabling sensor slots

Firmware publishes all six analog pins (A0–A5). Home Assistant controls which slots participate in auto logic:

| Helper | Purpose |
|--------|---------|
| `input_boolean.irrigation_<id>_sensor_N_enabled` | Include slot in auto, freshness, pulse sizing |
| `input_text.irrigation_<id>_sensor_N_label` | Optional dashboard label |

**Add a probe:** wire VH400 to the next free pin (A0–A5), turn on the enable helper, set min/target/max.

**Remove a probe:** turn off the enable helper (no reflash required).

Deploy generated `irrigation_<id>.yaml` and `irrigation_sensors_<id>.yaml` to
`config/packages/` (see [SETUP_AFTER_RESTORE.md](../homeassistant/SETUP_AFTER_RESTORE.md)).

---

## Per-probe calibration

Firmware publishes **raw** VH400 VWC from the factory curve. Each probe can differ; HA applies per-sensor calibration before auto logic:

```
calibrated = clamp(raw × scale + offset, 0, 100)
```

| Helper | Purpose |
|--------|---------|
| `sensor.irrigation_<id>_sensor_N_vwc_raw` | Unadjusted firmware reading |
| `sensor.irrigation_<id>_sensor_N_vwc` | Calibrated value (used everywhere) |
| `input_number.irrigation_<id>_sensorN_vwc_scale` | Gain |
| `input_number.irrigation_<id>_sensorN_vwc_offset` | Baseline shift (%) |

**Side-by-side alignment:** bury both probes in the same moist soil, wait ~1 min, tap **Calibrate S0 to Match S1** (or the reverse). That sets offset so calibrated readings match at the current moisture. Fine-tune scale if response shape still differs.

Scripts: `irrigation_<id>_calibrate_sensor_to_reference` (parameterized), `…_calibrate_sensor0_to_sensor1`, `…_calibrate_sensor1_to_sensor0`, `…_reset_sensor_calibration`, `…_reset_sensorN_calibration` (aliases for 0/1).

---

## Variable pulse length

On each pulse request, HA computes moisture deficit toward target for each **enabled** sensor and uses the larger deficit:

```
deficit = max((target - vwc) / (target - min), 0)   capped at 1.0
pulse_min = min_pulse + (max_pulse - min_pulse) × deficit
```

Published as `{"action":"pulse","pulse_duration_s":…}` on `irrigation/<id>/valve/command`.

---

## Scripts

| Script | Purpose |
|--------|---------|
| `irrigation_<id>_sync_firmware` | MQTT `configure` — settle, max pulse, failsafe |
| `irrigation_<id>_request_pulse` | Sync + compute duration + MQTT `pulse` |
| `irrigation_<id>_force_close` | MQTT `close` |
| `irrigation_<id>_clear_fault` | MQTT `clear_fault` |

---

## Automations

| Automation | Purpose |
|------------|---------|
| Sync On Connect / Start | Push configure after boot or MQTT reconnect |
| Auto Request Pulse | When block reason = `Ready`, every 5 min |
| Stop On Max VWC | Force close if any **enabled** sensor ≥ max while pulsing |
| Valve Fault Alert | Notification on `fault` state |
| Record Pulse Start Moisture | Store VWC for enabled slots when pulse begins |
| Log Settle Snapshot | Record enabled slots at end of settle (`settling` → `idle`) |

All automation `id:` values are instance-prefixed (`irrigation_<id>_…`).

---

## Settle snapshot (analysis)

When the valve completes settle and returns to `idle`, HA records moisture for all **enabled** slots:

| Output | Purpose |
|--------|---------|
| `input_number.irrigation_<id>_last_settle_sN_vwc` | Last post-settle reading per slot |
| `input_datetime.irrigation_<id>_last_settle_at` | Timestamp of last snapshot |
| Event `irrigation_<id>_settle_snapshot` | Full payload for export/analysis |
| Logbook entry | Human-readable summary with deltas |

Event fields: `sensors` (list of `{id, vwc, vwc_before, delta}` for enabled slots), `actual_pulse_s`, `requested_pulse_s`, `recorded_at`.

Pulse-start moisture is captured when valve enters `pulsing` so deltas reflect the full pulse + settle cycle. Early force-close during pulse (no settle) does not emit a snapshot.

**Analysis:** collect **~1 week** of auto cycles, then export settle-snapshot events and sensor history — see [data_extraction.md](data_extraction.md).

---

## MQTT commands

```json
{"action":"configure","settle_duration_s":1200,"max_pulse_duration_s":3600,"failsafe_disconnect_s":1800}
{"action":"pulse","pulse_duration_s":1800}
{"action":"close"}
{"action":"clear_fault"}
```

Topic: `irrigation/<id>/valve/command`. Full schema: [schemas/mqtt_topics.md](../schemas/mqtt_topics.md).
