#!/usr/bin/env python3
"""Render instance-scoped HA packages and dashboards from unscoped templates.

Source of truth (edit these, then re-run):
  homeassistant/templates/irrigation.yaml
  homeassistant/templates/irrigation_sensors.yaml
  homeassistant/templates/dashboard.yaml
  homeassistant/instances.yaml

Outputs:
  homeassistant/packages/irrigation_<id>.yaml
  homeassistant/packages/irrigation_sensors_<id>.yaml
  homeassistant/dashboards/irrigation_<id>.yaml
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None  # type: ignore

ROOT = Path(__file__).resolve().parents[1]
HA = ROOT / "homeassistant"
TEMPLATES = HA / "templates"
PACKAGES = HA / "packages"
DASHBOARDS = HA / "dashboards"
INSTANCES_FILE = HA / "instances.yaml"

ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,31}$")


def load_instances(path: Path) -> list[dict]:
    text = path.read_text()
    if yaml is not None:
        data = yaml.safe_load(text)
        instances = data.get("instances") or []
    else:
        # Minimal fallback without PyYAML
        instances = []
        cur: dict | None = None
        for line in text.splitlines():
            s = line.strip()
            if s.startswith("- id:"):
                if cur:
                    instances.append(cur)
                cur = {"id": s.split(":", 1)[1].strip()}
            elif cur is not None and s.startswith("name:"):
                cur["name"] = s.split(":", 1)[1].strip().strip("\"'")
        if cur:
            instances.append(cur)

    if not instances:
        raise SystemExit(f"No instances listed in {path}")

    seen: set[str] = set()
    for inst in instances:
        iid = str(inst["id"]).strip()
        name = str(inst["name"]).strip()
        if not ID_RE.match(iid):
            raise SystemExit(
                f"Invalid instance id {iid!r}: use lowercase letters, digits, "
                f"'_', '-' (max 32), matching firmware IRRIGATION_INSTANCE_ID"
            )
        if iid in seen:
            raise SystemExit(f"Duplicate instance id: {iid}")
        seen.add(iid)
        if not name:
            raise SystemExit(f"Instance {iid} needs a non-empty name")
        inst["id"] = iid
        inst["name"] = name
    return instances


def scope_package(text: str, instance_id: str, instance_name: str) -> str:
    """Apply multi-controller scoping to an unscoped package template."""
    # Protect filename mentions in comments from underscore rewrite
    protect = {
        "irrigation_sensors.yaml": "<<IRRIGATION_SENSORS_YAML>>",
        "packages/irrigation.yaml": "<<PACKAGES_IRRIGATION_YAML>>",
        "config/packages/irrigation.yaml": "<<CONFIG_PACKAGES_IRRIGATION_YAML>>",
    }
    for raw, token in protect.items():
        text = text.replace(raw, token)

    # MQTT topics: irrigation/<id>/...
    text = text.replace("irrigation/valve/", f"irrigation/{instance_id}/valve/")
    text = text.replace("irrigation/sensor/", f"irrigation/{instance_id}/sensor/")

    # Entity / helper / unique_id / script / automation prefixes
    # Do not rewrite already-scoped irrigation/<id>/ paths (no underscore after slash id)
    text = re.sub(
        rf"(?<![/\w])irrigation_(?!{re.escape(instance_id)}_)",
        f"irrigation_{instance_id}_",
        text,
    )

    for raw, token in protect.items():
        text = text.replace(token, raw)

    header = (
        f"# GENERATED — do not edit by hand.\n"
        f"# Instance: {instance_id} ({instance_name})\n"
        f"# Source: homeassistant/templates/ + instances.yaml\n"
        f"# Regenerate: python3 scripts/render_ha_instances.py\n"
        f"#\n"
    )
    # Soften leading package comment to keep guidance, after generated banner
    if text.startswith("#"):
        return header + text
    return header + text


def scope_dashboard(text: str, instance_id: str, instance_name: str) -> str:
    text = re.sub(
        rf"(?<![/\w])irrigation_(?!{re.escape(instance_id)}_)",
        f"irrigation_{instance_id}_",
        text,
    )
    # Dashboard chrome
    text = re.sub(
        r"^title:\s*Irrigation\s*$",
        f"title: {instance_name}",
        text,
        count=1,
        flags=re.M,
    )
    text = re.sub(
        r"^(\s+path:\s)irrigation\s*$",
        rf"\1irrigation-{instance_id}",
        text,
        count=1,
        flags=re.M,
    )
    text = re.sub(
        r"^(\s+path:\s)irrigation-moisture\s*$",
        rf"\1irrigation-{instance_id}-moisture",
        text,
        count=1,
        flags=re.M,
    )
    text = re.sub(
        r"^(\s+path:\s)irrigation-controls\s*$",
        rf"\1irrigation-{instance_id}-controls",
        text,
        count=1,
        flags=re.M,
    )
    text = re.sub(
        r"^(\s+path:\s)irrigation-sensors\s*$",
        rf"\1irrigation-{instance_id}-sensors",
        text,
        count=1,
        flags=re.M,
    )
    text = re.sub(
        r"^(\s+path:\s)irrigation-advanced\s*$",
        rf"\1irrigation-{instance_id}-advanced",
        text,
        count=1,
        flags=re.M,
    )

    header = (
        f"# GENERATED — do not edit by hand.\n"
        f"# Instance: {instance_id} ({instance_name})\n"
        f"# Source: homeassistant/templates/dashboard.yaml\n"
        f"# Regenerate: python3 scripts/render_ha_instances.py\n"
        f"#\n"
    )
    return header + text


def render(instances: list[dict], check_only: bool = False) -> int:
    tmpl_main = (TEMPLATES / "irrigation.yaml").read_text()
    tmpl_sensors = (TEMPLATES / "irrigation_sensors.yaml").read_text()
    tmpl_dash = (TEMPLATES / "dashboard.yaml").read_text()

    # Sanity: templates must still be unscoped
    if "irrigation/primary/" in tmpl_main or "irrigation_primary_" in tmpl_main:
        raise SystemExit(
            "templates/irrigation.yaml looks already scoped; restore unscoped template"
        )

    PACKAGES.mkdir(parents=True, exist_ok=True)
    DASHBOARDS.mkdir(parents=True, exist_ok=True)

    written: list[Path] = []
    for inst in instances:
        iid, name = inst["id"], inst["name"]
        outputs = {
            PACKAGES / f"irrigation_{iid}.yaml": scope_package(tmpl_main, iid, name),
            PACKAGES
            / f"irrigation_sensors_{iid}.yaml": scope_package(tmpl_sensors, iid, name),
            DASHBOARDS / f"irrigation_{iid}.yaml": scope_dashboard(tmpl_dash, iid, name),
        }
        for path, content in outputs.items():
            if check_only:
                if path.exists() and path.read_text() == content:
                    continue
                print(f"OUT OF DATE: {path.relative_to(ROOT)}")
                return 1
            path.write_text(content)
            written.append(path)
            print(f"Wrote {path.relative_to(ROOT)}")

    # Remove stale legacy unscoped package names if present
    for legacy in (
        PACKAGES / "irrigation.yaml",
        PACKAGES / "irrigation_sensors.yaml",
        DASHBOARDS / "irrigation.yaml",
    ):
        if legacy.exists():
            if check_only:
                print(f"STALE: {legacy.relative_to(ROOT)}")
                return 1
            legacy.unlink()
            print(f"Removed legacy {legacy.relative_to(ROOT)}")

    if check_only:
        print("Generated HA instance files are up to date.")
    else:
        print(f"Rendered {len(instances)} instance(s), {len(written)} files.")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Exit 1 if generated outputs differ from templates/instances",
    )
    args = parser.parse_args(argv)
    instances = load_instances(INSTANCES_FILE)
    return render(instances, check_only=args.check)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
