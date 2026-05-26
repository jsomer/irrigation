# MQTT Topic Schema — Smart Irrigation Controller

All topics follow the pattern `irrigation/zone/<zoneId>/<suffix>`.
`zoneId` is a zero-based integer (e.g. `0`, `1`).

---

## Published by ESP32

### `irrigation/zone/<id>/telemetry`
Published every 10 s (configurable via `TELEMETRY_INTERVAL_MS`).

```json
{
  "zone": 0,
  "vwc": 32.4,
  "valve_open": false,
  "runtime_today_s": 180,
  "runtime_hour_s": 60,
  "pulse_count": 3,
  "state": "idle",
  "ts": 1234567
}
```

| Field | Type | Description |
|---|---|---|
| `zone` | int | Zone ID |
| `vwc` | float | Volumetric water content (%) from VH400 |
| `valve_open` | bool | Current valve state |
| `runtime_today_s` | int | Total valve-open seconds in current day window |
| `runtime_hour_s` | int | Total valve-open seconds in current hour window |
| `pulse_count` | int | Total pulses since boot |
| `state` | string | `idle` \| `pulsing` \| `settling` \| `fault` |
| `ts` | int | ESP32 uptime seconds at publish time |

---

### `irrigation/zone/<id>/status`
Retained. Published on connect (online) and as LWT (offline).

```
"online"
"offline"
```

---

## Subscribed by ESP32

### `irrigation/zone/<id>/command`
Send commands from Home Assistant automations or the AI layer.

#### `pulse` — request one pulse cycle
```json
{ "action": "pulse" }
```
Denied (and logged) if safety limits are exceeded or state is not `idle`.

#### `close` — immediately close valve
```json
{ "action": "close" }
```
Always succeeds. Transitions state to `idle`.

#### `clear_fault` — clear fault state
```json
{ "action": "clear_fault" }
```
Allows zone to operate again after a safety fault.

---

### `irrigation/zone/<id>/config`
AI/HA layer writes recommended parameter updates here.
**All values are clamped to hard safety limits inside the ESP32
firmware — the controller never blindly applies raw values.**

```json
{
  "action": "configure",
  "pulse_duration_s": 30,
  "settle_duration_s": 300,
  "target_vwc": 40.0,
  "resume_vwc": 25.0
}
```

| Field | Type | Hard limit enforced by ESP32 |
|---|---|---|
| `pulse_duration_s` | int | ≤ 120 s (MAX_PULSE_DURATION_S) |
| `settle_duration_s` | int | ≥ 60 s (MIN_SETTLE_S) |
| `target_vwc` | float | No hard limit; advisory only |
| `resume_vwc` | float | No hard limit; advisory only |

---

## Safety Invariants

| Limit | Value | Enforced by |
|---|---|---|
| Max single pulse | 120 s | ESP32 firmware (hard `#define`) |
| Max runtime / hour | 600 s | ESP32 firmware (hard `#define`) |
| Max runtime / day | 3600 s | ESP32 firmware (hard `#define`) |
| Min settle gap | 60 s | ESP32 firmware (hard `#define`) |
| Failsafe on disconnect | 120 s | ESP32 firmware, closes valve |

The AI layer **may only recommend** changes via the `config` topic.
It cannot bypass any of the above limits.
