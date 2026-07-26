#!/usr/bin/env bash
# Deploy irrigation HA packages and print dashboard paste instructions.
#
# Usage:
#   ./scripts/deploy-ha.sh                          # print File Editor instructions
#   HA_HOST=10.0.0.110 HA_SMB_USER=homeassistant ./scripts/deploy-ha.sh
#
# Regenerates instance-scoped packages from templates + instances.yaml first.
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

render_instances() {
  python3 "$ROOT/scripts/render_ha_instances.py"
}

package_files() {
  local packages_dir="$ROOT/homeassistant/packages"
  # Main packages first, then sensor packages (avoid double-listing via irrigation_*.yaml)
  ls -1 "$packages_dir"/irrigation_*.yaml 2>/dev/null | grep -v '/irrigation_sensors_' || true
  ls -1 "$packages_dir"/irrigation_sensors_*.yaml 2>/dev/null || true
}

dashboard_files() {
  ls -1 "$ROOT/homeassistant/dashboards"/irrigation_*.yaml 2>/dev/null
}

copy_packages() {
  local dest_root="$1"
  local packages_dir="$dest_root/packages"
  mkdir -p "$packages_dir"

  # Remove legacy unscoped package names that would fight the new contract
  rm -f "$packages_dir/irrigation.yaml" "$packages_dir/irrigation_sensors.yaml"

  local f
  while IFS= read -r f; do
    cp "$f" "$packages_dir/$(basename "$f")"
    echo "Copied $(basename "$f") → $packages_dir/"
  done < <(package_files)

  echo
  echo "Dashboards (paste via HA raw config editor — NOT into packages/):"
  while IFS= read -r f; do
    echo "  $(basename "$f")  ←  $f"
  done < <(dashboard_files)
  echo
  echo "Then: Developer Tools → YAML → Check configuration → Restart Home Assistant."
  echo "Flash each UNO with matching IRRIGATION_INSTANCE_ID (see docs/firmware_deploy.md)."
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
}

render_instances

if [[ -n "$HA_MOUNT_PATH" ]]; then
  copy_via_mount_path
elif [[ -n "${HA_SMB_USER:-}" ]]; then
  copy_via_samba
else
  cat <<EOF
Deploy multi-controller irrigation config to Home Assistant at http://${HA_HOST}:8123

Instances are listed in homeassistant/instances.yaml (currently: raised_bed, vegetable_garden).
Regenerate after editing templates:

  python3 ${ROOT}/scripts/render_ha_instances.py

1. File Editor → config/packages/
   Copy EVERY generated package (and delete legacy unscoped names):
EOF
  while IFS= read -r f; do
    echo "   - $(basename "$f")"
  done < <(package_files)
  cat <<EOF

   Remove if present: irrigation.yaml, irrigation_sensors.yaml (legacy unscoped).

   IMPORTANT: Do NOT put dashboard YAML in packages/ (those files start with title:/views:).

2. Settings → Dashboards — create or update one dashboard per instance.
   Paste raw YAML from:
EOF
  while IFS= read -r f; do
    echo "   - $f"
  done < <(dashboard_files)
  cat <<EOF

3. Developer Tools → YAML → Check configuration, then Restart Home Assistant

4. Flash each UNO with matching secrets:
     IRRIGATION_INSTANCE_ID / IRRIGATION_INSTANCE_NAME
   Existing controller → id "raised_bed". Second board → "vegetable_garden".
   See docs/firmware_deploy.md and schemas/mqtt_topics.md (migration).

See homeassistant/SETUP_AFTER_RESTORE.md for the full checklist.

Optional Samba copy:
  HA_SMB_USER=homeassistant ${0}

Optional copy to an already-mounted Finder share:
  HA_MOUNT_PATH=/Volumes/${SMB_SHARE} ${0}

If the files should land in a subfolder inside the share:
  HA_SMB_SUBDIR=config HA_MOUNT_PATH=/Volumes/${SMB_SHARE} ${0}
EOF
fi
