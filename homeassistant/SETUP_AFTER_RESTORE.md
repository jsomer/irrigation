# Home Assistant — setup after backup restore

Multi-controller layout: every physical UNO has an `IRRIGATION_INSTANCE_ID`
(see [schemas/mqtt_topics.md](../schemas/mqtt_topics.md)). HA packages and
dashboards are **generated** per instance from templates.

| # | Source in repo | Destination on HA | Method |
|---|----------------|-------------------|--------|
| 1 | Package loader (below) | `config/configuration.yaml` | edit once |
| 2 | `homeassistant/packages/irrigation_<id>.yaml` | `config/packages/` | File Editor / Samba |
| 3 | `homeassistant/packages/irrigation_sensors_<id>.yaml` | `config/packages/` | File Editor / Samba |
| 4 | `homeassistant/dashboards/irrigation_<id>.yaml` | Lovelace dashboard (one per instance) | Raw config editor (NOT packages/) |

Edit **templates** + `instances.yaml`, then:

```bash
python3 scripts/render_ha_instances.py
# or just:
./scripts/deploy-ha.sh
```

Do **not** hand-edit generated `packages/irrigation_*.yaml` or dashboards.

## configuration.yaml

List every generated package (example matches `homeassistant/instances.yaml`):

```yaml
homeassistant:
  packages:
    irrigation_raised_bed: !include packages/irrigation_raised_bed.yaml
    irrigation_sensors_raised_bed: !include packages/irrigation_sensors_raised_bed.yaml
    irrigation_vegetable_garden: !include packages/irrigation_vegetable_garden.yaml
    irrigation_sensors_vegetable_garden: !include packages/irrigation_sensors_vegetable_garden.yaml
```

If `config/packages/` contains **only** these irrigation files, you may instead use:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Remove legacy unscoped files if present: `packages/irrigation.yaml`,
`packages/irrigation_sensors.yaml`.

## MQTT broker user

Create a dedicated MQTT user for the UNOs (matches `firmware/src/secrets.h.example`):

1. **Settings → Add-ons → Mosquitto broker → Configuration** (or the Mosquitto user UI)
2. Add user `irrigation` with a strong password
3. Copy the same credentials into each board’s `firmware/src/secrets.h`:
   - `MQTT_BROKER` — HA IP or hostname
   - `MQTT_PORT` — `1883`
   - `MQTT_USER` / `MQTT_PASSWORD`
   - `IRRIGATION_INSTANCE_ID` — unique per board (`raised_bed`, `vegetable_garden`, …)
   - `IRRIGATION_INSTANCE_NAME` — display name (`Raised Bed`, `Vegetable Garden`)

Recommended Mosquitto add-on settings: Start on boot **on**, Watchdog **on**.

## Migration from legacy single-controller topics

Old firmware used unscoped `irrigation/valve/...` and `irrigation/sensor/...`.
New firmware **only** uses `irrigation/<instance_id>/...`. Order matters:

1. Deploy instance-scoped HA packages + dashboards (`raised_bed` for the first board).
2. Restart Home Assistant; stop any automations still publishing to legacy command topics.
3. Flash the first UNO with `IRRIGATION_INSTANCE_ID "raised_bed"` (name `Raised Bed`).
4. Confirm `binary_sensor.irrigation_raised_bed_controller_online` is **on** and Sync runs.
5. Purge legacy retained discovery under `homeassistant/<domain>/irrigation_*/config`
   that lacks the instance node (see [mqtt_topics.md](../schemas/mqtt_topics.md)).
6. Flash the second UNO with `vegetable_garden` / `Vegetable Garden` and Sync that dashboard.

Do not run old and new command contracts at the same time.

## Deploy steps

1. Restore backup in Home Assistant (if applicable)
2. Render + deploy packages:
   - **Samba:** `HA_SMB_USER=homeassistant ./scripts/deploy-ha.sh`
   - **Manual:** run `python3 scripts/render_ha_instances.py`, then copy every
     `irrigation_*.yaml` / `irrigation_sensors_*.yaml` into `config/packages/`
3. Update `configuration.yaml` as above
4. **Developer Tools → YAML → Check configuration** → **Restart**
5. Create/update Lovelace dashboards — paste each
   `homeassistant/dashboards/irrigation_<id>.yaml` via raw editor
6. Flash UNOs with matching instance IDs ([firmware_deploy.md](../docs/firmware_deploy.md))

**Important:** Package files must start with a domain key (`input_select:` etc.),
never `title:` / `views:`. Dashboard YAML goes only through the Lovelace editor.

## Verify after restart

**Developer Tools → States** (example for Raised Bed; swap id for Vegetable Garden):

| Entity | Expected |
|--------|----------|
| `input_select.irrigation_raised_bed_control_mode` | last setting (helpers persist) |
| `sensor.irrigation_raised_bed_valve_state` | `idle` |
| `binary_sensor.irrigation_raised_bed_controller_online` | `on` after UNO connects |
| `input_boolean.irrigation_raised_bed_sensor_N_enabled` | your enables |

Set control mode to `auto` only after a manual test pulse on that instance.

On a **brand-new** install (not a restore), set mode, timings, bands, and enables —
see header comments in the templates.

## After deploy

Power-cycle or reflash each UNO so MQTT Discovery republishes under
`homeassistant/<domain>/irrigation_<id>/...`.

See: [ha_drip_control.md](../docs/ha_drip_control.md), [mqtt_topics.md](../schemas/mqtt_topics.md), [README.md](../README.md).
