# Home Assistant — setup after backup restore

What goes on the HA server:

| # | Source in repo | Destination on HA | Method |
|---|----------------|-------------------|--------|
| 1 | `configuration.yaml` package loader (below) | `config/configuration.yaml` | edit once |
| 2 | `homeassistant/packages/irrigation.yaml` | `config/packages/irrigation.yaml` | File Editor / Samba |
| 3 | `homeassistant/packages/irrigation_sensors.yaml` | `config/packages/irrigation_sensors.yaml` | File Editor / Samba |
| 4 | `homeassistant/dashboards/irrigation.yaml` | Lovelace dashboard | Raw config editor (NOT packages/) |

Deploy **both** package files (2 and 3) — the sensor slots (0–5), their bands,
calibration, and settle snapshots live in `irrigation_sensors.yaml`. The
dashboard is separate: it starts with `title:` / `views:` and must go through
the Lovelace raw editor, never into `config/packages/`.

## configuration.yaml

If you already have a `homeassistant:` block, merge the `packages:` section in.

```yaml
homeassistant:
  packages:
    irrigation: !include packages/irrigation.yaml
    irrigation_sensors: !include packages/irrigation_sensors.yaml
```

Both packages must be listed. Do **not** use:

```yaml
packages: !include_dir_named packages/
```

That loads every file in `packages/` as a separate package and can cause errors
if other files are present.

## MQTT broker user

Create a dedicated MQTT user for the UNO (matches `firmware/src/secrets.h.example`):

1. **Settings → Add-ons → Mosquitto broker → Configuration** (or the Mosquitto user UI)
2. Add user `irrigation` with a strong password
3. Copy the same credentials into `firmware/src/secrets.h`:
   - `MQTT_BROKER` — HA IP or hostname
   - `MQTT_PORT` — `1883`
   - `MQTT_USER` / `MQTT_PASSWORD`

Recommended Mosquitto add-on settings:

- Start on boot: **on**
- Watchdog: **on**

## Deploy steps

1. Restore backup in Home Assistant
2. Deploy **both** package files:
   - **Samba:** `HA_SMB_USER=homeassistant ./scripts/deploy-ha.sh` (copies both)
   - **File Editor:** paste `homeassistant/packages/irrigation.yaml` and
     `homeassistant/packages/irrigation_sensors.yaml` into `config/packages/`
3. Confirm `configuration.yaml` matches the block above (both `!include` lines)
4. **Developer Tools → YAML → Check configuration**
5. **Restart Home Assistant**
6. **Settings → Dashboards → Irrigation → Raw configuration editor** — paste `homeassistant/dashboards/irrigation.yaml`
7. Flash UNO firmware when ready (drip limits + HA control)

`deploy-ha.sh` without `HA_SMB_USER` prints File Editor instructions.

**Important:** The two files in `config/packages/` must start with a domain key
(`input_select:` / `input_boolean:`) — never `title:` or `views:`. The
`dashboards/irrigation.yaml` file (which does start with `title:`) goes only
through the Lovelace raw editor.

## Verify after restart

**Developer Tools → States:**

| Entity | Expected |
|--------|----------|
| `input_select.irrigation_control_mode` | your last setting (helpers now persist across restarts) |
| `sensor.irrigation_valve_state` | `idle` |
| `binary_sensor.irrigation_controller_online` | `on` (after UNO connects) |
| `input_boolean.irrigation_sensor_2_enabled` … `_5_enabled` | your last setting (no longer reset on restart) |

Set `irrigation_control_mode` to `auto` only after verifying a manual test cycle.

Note: helpers no longer carry `initial:` values, so on a **brand-new** install
(not a restore) set control mode, settle/pulse durations, per-sensor bands, and
enable the sensor slots you use — see the header comments in both package files
for recommended starting values.

## After deploy

Power-cycle or reflash the UNO so MQTT Discovery republishes entity configs.

See also: [docs/ha_drip_control.md](../docs/ha_drip_control.md), [README.md](../README.md).
