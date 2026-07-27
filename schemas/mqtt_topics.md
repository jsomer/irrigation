# MQTT Topic Schema

**Hardware:** Arduino UNO R4 WiFi  
**Broker:** Mosquitto on Home Assistant

---

## Multi-controller namespace

Every physical controller has an `IRRIGATION_INSTANCE_ID` configured in its
local `firmware/src/secrets.h`. The ID is the stable machine identifier for that
installation.

- Allowed form: lowercase ASCII letters, digits, `_`, and `-`
- Maximum length: 32 characters
- Must be unique among controllers connected to the same MQTT broker
- Must not be changed after Home Assistant has been provisioned unless the
  controller is intentionally being migrated as a new instance

Example IDs: `raised_bed`, `vegetable_garden`.

All runtime topics use this root:

```text
irrigation/<instance_id>
```

The MQTT client ID is `irrigation-<instance_id>`. The broker credentials may be
shared, but the instance ID and resulting client ID may not.

In the remainder of this specification, `<root>` means
`irrigation/<instance_id>`.

---

## Architecture

Home Assistant reads moisture telemetry and sends pulse commands. Firmware executes pulse → settle → idle.

```
Sensor 0..5 (A0–A5) ── telemetry ──► Home Assistant
                                      │
                                      │ pulse / configure / close
                                      ▼
                                 Valve (Pin 5)
```

---

## Valve telemetry

**Topic:** `<root>/valve/telemetry`
**Interval:** ~10 s

| Field | Type | Description |
|-------|------|-------------|
| `valve_open` | bool | Relay energized |
| `state` | string | `idle` / `pulsing` / `settling` / `fault` |
| `actual_pulse_s` | uint32 | Seconds of last completed pulse |
| `pulse_elapsed_s` | uint32 | Seconds elapsed in current pulse (0 if not pulsing) |
| `pulse_count` | uint32 | Pulses since boot |
| `error_code` | string | `none`, `E001` |
| `fault_reason` | string | Present when `state=fault` |
| `configured` | bool | Params loaded from HA or flash |
| `config_source` | string | `none` / `persisted` / `ha` |
| `ts` | uint32 | Uptime seconds |

**Error codes:**

| Code | Meaning |
|------|---------|
| `E001` | Hardware stuck-valve limit (86 400 s) |

---

## Valve command

**Topic:** `<root>/valve/command`

| Action | Fields | Effect |
|--------|--------|--------|
| `pulse` | `pulse_duration_s` (required) | Start pulse for requested seconds (clamped to max pulse / hardware cap) |
| `close` | — | Force valve closed → idle |
| `clear_fault` | — | Clear fault → idle |
| `configure` | `settle_duration_s`, `max_pulse_duration_s`, `failsafe_disconnect_s` | Persist operational limits |

```json
{"action":"pulse","pulse_duration_s":1800}
{"action":"configure","settle_duration_s":1200,"max_pulse_duration_s":3600,"failsafe_disconnect_s":1800}
```

**Hardware-only limits (not MQTT-configurable):**

| Limit | Value |
|-------|-------|
| Stuck-valve absolute max open | 86 400 s |
| Min settle gap | 60 s |

---

## Sensor telemetry

**Topic:** `<root>/sensor/<sensor_id>/telemetry`

```json
{"sensor":0,"vwc":18.3,"voltage":1.42,"ts":1024}
{"sensor":4,"vwc":22.1,"voltage":1.58,"ts":2048}
```

| Field | Type | Description |
|-------|------|-------------|
| `sensor` | uint8 | Sensor index (0–5) |
| `vwc` | float | Raw VWC % from firmware VH400 curve |
| `voltage` | float | Averaged signal voltage at ADC pin (V) |
| `ts` | uint32 | Uptime seconds |

---

## Valve status (LWT)

**Topic:** `<root>/valve/status` — retained `online` / `offline`

This is also the MQTT Last Will topic. Home Assistant must use the status topic
from the same instance as the availability source for all entities belonging to
that controller.

---

## Home Assistant MQTT Discovery contract

Firmware publishes retained discovery messages using:

```text
homeassistant/<domain>/irrigation_<instance_id>/<local_entity_id>/config
```

Each discovery payload uses:

| Field | Required value |
|-------|----------------|
| `device.identifiers[0]` | `irrigation_<instance_id>` |
| `device.name` | `IRRIGATION_INSTANCE_NAME` from that controller |
| `object_id` | `irrigation_<instance_id>_<local_entity_id>` |
| `unique_id` | `irrigation_<instance_id>_<local_entity_id>` |
| `availability_topic` | `<root>/valve/status` |

The display name shown in HA is `device.name` + local entity name (e.g. device
`Raised Bed` + entity `Sensor 0 VWC`). Discovery sets:

| Field | Value |
|-------|--------|
| `unique_id` / `object_id` | `irrigation_<instance_id>_<local_entity_id>` |
| `default_entity_id` | `<domain>.irrigation_<instance_id>_<local_entity_id>` |
| `name` | local label only (no instance prefix — avoids doubled entity IDs) |

Example for instance `raised_bed` named `Raised Bed`:

```text
MQTT client:       irrigation-raised_bed
Runtime root:      irrigation/raised_bed
HA device ID:      irrigation_raised_bed
Discovery topic:   homeassistant/sensor/irrigation_raised_bed/sensor_0_vwc/config
Entity unique ID:  irrigation_raised_bed_sensor_0_vwc
default_entity_id: sensor.irrigation_raised_bed_sensor_0_vwc
Display:           Raised Bed → Sensor 0 VWC
```

---

## Home Assistant entities

Firmware discovery entity IDs are instance-scoped. Home Assistant package
entities, helpers, scripts, automations, and dashboard references must use the
same instance prefix; no unscoped entity may be shared by two controllers.

| Local entity ID | Discovery domain | HA unique/object ID |
|-----------------|------------------|---------------------|
| `sensor_N_vwc` | `sensor` | `irrigation_<instance_id>_sensor_N_vwc` |
| `controller_online` | `binary_sensor` | `irrigation_<instance_id>_controller_online` |
| `valve` | `binary_sensor` | `irrigation_<instance_id>_valve` |
| `valve_state` | `sensor` | `irrigation_<instance_id>_valve_state` |
| `valve_pulse_count` | `sensor` | `irrigation_<instance_id>_valve_pulse_count` |
| `pulse_elapsed_s` | `sensor` | `irrigation_<instance_id>_pulse_elapsed_s` |
| `error_code` | `sensor` | `irrigation_<instance_id>_error_code` |
| `fault_reason` | `sensor` | `irrigation_<instance_id>_fault_reason` |
| `firmware_configured` | `binary_sensor` | `irrigation_<instance_id>_firmware_configured` |
| `config_source` | `sensor` | `irrigation_<instance_id>_config_source` |
| `valve_close` | `button` | `irrigation_<instance_id>_valve_close` |
| `clear_fault` | `button` | `irrigation_<instance_id>_clear_fault` |

Pulse is requested by HA scripts (`irrigation_request_pulse`), not a discovery button.

### Requirements for the HA software update

For each configured controller, HA must:

1. Subscribe to that controller's `<root>/sensor/+/telemetry`,
   `<root>/valve/telemetry`, and `<root>/valve/status` topics.
2. Publish commands and configuration only to that controller's
   `<root>/valve/command`.
3. Give all HA-created `unique_id`, helper, script, automation, timer, and
   template entity identifiers an instance-specific prefix.
4. Resolve automation and dashboard entity references from the selected
   instance rather than from the old unscoped `irrigation_*` entities.
5. Keep configuration, freshness timers, cycle state, counters, alarms, and
   history independent per instance.
6. Reject duplicate instance IDs during HA configuration.

### Migration from the legacy single-controller contract

The old unscoped topics (`irrigation/valve/...` and
`irrigation/sensor/...`) are no longer published or consumed by this firmware.
Before flashing:

1. Choose the existing controller's stable instance ID (recommended:
   `raised_bed`) and update HA to the scoped topics.
2. Stop automations that publish to legacy command topics.
3. Flash the firmware and verify `<root>/valve/status` becomes `online`.
4. Confirm HA sends `configure` to the new scoped command topic.
5. Remove legacy retained discovery messages under
   `homeassistant/<domain>/irrigation_*/config` that do not contain the
   instance node level, then remove obsolete legacy entities from HA.

Do not operate old and new command contracts simultaneously: doing so can
create ambiguous valve control and duplicate entities.
