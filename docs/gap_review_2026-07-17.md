# Irrigation Gap Review — 2026-07-17

Working document produced before the next fix pass. Captures confirmed code bugs, doc/code conflicts, and a recommended fix order.

**Source of truth for current behavior:** the Home Assistant packages + firmware.

| Trust | Do not trust (until rewritten) |
|-------|--------------------------------|
| `homeassistant/packages/irrigation.yaml` | `docs/system_spec.md` |
| `homeassistant/packages/irrigation_sensors.yaml` | `docs/operator_guide.md` |
| `homeassistant/dashboards/irrigation.yaml` | `docs/feature_inventory.md` |
| `docs/ha_drip_control.md` | `docs/ai_tuning_guide.md` |
| `docs/theory_of_operation.md` (mostly) | `docs/audit_report.md` (2026-06-24) |
| `docs/data_extraction.md` | Large parts of `README.md` |

---

## Current architecture (what the code actually does)

Simple moisture loop with up to **six** optional VH400 sensors:

1. HA reads calibrated VWC for enabled sensors.
2. If any enabled sensor is below its **min** VWC, and none are at **max**, HA requests a pulse sized between **min pulse** and **max pulse**.
3. Firmware runs the pulse, then settle.
4. HA logs a **settle snapshot** when the valve returns `settling` → `idle`.

Control modes: `disabled` / `manual` / `auto` only.  
Firmware safety: **E001** (stuck-valve hardware cap) + MQTT failsafe close.  
No on-device auto-trigger, no hour/day runtime budgets, no leak-fault latch in the current package.

Deploy requires **two** package files plus the Lovelace dashboard (not into `packages/`).

---

## Confirmed code bugs (MUST-FIX)

### 1. Zone Delta template is broken

**File:** `homeassistant/packages/irrigation.yaml` (~lines 242–264)

An `{% if %}` is closed with `{% endfor %}` instead of `{% endif %}`.

**Effect:** `sensor.irrigation_zone_delta` fails; Overview “Matrix Summary” shows no data.

### 2. HA start always forces control mode to `auto`

**File:** `homeassistant/packages/irrigation.yaml` — automation `Irrigation Sync On Start`

Every Home Assistant restart runs `input_select.select_option` → `auto`, then syncs firmware.

**Effect:** Overrides the operator’s last mode (`manual` / `disabled`). Conflicts with the persistence design (no `initial:` so settings restore) and with setup guidance that says leave mode manual until a test cycle succeeds. Can irrigate after reboot without an intentional mode choice.

### 3. Freshness binary sensors never expire on the clock

**File:** `homeassistant/packages/irrigation_sensors.yaml` — `irrigation_sensor_N_fresh`

Templates use `(now() - last_updated) < 90` but have **no time trigger**, so they only re-evaluate when another entity changes.

**Effect:** After the last VWC update, “fresh” can stay `on` indefinitely. Auto-block logic that trusts freshness may treat stale moisture as Ready and pulse on old data.

### 4. Max-VWC force-close skips settle snapshot

**HA:** `Irrigation Stop On Max VWC` → `script.irrigation_force_close` → MQTT `close`  
**Firmware:** `forceClose()` goes to `IDLE` (skips `SETTLING`)  
**Snapshot trigger:** only `settling` → `idle`

**Effect:** Early stops never fire `irrigation_settle_snapshot`. Advanced “Last Settle Values” and settle export miss those cycles.

### 5. Fresh install can sync zero timings to firmware

Helpers intentionally have no `initial:` (so they persist). On a brand-new install, unset `input_number`s are `unknown` → `| int` → `0` in the MQTT configure payload. Combined with Sync On Start forcing `auto`, a new deploy can push `settle_duration_s` / `max_pulse_duration_s` / `failsafe_disconnect_s` = 0 before the operator sets recommended defaults.

---

## Conflicting requirements (docs vs code)

Two “north stars” are documented at once. The 2026-06 audit docs describe the **old** leak / dry-event / budget system; the packages implement the **simplified** loop.

| Topic | Stale L0/L1 docs claim | Current code |
|-------|------------------------|--------------|
| Sensors | 2 fixed roles (S0 far / S1 near), `resume_vwc` | 6 slots, `min_vwc` / `target_vwc` / `max_vwc`, enable toggles |
| Dry event | Cycle counter + `max_cycles_per_event` | Removed |
| Leak alarm | Core unattended alarm; Clear Leak Fault | Removed from package |
| Control modes | Includes `firmware_fallback` | `disabled` / `manual` / `auto` only |
| Pulse length | Fixed `irrigation_duration_min` | Variable pulse between min and max |
| Runtime budgets | Hour/day → E002 / E003 | Firmware **E001 only** |
| Default mode | Manual until verified | Forced **auto** on every HA start (bug) |
| Cycle logging | `irrigation_cycle_complete` + `analysis_rec_*` | `irrigation_settle_snapshot` + settle CSV |
| Deploy | Often “one package file” | **Two** packages + Lovelace dashboard |
| Entity IDs | `*_resolved`, `operational_status`, `valve_phase`, leak flags | Bare package entity IDs |

### Firmware / MQTT specifics

- Configure payload (settle / max pulse / failsafe) matches firmware `applyHaConfigure`.
- README still documents per-sensor MQTT `resume_vwc` config and configure fields firmware ignores (`max_runtime_day_s`, etc.).
- MQTT Discovery in firmware publishes only a subset of entities; docs claim a fuller discovery catalog. Core valve/sensor entities come from the **HA package**, not discovery.
- Package MQTT templates ignore some firmware telemetry fields (`pulse_count`, `fault_reason`, `configured`, `config_source`).

---

## Documentation status

### Current / usable

| Document | Notes |
|----------|-------|
| `docs/ha_drip_control.md` | Matches auto rule, calibration, settle snapshot, scripts |
| `docs/theory_of_operation.md` | 6 pins, no auto-trigger, E001; discovery table slightly overstates |
| `docs/data_extraction.md` | Settle-primary export pipeline |
| `schemas/mqtt_topics.md` | Wire format + E001 mostly good |
| `homeassistant/SETUP_AFTER_RESTORE.md` | Updated for dual packages (after prior fix) |

### Stale / misleading

| Document | Problem |
|----------|---------|
| `docs/system_spec.md` | Entire L0 model obsolete (2 sensors, leak, dry event, E002–E004) |
| `docs/operator_guide.md` | Dashboard/playbook for removed UI (countdown, leak, test mode, analysis_rec) |
| `docs/feature_inventory.md` | Inventory of deleted entities/scripts |
| `docs/ai_tuning_guide.md` | Still on `irrigation_cycle_complete` / leak simulation |
| `docs/audit_report.md` | 2026-06 backlog targets ghosts |
| `README.md` | Mixed: E002/E003, `firmware_fallback`, 2-sensor pins, single-package include |
| `.cursor/skills/irrigation-docs/SKILL.md` | Checklist still requires `*_resolved`, `duration_min`, `firmware_fallback` |

### Under-documented new features

Present in code / `ha_drip_control.md` but missing from L0/L1:

1. Six sensor slots + enable/label  
2. Per-sensor calibration (scale/offset, calibrate-to-reference)  
3. Variable pulse (deficit → min↔max)  
4. Settle snapshot event + helpers  
5. No-`initial:` persistence model  
6. `sensor.irrigation_zone_delta` (matrix summary)  
7. Cycle time-remaining helpers/automation  
8. Dual-package deploy as the normal path  
9. HA start → force `auto` (should be documented or removed)

---

## Deploy traps (operational)

| Trap | Symptom |
|------|---------|
| Dashboard YAML pasted into `config/packages/irrigation.yaml` | `expected dict for dictionary value @ data['title']` — package fails to load |
| Only one of the two package files deployed | Sensor slots / bands / settle helpers missing |
| `configuration.yaml` missing second `!include` | `irrigation_sensors` package never loads |
| Samba note saying “irrigation.yaml only” | `scripts/deploy-ha.sh` copies both; success text was misleading |

---

## Recommended fix order

### Phase A — restore functions (code)

1. Fix Zone Delta `{% endif %}`  
2. Stop Sync-On-Start from forcing `auto` (sync firmware only; leave mode alone)  
3. Add a time trigger to freshness binary sensors  
4. After max-VWC force-close, still record a settle snapshot (or transition through settle)  
5. Guard configure sync until helpers have valid values (or document a one-shot apply-defaults for fresh installs)

### Phase B — single north-star (docs)

6. Rewrite `docs/system_spec.md` for the simple 6-sensor loop  
7. Rewrite `docs/operator_guide.md` to the current dashboard + E001/offline/stale/at-max playbook  
8. Rebuild `docs/feature_inventory.md` from a package grep  
9. Align `README.md`, `docs/ai_tuning_guide.md`, irrigation-docs skill; demote or mark historical `schemas/irrigation_cycle_log.md` and `docs/audit_report.md`  
10. Fold zone delta, dual-package deploy, and persistence rules into L0–L1

### Explicitly out of scope for the next pass unless requested

- Restoring the old leak-detection / dry-event / hour-day budget stack  
- Changing dual-sensor asymmetric roles (replaced by 6 optional slots)  
- Automatic application of AI recommendations  

---

## Verification checklist after Phase A

| Check | Expected |
|-------|----------|
| Overview Matrix Summary | Shows enabled sensors + pairwise deltas |
| Control mode after HA restart | Same as before restart (`manual` stays `manual`) |
| Disable MQTT / stale VWC for 2+ min | Fresh sensors flip off; auto block reason not Ready |
| Stop on max VWC | Last Settle helpers update (or settle event fires) |
| Fresh install configure | Non-zero settle / max_pulse / failsafe until operator sets values |
| Developer Tools → States | `sensor.irrigation_valve_state`, `binary_sensor.irrigation_controller_online`, enabled S0–S5 VWC |

---

## How to use this document

1. Agree Phase A vs Phase B order (or both).  
2. Fix items without expanding scope back into the old architecture unless that is an explicit product decision.  
3. When Phase B lands, either retire this file or append a short “resolved” section with dates.
