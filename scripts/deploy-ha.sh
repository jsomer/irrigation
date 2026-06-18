#!/usr/bin/env bash
# Deploy irrigation HA package and dashboard to a Home Assistant host.
#
# Usage:
#   ./scripts/deploy-ha.sh                          # print File Editor instructions
#   HA_HOST=10.0.4.169 HA_SMB_USER=homeassistant ./scripts/deploy-ha.sh
#
# Samba (HA Samba add-on) must be enabled for copy mode.

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HA_HOST="${HA_HOST:-10.0.4.169}"
SMB_SHARE="${HA_SMB_SHARE:-config}"

copy_via_samba() {
  local mount="/tmp/ha-config-$$"
  mkdir -p "$mount"
  trap 'umount "$mount" 2>/dev/null; rmdir "$mount" 2>/dev/null' EXIT
  echo "Mounting //$HA_HOST/$SMB_SHARE ..."
  mount_smbfs "//${HA_SMB_USER}@${HA_HOST}/${SMB_SHARE}" "$mount"
  mkdir -p "$mount/packages"
  cp "$ROOT/homeassistant/packages/irrigation.yaml" "$mount/packages/irrigation.yaml"
  echo "Copied package to $mount/packages/irrigation.yaml"
  echo "Dashboard: paste $ROOT/homeassistant/dashboards/irrigation.yaml via HA raw config editor"
  echo "Then restart Home Assistant."
}

if [[ -n "${HA_SMB_USER:-}" ]]; then
  copy_via_samba
else
  cat <<EOF
Deploy irrigation config to Home Assistant at http://${HA_HOST}:8123

1. File Editor → config/packages/irrigation.yaml
   Paste contents from:
   ${ROOT}/homeassistant/packages/irrigation.yaml

2. Settings → Dashboards → Irrigation → Raw configuration editor
   Paste contents from:
   ${ROOT}/homeassistant/dashboards/irrigation.yaml

3. Developer Tools → YAML → Restart Home Assistant

Optional Samba copy:
  HA_SMB_USER=homeassistant ${0}

After restart, power-cycle or reflash the UNO so MQTT discovery republishes.
EOF
fi
