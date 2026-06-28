---
name: irrigation-docs
description: Audit and sync irrigation project documentation against firmware and Home Assistant code. Use when the user asks to update, audit, or sync docs, or when code changes may have outpaced documentation.
---

# Irrigation Documentation Maintenance

Keep docs aligned with code. **Source of truth:** firmware + HA YAML → schemas → docs.

## Audit order

1. **Firmware** — `firmware/src/config.h`, `main.cpp`, `irrigation/ValveController.cpp`, `mqtt/MqttManager.cpp`
2. **Home Assistant** — `homeassistant/packages/irrigation.yaml` (scripts, automations, template entities)
3. **Schemas** — `schemas/mqtt_topics.md`, `schemas/irrigation_cycle_log.md`
4. **Docs** — `docs/system_spec.md` first, then `docs/*.md`, `README.md`, `homeassistant/SETUP_AFTER_RESTORE.md`

## Doc ownership

| Topic | Primary doc | Do not duplicate |
|-------|-------------|------------------|
| North-star, purpose, alarms | `docs/system_spec.md` | README, ha_drip_control |
| Operator dashboard, troubleshooting | `docs/operator_guide.md` | system_spec |
| Entity classification | `docs/feature_inventory.md` | system_spec |
| Audit gaps, phase-2 backlog | `docs/audit_report.md` | — |
| Quick start, deploy | `README.md` | SETUP_AFTER_RESTORE |
| Firmware loop, valve SM | `docs/theory_of_operation.md` | mqtt_topics (wire format only) |
| HA drip algorithm | `docs/ha_drip_control.md` | theory_of_operation |
| MQTT wire format | `schemas/mqtt_topics.md` | — |
| Cycle events for AI | `schemas/irrigation_cycle_log.md` | ai_tuning_guide |
| AI tuning workflow | `docs/ai_tuning_guide.md` | ha_drip_control |
| HA data export | `docs/data_extraction.md` | ai_tuning_guide |
| HA restore/deploy | `homeassistant/SETUP_AFTER_RESTORE.md` | README (brief link) |
| Data export / analysis scripts | `docs/data_extraction.md`, `scripts/export_irrigation_cycles.py`, `scripts/analyze_irrigation.py` | ai_tuning_guide |
| Hardware, VWC math | `docs/hardware.md`, `docs/vh400_calibration.md` | Only if hardware/sensor code changed |

## Layered reading order

1. L0 — `docs/system_spec.md`
2. L1 — `docs/operator_guide.md`
3. L2 — `docs/ha_drip_control.md`, `docs/theory_of_operation.md`
4. L3 — schemas, `ai_tuning_guide.md`, `data_extraction.md`

## Verification checklist

Cross-check these against code on every audit:

- [ ] **North-star** — system_spec matches package behavior (moisture window, leak alarm, tuning pipeline)
- [ ] **Control authority** — HA `auto` mode runs drip; firmware auto-trigger off by default; `firmware_fallback` emergency-only
- [ ] **Safety defaults** — HA slider `initial:` values in package vs docs tables
- [ ] **Error codes** — E001 = hardware 86 400 s stuck-valve cap; E002/E003 = MQTT hourly/daily budgets; E004 = pulse denied
- [ ] **Failsafe** — default `failsafe_disconnect_min` = 30 min
- [ ] **resume_vwc** — default 35 %, not 25 %
- [ ] **Helper names** — `irrigation_duration_min` (not `irrigation_pulse_duration`)
- [ ] **Automation/script aliases** — grep `alias:` in `irrigation.yaml`; update `ha_drip_control.md` and `feature_inventory.md`
- [ ] **Entity IDs** — package automations use `*_resolved` templates
- [ ] **feature_inventory.md** — new entities get a class (A–F)

## Stale-term grep

After edits, search docs for contradictions:

```
DefaultParams
firmware auto-trigger enabled by default
120 s failsafe
E001.*7200
resume_vwc: 25
irrigation_pulse_duration
Stop On Max VWC
duration.*initial.*10
max_cycles.*4
```

## WIP artifacts (do not document as active)

- `homeassistant/packages/fragments/` — untracked copies, not `!include`'d by main package

## Output format

1. List gaps in `docs/audit_report.md` (file, stale claim, correct value from code)
2. Apply edits — one primary home per topic; link elsewhere
3. Update `feature_inventory.md` when adding/removing entities
4. Update README project tree when adding/removing doc files

## Do not touch unless related code changed

- `docs/hardware.md`
- `docs/vh400_calibration.md`
