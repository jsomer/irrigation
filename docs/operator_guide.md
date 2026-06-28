# Irrigation Operator Guide

Practical guide for running and troubleshooting the system. For the full specification see [system_spec.md](system_spec.md).

---

## Dashboard layout

The Irrigation dashboard has two tabs:

### Irrigation tab

| Section | What you see | What to do with it |
|---------|--------------|-------------------|
| **Monitoring** | 48 h moisture graph, gauges, threshold sliders, last-cycle snapshots | Confirm sensors read sensibly; tune resume/target/max |
| **Control — Fault** | Red banner when something is wrong | Read message; follow [Alarm playbook](#alarm-playbook) |
| **Control — Countdown** | Timer + dry-event cycle count | See [Three numbers](#three-numbers-that-are-easy-to-confuse) |
| **Control — Status** | Mode, valve state, block reason, runtimes | Check before leaving in auto |
| **Manual Controls** | Start, force close, clear faults, test mode | Use for testing and recovery |
| **Irrigation Timing** | Duration, settle, max cycles | Core tuning |
| **Runtime Limits** | Hour, day, single run, failsafe | Safety budgets |
| **Leak Detection** | Min delta, check delay, min gallons | Leak alarm sensitivity |

### Data Analysis tab

Export/analyze workflow, suggested settings from last analysis run, 7-day moisture trend. Does not control irrigation directly.

---

## Three numbers that are easy to confuse

These appear near each other on the dashboard but mean different things.

### 1. Countdown (`sensor.irrigation_countdown`)

A **timer**, not a cycle counter.

| Phase | What it shows |
|-------|----------------|
| Irrigating | Minutes left in current pulse |
| Settling | Minutes left in settle phase |
| Auto + Ready | Minutes until next 5-minute auto check |
| Auto + blocked | `—` (see Auto Start Status) |
| Manual | `Manual` |

When it hits **0 min**, the current phase ended — it does **not** mean the dry event is over.

**Valve State vs water at the emitters:** **Valve State** is the firmware phase (`idle`, `pulsing`, `settling`). **Valve Open (Relay)** is whether the controller energizes the solenoid. During `settling` and `idle`, both should be off while soil moisture — especially S0 — can still rise for a long time in hot, dry weather. Slow moisture rise after the relay is off is normal; it is not the same as the valve still being open.

### 2. Dry event cycles (`counter.irrigation_cycles_this_event`)

How many times irrigation has **started** since moisture last dropped into the dry band.

- Only goes **up** (+1 each time a cycle starts, including manual starts).
- Does **not** go down when you lower **Max Cycles Per Dry Event** — you can end up **over limit** (e.g. counter **6**, limit **4**).
- Resets when **both** sensors reach target, when post-cycle evaluation reports `target_reached`, or when you tap **Reset Dry Event Counter**.
- Shown on the Countdown card as: `Dry event: N cycle(s) started · limit M`.

**Two different entities:**

| Entity | What it is |
|--------|------------|
| `counter.irrigation_cycles_this_event` | Count of cycles **already started** this dry event |
| `input_number.irrigation_max_cycles_per_event` | **Cap** on how many auto cycles may start before giving up |

Auto mode blocks when **counter ≥ limit**. If **counter > limit**, you lowered the cap (or ran manual tests) without resetting — tap **Reset Dry Event Counter** or raise the limit above the counter.

**Asymmetric layout (S0 far, S1 near):** S1 is often wet at/above target while S0 is still dry. Auto should still run when S0 needs water; the cycle counter increments only after the valve actually enters `pulsing`. Multiple cycles while S0 catches up is normal — the counter resets when **both** sensors reach target.

### 3. Runtime this hour (`sensor.irrigation_runtime_hour_min`)

**Cumulative valve-open time** in the controller’s rolling 60-minute window.

- **Hour cap off:** when **Max Runtime Per Hour** ≤ **Duration**, the rolling-hour limit is **disabled** (only per-pulse duration applies). Setting both to 60 min is the normal way to turn this off.
- **Hour cap on:** when hour max **>** duration (e.g. 120 min hour max, 60 min pulses), at most `hour_max ÷ duration` full pulses fit in each rolling hour.
- **Runtime This Hour** at 0 with an old **E002** fault: clear fault once; current firmware auto-clears when the hour window rolls.

---

## Normal operation checklist

1. **Control Mode** = `manual` while testing one cycle.
2. Tap **Start Irrigation Cycle**; watch valve go `pulsing` → `settling` → `idle`.
3. After settle + leak-check delay, check Logbook for `irrigation_cycle_complete`.
4. Set **Control Mode** = `auto` when satisfied.
5. **Auto Start Status** should eventually show `Ready`, then cycle starts on a 5-minute boundary.

Leave **Auto Block Override** off in production.

---

## Safe testing sequence

1. `manual` mode.
2. **Reset Dry Event Counter** if recovering from a test binge.
3. Set **Max Cycles Per Dry Event** = 1 for a single-cycle test.
4. Run **Start Irrigation Cycle** once; verify moisture response and Logbook event.
5. Enable `auto` only after leak settings look correct.
6. Use **Enable Auto Test Mode** only to verify auto-start timing — it **resets the dry-event counter** and skips post-cycle wait and moisture band (not max cycles or runtime budgets). **Disable** when done.

---

## Alarm playbook

### Leak fault (`input_boolean.irrigation_leak_fault` on)

**Meaning:** Water was delivered but moisture did not rise enough (broken line, valve stuck open downstream, wrong sensor placement, or check delay too short for slow soil).

**Actions:**
1. **Force Close** if valve might still be open.
2. Inspect plumbing and sensor readings.
3. Tap **Clear Leak Fault**.
4. Stay in `manual`; run one test cycle before re-enabling `auto`.
5. If false alarm on slow S0 soil, increase **Leak Check Delay** (see [ai_tuning_guide.md](ai_tuning_guide.md)).

### Sensor fault (`input_boolean.irrigation_sensor_fault` on)

**Meaning:** VWC &lt; 1 % — probe disconnected or failed.

**Actions:**
1. Check wiring and probe.
2. **Clear Sensor Fault** after readings recover.
3. Test in `manual` before `auto`.

### Valve fault (E001–E004)

**Meaning:** Firmware refused or aborted a pulse. Common codes:

| Code | Usual cause | Recovery |
|------|-------------|----------|
| E002 | Hourly runtime budget used up | **Clear Valve Fault**; check Runtime This Hour; wait or reboot controller |
| E003 | Daily runtime budget used up | Same for daily |
| E004 | Pulse denied (busy, unconfigured, budget) | Clear fault; verify Configured = on |
| E001 | Extreme overrun (stuck valve) | Clear fault; inspect valve |

Faults **latch** until **Clear Valve Fault** — waiting alone is not enough.

### Controller offline

**Meaning:** No MQTT from Arduino for 2+ minutes.

**Actions:** Check WiFi, Mosquitto, USB power. Firmware failsafe closes valve after **MQTT Failsafe Disconnect** minutes.

### Auto blocked but no fault

Read **Auto Start Status** (`sensor.irrigation_auto_start_block_reason`):

| Message | Meaning |
|---------|---------|
| `Ready` | Will start on next 5-min check if valve idle |
| `Moisture OK (S0 … · S1 …)` | Both sensors at or above resume — no irrigation needed |
| `Hourly runtime (X/Y min used…)` | Rolling-hour budget full — wait for window to roll or lower **Duration** |
| `Leak fault` | Clear leak fault after inspection before auto resumes |
| `Valve pulsing` / `settling` | Cycle in progress |

---

## When to use manual buttons

| Button | When |
|--------|------|
| **Start Irrigation Cycle** | Manual mode test run |
| **Force Close** | Emergency stop mid-pulse |
| **Clear Valve Fault** | After E00x fault resolved |
| **Clear Leak / Sensor Fault** | After physical issue fixed |
| **Reset Dry Event Counter** | After testing; counter stuck high or over limit |
| **Apply Default Settings** | Restore package defaults and **reset dry-event counter** (rare) |
| **Enable / Disable Auto Test Mode** | Short auto verification only |

---

## Key entities quick reference

| Question | Entity |
|----------|--------|
| Why isn’t auto starting? | `sensor.irrigation_auto_start_block_reason` |
| Is anything wrong? | `sensor.irrigation_fault_message` |
| Combined status | `sensor.irrigation_operational_status` |
| How wet is soil? | `sensor.irrigation_sensor_0_vwc_resolved`, `sensor.irrigation_sensor_1_vwc_resolved` |
| Valve doing what? | `sensor.irrigation_valve_state_resolved` |
| Cycles this dry spell? | `counter.irrigation_cycles_this_event` |

---

## Further reading

- [system_spec.md](system_spec.md) — purpose, alarms, setting tiers
- [ha_drip_control.md](ha_drip_control.md) — full automation list
- [data_extraction.md](data_extraction.md) — export data for analysis
- [feature_inventory.md](feature_inventory.md) — what every entity does
