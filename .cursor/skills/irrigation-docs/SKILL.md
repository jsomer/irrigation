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
4. **Docs** — `README.md`, `docs/*.md`, `homeassistant/SETUP_AFTER_RESTORE.md`

## Doc ownership

| Topic | Primary doc | Do not duplicate |
|-------|-------------|------------------|
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

## Verification checklist

Cross-check these against code on every audit:

- [ ] **Control authority** — HA `auto` mode runs drip; firmware `AUTO_TRIGGER_ENABLED = false` by default; only `firmware_fallback` enables on-device auto-trigger
- [ ] **Safety defaults** — HA sliders vs `DefaultParams` in `config.h` vs emergency caps in `Safety::`
- [ ] **Error codes** — E001 = emergency 7200 s pulse cap; E002/E003 = MQTT hourly/daily budgets; E004 = pulse denied
- [ ] **Failsafe** — default `failsafe_disconnect_s` = 1800 s (30 min), not 120 s
- [ ] **resume_vwc** — default 35 %, not 25 %
- [ ] **Helper names** — `irrigation_duration_min` (not `irrigation_pulse_duration`)
- [ ] **Automation/script aliases** — grep `alias:` in `irrigation.yaml`; update `ha_drip_control.md` if count changes
- [ ] **Entity IDs** — package automations use `*_resolved` templates; dashboards may still use legacy `irrigation_controller_irrigation_*`

## Stale-term grep

After edits, search docs for contradictions:

```
firmware auto-trigger
120 s
600 s
resume_vwc: 25
irrigation_pulse_duration
```

## WIP artifacts (do not document as active)

- `homeassistant/packages/fragments/` — untracked copies, not `!include`'d by main package

## Output format

1. List gaps found (file, stale claim, correct value from code)
2. Apply edits — one primary home per topic; link elsewhere
3. Add mermaid diagrams for state machines when documenting HA drip logic
4. Update README project tree when adding/removing doc files

## Do not touch unless related code changed

- `docs/hardware.md`
- `docs/vh400_calibration.md`
