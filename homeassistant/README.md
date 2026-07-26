# Home Assistant config (multi-controller)

## Layout

| Path | Role |
|------|------|
| `instances.yaml` | List of controllers (`id` + `name`) |
| `templates/` | **Edit these** — unscoped package + dashboard sources |
| `packages/irrigation_<id>.yaml` | **Generated** — do not hand-edit |
| `packages/irrigation_sensors_<id>.yaml` | **Generated** |
| `dashboards/irrigation_<id>.yaml` | **Generated** — paste via Lovelace raw editor |
| `SETUP_AFTER_RESTORE.md` | Deploy / migration checklist |

## Workflow

```bash
# 1. Edit templates/ and/or instances.yaml
# 2. Regenerate
python3 scripts/render_ha_instances.py
# 3. Deploy packages (and follow printed dashboard steps)
./scripts/deploy-ha.sh
```

Firmware `IRRIGATION_INSTANCE_ID` must match an `id` in `instances.yaml`.
Contract: [schemas/mqtt_topics.md](../schemas/mqtt_topics.md).
