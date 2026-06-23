# Home Assistant — setup after backup restore
#
# You only need TWO things in HA config:
#   1. configuration.yaml  — package loader (below)
#   2. packages/irrigation.yaml — single file from this repo
#
# Dashboard is separate (Lovelace raw editor, not packages/).

# ── Add to configuration.yaml ─────────────────────────────────────────────────
# If you already have a homeassistant: block, merge the packages: section in.

homeassistant:
  packages:
    irrigation: !include packages/irrigation.yaml

# Do NOT use:
#   packages: !include_dir_named packages/
# That loads every file in packages/ as a separate package and causes errors.

# ── Deploy steps ──────────────────────────────────────────────────────────────
# 1. Restore backup in Home Assistant
# 2. Studio Code Server (or Samba): replace config/packages/irrigation.yaml
#    with homeassistant/packages/irrigation.yaml from this repo
# 3. Confirm configuration.yaml matches the block above
# 4. Developer Tools → YAML → Check configuration
# 5. Restart Home Assistant
# 6. Settings → Dashboards → Irrigation → Raw configuration editor
#    paste homeassistant/dashboards/irrigation.yaml
# 7. Flash UNO firmware when ready (drip limits + HA control)
#
# ── Verify after restart (Developer Tools → States) ───────────────────────────
#   input_select.irrigation_control_mode  →  manual (default)
#   sensor.irrigation_operational_status
#   sensor.irrigation_valve_phase
#
# ── From Mac (Samba add-on) ───────────────────────────────────────────────────
#   HA_SMB_USER=homeassistant ./scripts/deploy-ha.sh
