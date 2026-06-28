# Irrigation Feature Inventory

Every Home Assistant entity, script, and automation in [`homeassistant/packages/irrigation.yaml`](../homeassistant/packages/irrigation.yaml), classified against the [system spec](system_spec.md).

**Classification key:**

| Class | Meaning | This phase |
|-------|---------|------------|
| **A — Core** | Required for moisture-window control | Document; keep |
| **B — Safety** | Required for unattended operation | Document; keep |
| **C — Tuning** | Supports export / AI analysis | Document; keep |
| **D — Ops/Test** | Deploy, testing, defaults | Document; consolidate in phase 2 |
| **E — Migration** | Legacy entity ID bridge | Document sunset path |
| **F — Review** | Unclear ROI or confusing UX | See [audit_report.md](audit_report.md) |

---

## Control mode

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `input_select.irrigation_control_mode` | A | Core | Dashboard | `manual` / `auto` / `disabled` / `firmware_fallback` |

---

## Operator sliders — timing & budgets

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `input_number.irrigation_duration_min` | A | Core | Dashboard | Pulse length; synced to firmware |
| `input_number.irrigation_settle_min` | A | Core | Dashboard | Post-pulse settle; synced to firmware |
| `input_number.irrigation_max_cycles_per_event` | A | Core | Dashboard | Cap cycles per dry event |
| `input_number.irrigation_max_runtime_hour_min` | B | Safety | Dashboard | Rolling-hour **cumulative** budget |
| `input_number.irrigation_max_runtime_day_min` | B | Safety | Dashboard | Rolling-day cumulative budget |
| `input_number.irrigation_max_single_run_min` | B | Safety | Dashboard | Per-pulse cap; must be ≥ duration |
| `input_number.irrigation_failsafe_disconnect_min` | B | Safety | Dashboard | MQTT-loss valve close |

---

## Operator sliders — moisture bands

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `input_number.irrigation_sensor0_resume_vwc` | A | Core | Dashboard | S0 dry threshold |
| `input_number.irrigation_sensor1_resume_vwc` | A | Core | Dashboard | S1 dry threshold |
| `input_number.irrigation_sensor0_target_vwc` | A | Core | Dashboard | S0 goal; dry-event reset |
| `input_number.irrigation_sensor1_target_vwc` | A | Core | Dashboard | S1 goal; dry-event reset |
| `input_number.irrigation_sensor0_max_vwc` | F | Core | Dashboard | Stored; **not** used for early stop (S0 uses target) |
| `input_number.irrigation_sensor1_max_vwc` | A | Core | Dashboard | S1 saturation early-stop |

---

## Operator sliders — leak detection

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `input_number.irrigation_min_vwc_delta` | B | Safety | Dashboard | Primary alarm sensitivity |
| `input_number.irrigation_leak_check_delay_min` | B | Safety | Dashboard | Delay after idle before check |
| `input_number.irrigation_min_gallons_for_leak_check` | B | Safety | Dashboard | Minimum volume to evaluate leak |

---

## Operator sliders — flow estimate

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `input_number.irrigation_system_gph` | B | Safety | Dashboard | Leak math gallons estimate |
| `input_number.irrigation_emitter_count` | C | Tuning | Dashboard | Informational; not in leak formula |

---

## Cycle snapshot helpers (internal)

Written by automations during each cycle; visible on dashboard for debugging.

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `input_text.irrigation_cycle_started_at` | C | Tuning | No | ISO timestamp in cycle events |
| `input_number.irrigation_cycle_vwc_before_0` | C | Tuning | Dashboard | S0 at pulse start |
| `input_number.irrigation_cycle_vwc_before_1` | C | Tuning | Dashboard | S1 at pulse start |
| `input_number.irrigation_cycle_vwc_end_0` | C | Tuning | Dashboard | S0 when valve closes |
| `input_number.irrigation_cycle_vwc_end_1` | C | Tuning | Dashboard | S1 when valve closes |
| `input_number.irrigation_cycle_vwc_check_0` | C | Tuning | Dashboard | S0 at leak-check time |
| `input_number.irrigation_cycle_vwc_check_1` | C | Tuning | Dashboard | S1 at leak-check time |
| `input_number.irrigation_cycle_vwc_60m_0` | C | Tuning | Dashboard | S0 +60 min after cycle |
| `input_number.irrigation_cycle_vwc_60m_1` | C | Tuning | Dashboard | S1 +60 min after cycle |
| `input_number.irrigation_cycle_actual_pulse_s` | C | Tuning | No | Actual valve-open seconds |

---

## Analysis staging helpers

Written by `scripts/analyze_irrigation.py --apply-to-ha`; suggestions only.

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `input_number.irrigation_analysis_rec_duration_min` | C | Tuning | Analysis tab | Suggested duration |
| `input_number.irrigation_analysis_rec_settle_min` | C | Tuning | Analysis tab | Suggested settle |
| `input_number.irrigation_analysis_rec_leak_check_delay_min` | C | Tuning | Analysis tab | Suggested leak delay |
| `input_number.irrigation_analysis_rec_min_vwc_delta` | C | Tuning | Analysis tab | Suggested min delta |
| `input_text.irrigation_analysis_summary` | C | Tuning | Analysis tab | Human-readable summary |
| `input_datetime.irrigation_analysis_last_export` | C | Tuning | Analysis tab | Last export timestamp |
| `input_datetime.irrigation_analysis_last_run` | C | Tuning | Analysis tab | Last analysis timestamp |

---

## Flags and counters

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `input_boolean.irrigation_leak_fault` | B | Safety | Dashboard | Latched leak alarm |
| `input_boolean.irrigation_sensor_fault` | B | Safety | Dashboard | Latched sensor alarm |
| `input_boolean.irrigation_cycle_eval_pending` | F | Core | No | Blocks auto during leak-check wait |
| `input_boolean.irrigation_auto_block_override` | D | Ops/Test | Dashboard | Skips eval wait + moisture band |
| `counter.irrigation_cycles_this_event` | A | Core | Dashboard | Cycles started this dry event |
| `input_text.irrigation_settings_revision` | D | Ops/Test | No | Triggers apply-defaults on HA start |
| `input_text.irrigation_last_fault_message` | C | Tuning | No | Reserved; fault text via template sensor |

---

## MQTT entity (package-defined)

| Entity | Class | North-star | Notes |
|--------|-------|------------|-------|
| `sensor.irrigation_valve_state` | A | Core | Raw MQTT valve state |

---

## Template sensors — migration bridge (E)

| Entity | Class | Notes |
|--------|-------|-------|
| `sensor.irrigation_valve_state_resolved` | E | Short + legacy ID; online-unknown → idle |
| `sensor.irrigation_sensor_0_vwc_resolved` | E | |
| `sensor.irrigation_sensor_1_vwc_resolved` | E | |
| `sensor.irrigation_runtime_today_resolved` | E | |
| `sensor.irrigation_runtime_hour_resolved` | E | |
| `sensor.irrigation_fault_reason_resolved` | E | |
| `sensor.irrigation_config_source_resolved` | E | `none` / `persisted` / `ha` |
| `binary_sensor.irrigation_controller_online_resolved` | E | |
| `binary_sensor.irrigation_firmware_configured_resolved` | E | |

---

## Template sensors — display / logic

| Entity | Class | North-star | Visible | Notes |
|--------|-------|------------|---------|-------|
| `sensor.irrigation_runtime_today_min` | B | Safety | Dashboard | Human-readable day budget used |
| `sensor.irrigation_runtime_hour_min` | B | Safety | Dashboard | Human-readable hour budget used |
| `sensor.irrigation_cycle_actual_pulse_min` | C | Tuning | No | Display helper |
| `sensor.irrigation_gallons_per_minute` | B | Safety | Dashboard | Derived from GPH |
| `sensor.irrigation_last_cycle_gallons` | C | Tuning | Dashboard | Last cycle volume estimate |
| `sensor.irrigation_operational_status` | A | Core | Dashboard | Combined HA status string |
| `sensor.irrigation_auto_start_block_reason` | A | Core | Dashboard | Why auto is not starting |
| `sensor.irrigation_valve_phase` | D | Ops/Test | Dashboard | Pulse/settle progress text |
| `sensor.irrigation_countdown` | D | Ops/Test | Dashboard | Timer until phase end or next auto poll |
| `sensor.irrigation_fault_message` | B | Safety | Dashboard | Human-readable fault banner |

---

## Template buttons

| Entity | Class | Notes |
|--------|-------|-------|
| `button.irrigation_start_cycle` | A | Calls begin_cycle script |
| `button.irrigation_force_close` | B | Abort script |
| `button.irrigation_clear_valve_fault` | B | MQTT clear_fault |

---

## Scripts

| Script | Class | North-star | Notes |
|--------|-------|------------|-------|
| `irrigation_sync_firmware` | A | Core | MQTT configure all limits |
| `irrigation_begin_cycle` | A | Core | Start pulse + increment counter |
| `irrigation_abort_and_close` | B | Safety | MQTT close |
| `irrigation_set_leak_fault` | B | Safety | Alarm + manual mode |
| `irrigation_set_sensor_fault` | B | Safety | Alarm + manual mode |
| `irrigation_post_cycle_evaluate` | B | Safety | Leak/target logic |
| `irrigation_log_cycle_complete` | C | Tuning | Fire `irrigation_cycle_complete` event |
| `irrigation_enable_auto_test_mode` | D | Ops/Test | Reset counter + override on + clear eval pending |
| `irrigation_disable_auto_test_mode` | D | Ops/Test | Override off |
| `irrigation_reset_dry_event` | D | Ops/Test | Manual counter reset |
| `irrigation_apply_package_defaults` | D | Ops/Test | Reset counter + sliders to package defaults |

---

## Automations

### Sync and config

| Automation | Class | Notes |
|------------|-------|-------|
| Irrigation Apply Firmware Config | A | Slider/mode change → sync |
| Irrigation Apply Sensor 0 Config | A | resume_vwc → MQTT |
| Irrigation Apply Sensor 1 Config | A | resume_vwc → MQTT |
| Irrigation Sync Firmware On Connect | A | HA start / online → full sync |
| Irrigation Apply Package Defaults On Revision | D | One-shot defaults on revision mismatch |

### Alerts

| Automation | Class | Notes |
|------------|-------|-------|
| Irrigation Valve Fault Alert | B | Notification on firmware fault |
| Irrigation Controller Offline Alert | B | 2 min offline notification |

### Auto control

| Automation | Class | Notes |
|------------|-------|-------|
| Irrigation Reset Dry Event Counter | A | Both sensors ≥ target → counter reset |
| Irrigation Snapshot VWC On Pulsing | C | Record before-VWC |
| Irrigation Auto Start Cycle | A | Every 5 min + state change when Ready |
| Irrigation Stop On Saturation | A | S1 ≥ max_vwc while pulsing |
| Irrigation Stop On Target VWC | A | S0 ≥ target_vwc while pulsing |
| Irrigation Sensor Invalid During Run | B | VWC &lt; 1 % while pulsing |
| Irrigation Mid Cycle Leak Check | B | Half-duration leak sample |
| Irrigation Block Start On Invalid Sensor | B | Invalid VWC anytime |

### Cycle logging

| Automation | Class | Notes |
|------------|-------|-------|
| Irrigation Record VWC At Cycle End | C | pulsing → settling |
| Irrigation Post Settle Evaluate | B | settling → idle → delay → evaluate |
| Irrigation Snapshot VWC At 60 Minutes | C | +60 min VWC for AI lag analysis |

### Safety backstop

| Automation | Class | Notes |
|------------|-------|-------|
| Irrigation Stuck Valve Overrun | B | Force close if pulse &gt; max_single_run + 2 min |

---

## Firmware capabilities (not HA entities)

| Feature | Class | Location | Notes |
|---------|-------|----------|-------|
| Valve state machine | A | `ValveController.cpp` | IDLE → PULSING → SETTLING |
| Hour/day runtime counters | B | `ValveController.cpp` | Rolling windows from boot |
| E001–E004 faults | B | `ValveController.cpp` | Latch until clear_fault |
| ConfigStore (NVS) | A | `ConfigStore.cpp` | Persist last HA configure |
| MQTT failsafe | B | `main.cpp` | Close on disconnect timeout |
| HA-loss autonomous trigger | F | `main.cpp` | Auto-trigger if MQTT down ≥ 120 s |
| On-device auto-trigger | F | `main.cpp` | `firmware_fallback` mode only |

---

## Dashboard (`homeassistant/dashboards/irrigation.yaml`)

| Section | Class | Notes |
|---------|-------|-------|
| Monitoring — graphs, gauges, thresholds | A/C | Operator visibility |
| Control — status, countdown, faults | A/B/D | Primary operator UI |
| Manual controls buttons | A/B/D | Includes test mode + reset |
| Timing / runtime / leak sliders | A/B | Matches package helpers |
| Data Analysis tab | C | Export workflow + rec helpers |

---

## Orphan artifacts (not active)

| Path | Class | Notes |
|------|-------|-------|
| `homeassistant/packages/fragments/` | — | Not `!include`'d; do not deploy |

---

## Summary counts

| Class | Approx. count | Role |
|-------|---------------|------|
| A — Core | ~35 | Moisture-window control |
| B — Safety | ~30 | Alarms, budgets, leak, faults |
| C — Tuning | ~25 | Logging, export, AI staging |
| D — Ops/Test | ~12 | Test mode, defaults, display |
| E — Migration | ~9 | Legacy entity bridge |
| F — Review | ~5 | See audit report |
