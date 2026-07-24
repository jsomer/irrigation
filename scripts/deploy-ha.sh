#!/usr/bin/env bash
# Deploy irrigation HA package and dashboard to a Home Assistant host.
#
# Usage:
#   ./scripts/deploy-ha.sh                          # print File Editor instructions
#   HA_HOST=10.0.0.110 HA_SMB_USER=homeassistant ./scripts/deploy-ha.sh
#
# Samba (HA Samba add-on) must be enabled for copy mode.

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HA_HOST="${HA_HOST:-10.0.0.110}"
SMB_SHARE="${HA_SMB_SHARE:-config}"
SMB_SUBDIR="${HA_SMB_SUBDIR:-.}"
HA_MOUNT_PATH="${HA_MOUNT_PATH:-}"

cleanup_mount() {
  local mount="$1"
  umount "$mount" 2>/dev/null || true
  rmdir "$mount" 2>/dev/null || true
}

copy_packages() {
  local dest_root="$1"
  local packages_dir="$dest_root/packages"
  mkdir -p "$packages_dir"
  cp "$ROOT/homeassistant/packages/irrigation.yaml" "$packages_dir/irrigation.yaml"
  cp "$ROOT/homeassistant/packages/irrigation_sensors.yaml" "$packages_dir/irrigation_sensors.yaml"
  echo "Copied packages to $packages_dir/irrigation.yaml and irrigation_sensors.yaml"
}

resolve_share_root() {
  local base="$1"
  if [[ "$SMB_SUBDIR" == "." ]]; then
    printf '%s\n' "$base"
  else
    printf '%s\n' "${base%/}/$SMB_SUBDIR"
  fi
}

print_samba_help() {
  cat <<EOF
Samba copy failed. Things to check:

1. Confirm the Raspberry Pi / Home Assistant host is reachable at ${HA_HOST}
2. Confirm the Samba share name is correct (current: ${SMB_SHARE})
3. Confirm the destination path inside that share is correct (current: ${SMB_SUBDIR})
4. Confirm the Samba add-on or SMB server is enabled and your user can write there

More reliable fallback on macOS:
  1. Mount the share in Finder using smb://${HA_HOST}/${SMB_SHARE}
  2. Re-run with:
     HA_MOUNT_PATH=/Volumes/${SMB_SHARE} ${0}

If the packages live under a subfolder in the share, add:
  HA_SMB_SUBDIR=config
EOF
}

copy_via_samba() {
  local mount="/tmp/ha-config-$$"
  mkdir -p "$mount"
  trap "cleanup_mount \"$mount\"" EXIT
  echo "Mounting //$HA_HOST/$SMB_SHARE ..."
  if ! mount_smbfs "//${HA_SMB_USER}@${HA_HOST}/${SMB_SHARE}" "$mount"; then
    print_samba_help
    return 1
  fi
  copy_packages "$(resolve_share_root "$mount")"
  echo "NOTE: Deploy irrigation.yaml only — NOT dashboards/irrigation.yaml (that file has title:/views:)"
  echo "Dashboard: paste $ROOT/homeassistant/dashboards/irrigation.yaml via HA raw config editor"
  echo "Then restart Home Assistant."
}

copy_via_mount_path() {
  local share_root
  share_root="$(resolve_share_root "$HA_MOUNT_PATH")"
  if [[ ! -d "$HA_MOUNT_PATH" ]]; then
    echo "HA_MOUNT_PATH does not exist: $HA_MOUNT_PATH" >&2
    echo "Mount the SMB share first, then point HA_MOUNT_PATH at that mounted folder." >&2
    return 1
  fi
  copy_packages "$share_root"
  echo "Copied via mounted share at $share_root"
  echo "Dashboard: paste $ROOT/homeassistant/dashboards/irrigation.yaml via HA raw config editor"
  echo "Then restart Home Assistant."
}

if [[ -n "$HA_MOUNT_PATH" ]]; then
  copy_via_mount_path
elif [[ -n "${HA_SMB_USER:-}" ]]; then
  copy_via_samba
else
  cat <<EOF
Deploy irrigation config to Home Assistant at http://${HA_HOST}:8123

1. File Editor → config/packages/
   Paste contents from:
   ${ROOT}/homeassistant/packages/irrigation.yaml
   ${ROOT}/homeassistant/packages/irrigation_sensors.yaml

   IMPORTANT: Deploy BOTH package files. This is NOT the dashboard file.

2. Settings → Dashboards → Irrigation → Raw configuration editor
   Paste contents from:
   ${ROOT}/homeassistant/dashboards/irrigation.yaml

   Moisture history uses built-in history-graph cards (no HACS auto-entities).

3. Developer Tools → YAML → Check configuration, then Restart Home Assistant

See homeassistant/SETUP_AFTER_RESTORE.md for the full post-backup-restore checklist.

Optional Samba copy:
  HA_SMB_USER=homeassistant ${0}

Optional copy to an already-mounted Finder share:
  HA_MOUNT_PATH=/Volumes/${SMB_SHARE} ${0}

If the files should land in a subfolder inside the share:
  HA_SMB_SUBDIR=config HA_MOUNT_PATH=/Volumes/${SMB_SHARE} ${0}

After restart, power-cycle or reflash the UNO so MQTT discovery republishes.
EOF
fi
