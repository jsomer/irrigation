# Home Assistant — setup after backup restore

You only need TWO things in HA config:

1. `configuration.yaml` — package loader (below)
2. `packages/irrigation.yaml` — single file from this repo

Dashboard is separate (Lovelace raw editor, not packages/).

## configuration.yaml

If you already have a `homeassistant:` block, merge the `packages:` section in.

```yaml
homeassistant:
  packages:
    irrigation: !include packages/irrigation.yaml
```

Do **not** use:

```yaml
packages: !include_dir_named packages/
```

That loads every file in `packages/` as a separate package and causes errors.

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
2. Deploy the package:
   - **Samba:** `HA_SMB_USER=homeassistant ./scripts/deploy-ha.sh`
   - **File Editor:** paste `homeassistant/packages/irrigation.yaml` into `config/packages/irrigation.yaml`
3. Confirm `configuration.yaml` matches the block above
4. **Developer Tools → YAML → Check configuration**
5. **Restart Home Assistant**
6. **Settings → Dashboards → Irrigation → Raw configuration editor** — paste `homeassistant/dashboards/irrigation.yaml`
7. Flash UNO firmware when ready (drip limits + HA control)

`deploy-ha.sh` without `HA_SMB_USER` prints File Editor instructions.

**Important:** Deploy `packages/irrigation.yaml` only — not `dashboards/irrigation.yaml`. The package must start with `input_select:` — not `title:` or `views:`.

## Verify after restart

**Developer Tools → States:**

| Entity | Expected |
|--------|----------|
| `input_select.irrigation_control_mode` | `manual` (default) |
| `sensor.irrigation_operational_status` | `idle` |
| `sensor.irrigation_valve_phase` | `Idle` |
| `binary_sensor.irrigation_controller_online_resolved` | `on` (after UNO connects) |

Set `irrigation_control_mode` to `auto` only after verifying a manual test cycle.

## After deploy

Power-cycle or reflash the UNO so MQTT Discovery republishes entity configs.

See also: [docs/ha_drip_control.md](../docs/ha_drip_control.md), [README.md](../README.md).
