# Irrigation Documentation Audit Report

**Date:** 2026-06-24  
**Scope:** Documentation-only phase per [system spec review plan](system_spec.md). Code unchanged.

---

## Deliverables completed

| Document | Status |
|----------|--------|
| [system_spec.md](system_spec.md) | Created — L0 north-star |
| [feature_inventory.md](feature_inventory.md) | Created — full entity/script/automation classification |
| [operator_guide.md](operator_guide.md) | Created — L1 operator recovery guide |
| README, ha_drip_control, theory_of_operation, ai_tuning_guide, irrigation_cycle_log, irrigation-docs skill | Updated — layered under system_spec |
| This report | Created — gaps + phase-2 backlog |

---

## Doc vs code gaps found

| Location | Stale or missing claim | Correct value (from code) |
|----------|------------------------|---------------------------|
| `README.md` Safety Limits table | Single run 20 min, hour 30 min, day 120 min | Package defaults: duration 60, hour 60, day 720, max_single_run 360 |
| `README.md` Safety Limits | "Emergency cap 120 min" per row | Firmware stuck-valve cap is 86 400 s (24 h) per `ValveController.cpp`; E001 at hardware limit |
| `ha_drip_control.md` | Automation "Stop On Max VWC" | Split into **Stop On Saturation** (S1) and **Stop On Target VWC** (S0) |
| `ha_drip_control.md` | Script table missing test/reset/defaults scripts | 12 scripts total — see feature inventory |
| `irrigation_cycle_log.md` Tunable defaults | duration 10, leak delay 20, max_cycles 4 | Package `initial:` values: duration 60, leak delay 30, max_cycles 10 |
| `irrigation-docs` skill | E001 = 7200 s | E001 = hardware 86 400 s stuck-valve cap in current `ValveController.cpp` |
| `irrigation-docs` skill | References `DefaultParams` in config.h | Operational defaults removed; HA-authoritative + ConfigStore |
| `theory_of_operation.md` | Duplicates HA drip purpose | Trimmed; links to system_spec + ha_drip_control |

---

## Class F items — recommendations

Items flagged **F — Review** in [feature_inventory.md](feature_inventory.md).

| Item | Recommendation | Rationale |
|------|----------------|-----------|
| `irrigation_sensor0_max_vwc` | **Simplify** | Not used for early stop; remove from dashboard or document as unused; avoid operator confusion |
| `irrigation_cycle_eval_pending` + leak_check_delay | **Keep** | Separates firmware settle from slow-soil leak measurement; merge only if delay always equals settle |
| `Irrigation Snapshot VWC At 60 Minutes` | **Simplify** | Make delay proportional to `duration_min` or optional flag; fixed 60 min weak for 3 min test cycles |
| `irrigation_analysis_rec_*` staging | **Keep** | Clear separation between suggestions and live sliders; could merge into analysis report JSON only in phase 2 |
| `firmware_fallback` + HA-loss autonomous trigger | **Keep, document as emergency** | Valid unattended fallback; hide from main operator UI |
| Dual runtime enforcement (HA + firmware E002/E003) | **Keep** | Defense in depth; improve messaging (done in fault_message template) |
| `irrigation_auto_block_override` | **Keep, restrict** | Dev-only; already excludes max cycles and runtime budgets |
| `irrigation_settings_revision` + apply defaults on start | **Simplify** | One-shot migration aid; remove after all deployments at revision 2 |
| `homeassistant/packages/fragments/` | **Remove** | Orphan; not included by package |
| E002 fault latch after hour window resets | **Simplify (firmware)** | Auto-clear HOURLY_LIMIT when `_runtimeHourS` resets would improve UX |
| `input_text.irrigation_last_fault_message` | **Remove or wire** | Appears unused; fault text comes from template sensor |

---

## Phase-2 backlog (code / UX — not in this phase)

Prioritized simplification work after operator review of this audit:

### P1 — Operator clarity (low risk)

1. Remove or hide `irrigation_sensor0_max_vwc` from dashboard; note in spec as legacy/unused.
2. Add runtime budget line to Countdown card: `Hour: X / Y min used`.
3. Update `ha_drip_control.md` automation table whenever aliases change (automate via grep in CI or skill checklist).

### P2 — Consolidation (medium risk)

4. Replace fixed 60 m snapshot with `max(duration_min, leak_check_delay_min)` or configurable `lag_snapshot_min`.
5. Delete `homeassistant/packages/fragments/` or add README warning.
6. Remove `irrigation_apply_package_defaults_on_revision` after all HA instances at revision `2`.

### P3 — Behavioral changes (needs testing)

7. Firmware: auto-clear E002/E003 when rolling window resets and runtime below limit.
8. Collapse `analysis_rec_*` into dashboard markdown from `analysis_report.json` only (fewer entities).
9. Evaluate merging `cycle_eval_pending` into block reason without separate boolean.

### P4 — Out of scope unless requirements change

10. Symmetric two-sensor logic (user confirmed asymmetric layout).
11. Weather integration, multi-valve support.
12. Automatic application of AI recommendations without manual confirm.

---

## Verification checklist (irrigation-docs skill)

| Check | Status |
|-------|--------|
| Control authority — HA `auto`, firmware auto off by default | OK |
| Safety defaults documented vs package `initial:` | Fixed in README |
| Error codes E001–E004 | Fixed E001 description |
| Failsafe default 30 min | OK |
| resume_vwc default 35 % | OK |
| Helper names `irrigation_duration_min` | OK |
| Automation/script count vs docs | Fixed in ha_drip_control |
| `*_resolved` entities documented | OK in feature inventory |

---

## Stale-term grep (post-edit)

Run after any future doc change:

```
DefaultParams
120 s failsafe
E001.*7200
irrigation_pulse_duration
Stop On Max VWC
duration.*initial.*10
max_cycles.*4
```

---

## How to use this audit

1. Read [system_spec.md](system_spec.md) for the minimum mental model.
2. Use [operator_guide.md](operator_guide.md) when something looks wrong on the dashboard.
3. Use [feature_inventory.md](feature_inventory.md) to answer "what does this entity do?"
4. Prioritize phase-2 items with the user before any code deletion.
