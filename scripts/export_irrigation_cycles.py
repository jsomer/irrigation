#!/usr/bin/env python3
"""Export irrigation_cycle_complete events and optional VWC time series from Home Assistant."""

from __future__ import annotations

import argparse
import csv
import json
import os
import sqlite3
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

EVENT_TYPE = "irrigation_cycle_complete"

# All event fields (schema) plus export-time derived columns.
CYCLE_COLUMNS = [
    "cycle_id",
    "ended_at",
    "cycle_started_at",
    "duration_min",
    "duration_min_setting",
    "actual_pulse_s",
    "gallons_estimated",
    "gallons_actual",
    "early_stop",
    "outcome",
    "cycles_this_event",
    "runtime_today_s",
    "settle_min_setting",
    "system_gph",
    "resume_vwc_0",
    "resume_vwc_1",
    "target_vwc_0",
    "target_vwc_1",
    "max_vwc_0",
    "max_vwc_1",
    "s0_role",
    "s1_role",
    "s0_vwc_before",
    "s0_vwc_end",
    "s0_vwc_check",
    "s0_vwc_60m",
    "s0_delta_check",
    "s1_vwc_before",
    "s1_vwc_end",
    "s1_vwc_check",
    "s1_vwc_60m",
    "s1_delta_check",
    "s0_gap_to_target",
    "s1_headroom_to_max",
    "s0_vwc_per_gal",
    "s1_overshoot_60m",
    "limiting_sensor",
]

VWC_ENTITIES = [
    "sensor.irrigation_sensor_0_vwc_resolved",
    "sensor.irrigation_sensor_1_vwc_resolved",
    "sensor.irrigation_valve_state_resolved",
]

TIMESERIES_COLUMNS = ["cycle_id", "timestamp", "entity_id", "state"]


def _parse_float(value: Any, default: float = 0.0) -> float:
    if value is None or value == "":
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _coerce_event_row(raw: dict[str, Any]) -> dict[str, Any]:
    """Normalize event_data dict to a flat row with derived columns."""
    row = {col: raw.get(col, "") for col in CYCLE_COLUMNS if col in (
        "cycle_id", "ended_at", "cycle_started_at", "duration_min", "duration_min_setting",
        "actual_pulse_s", "gallons_estimated", "gallons_actual", "early_stop", "outcome",
        "cycles_this_event", "runtime_today_s", "settle_min_setting", "system_gph",
        "resume_vwc_0", "resume_vwc_1", "target_vwc_0", "target_vwc_1",
        "max_vwc_0", "max_vwc_1", "s0_role", "s1_role",
        "s0_vwc_before", "s0_vwc_end", "s0_vwc_check", "s0_vwc_60m", "s0_delta_check",
        "s1_vwc_before", "s1_vwc_end", "s1_vwc_check", "s1_vwc_60m", "s1_delta_check",
    )}

    target_0 = _parse_float(raw.get("target_vwc_0"), 44.0)
    target_1 = _parse_float(raw.get("target_vwc_1"), 44.0)
    max_vwc_1 = _parse_float(raw.get("max_vwc_1"), 50.0)
    s0_check = _parse_float(raw.get("s0_vwc_check"))
    s1_check = _parse_float(raw.get("s1_vwc_check"))
    s0_before = _parse_float(raw.get("s0_vwc_before"))
    s1_before = _parse_float(raw.get("s1_vwc_before"))
    s1_60m = _parse_float(raw.get("s1_vwc_60m"))
    gallons = _parse_float(raw.get("gallons_actual") or raw.get("gallons_estimated"))
    s0_delta = _parse_float(raw.get("s0_delta_check"), s0_check - s0_before)

    row["s0_gap_to_target"] = round(target_0 - s0_check, 2)
    row["s1_headroom_to_max"] = round(max_vwc_1 - s1_check, 2)
    row["s0_vwc_per_gal"] = round(s0_delta / gallons, 4) if gallons > 0 else ""
    row["s1_overshoot_60m"] = round(s1_60m - target_1, 2)
    row["limiting_sensor"] = "s0" if s0_before < s1_before else "s1"
    return row


def export_from_sqlite(db_path: Path, days: int) -> list[dict[str, Any]]:
    """Read irrigation_cycle_complete events from the HA recorder database."""
    if not db_path.is_file():
        raise FileNotFoundError(f"Database not found: {db_path}")

    since = datetime.now(timezone.utc) - timedelta(days=days)
    since_iso = since.strftime("%Y-%m-%d %H:%M:%S")

    conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    try:
        rows = conn.execute(
            """
            SELECT e.time_fired, ed.shared_data
            FROM events e
            JOIN event_data ed ON e.data_id = ed.data_id
            WHERE e.event_type = ?
              AND e.time_fired >= ?
            ORDER BY e.time_fired
            """,
            (EVENT_TYPE, since_iso),
        ).fetchall()
    finally:
        conn.close()

    events: list[dict[str, Any]] = []
    for row in rows:
        try:
            payload = json.loads(row["shared_data"])
        except json.JSONDecodeError:
            continue
        event_data = payload.get("event_data") or payload
        if not isinstance(event_data, dict):
            continue
        if not event_data.get("ended_at"):
            event_data["ended_at"] = row["time_fired"]
        events.append(_coerce_event_row(event_data))
    return events


def _ha_request(url: str, token: str, path: str, params: dict[str, str] | None = None) -> Any:
    query = f"?{urllib.parse.urlencode(params)}" if params else ""
    req = urllib.request.Request(
        f"{url.rstrip('/')}{path}{query}",
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        return json.loads(resp.read().decode())


def export_from_logbook(url: str, token: str, days: int) -> list[dict[str, Any]]:
    """Best-effort export via HA logbook REST API."""
    start = datetime.now(timezone.utc) - timedelta(days=days)
    end = datetime.now(timezone.utc)
    params = {
        "end_time": end.isoformat(),
    }
    entries = _ha_request(url, token, f"/api/logbook/{start.isoformat()}", params)

    events: list[dict[str, Any]] = []
    for entry in entries:
        name = entry.get("name") or entry.get("context_event_type") or ""
        if name != EVENT_TYPE:
            continue
        event_data = entry.get("context_event_data") or {}
        if not event_data:
            # Some HA versions nest data differently; skip incomplete rows.
            continue
        if not event_data.get("ended_at"):
            event_data["ended_at"] = entry.get("when", "")
        events.append(_coerce_event_row(event_data))
    return events


def export_timeseries(
    url: str,
    token: str,
    cycles: list[dict[str, Any]],
    window_before_min: int,
    window_after_min: int,
) -> list[dict[str, Any]]:
    """Export VWC/valve history around each cycle start."""
    rows: list[dict[str, Any]] = []
    for cycle in cycles:
        cycle_id = cycle.get("cycle_id") or cycle.get("ended_at")
        started = cycle.get("cycle_started_at") or cycle.get("ended_at")
        if not started:
            continue
        try:
            start_dt = datetime.fromisoformat(str(started).replace("Z", "+00:00"))
        except ValueError:
            continue
        if start_dt.tzinfo is None:
            start_dt = start_dt.replace(tzinfo=timezone.utc)
        range_start = start_dt - timedelta(minutes=window_before_min)
        range_end = start_dt + timedelta(minutes=window_after_min)
        params = {
            "filter_entity_id": ",".join(VWC_ENTITIES),
            "minimal_response": "true",
            "no_attributes": "true",
            "start_time": range_start.isoformat(),
            "end_time": range_end.isoformat(),
        }
        try:
            history = _ha_request(url, token, "/api/history/period", params)
        except urllib.error.URLError as exc:
            print(f"Warning: history fetch failed for cycle {cycle_id}: {exc}", file=sys.stderr)
            continue
        for entity_history in history:
            if not entity_history:
                continue
            entity_id = entity_history[0].get("entity_id", "")
            for point in entity_history:
                rows.append({
                    "cycle_id": cycle_id,
                    "timestamp": point.get("last_changed") or point.get("last_updated", ""),
                    "entity_id": entity_id,
                    "state": point.get("state", ""),
                })
    return rows


def write_csv(path: Path, columns: list[str], rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def _ha_call_service(url: str, token: str, domain: str, service: str, data: dict[str, Any]) -> None:
    payload = json.dumps(data).encode()
    req = urllib.request.Request(
        f"{url.rstrip('/')}/api/services/{domain}/{service}",
        data=payload,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=30):
        pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--days", type=int, default=90, help="Lookback window (default: 90)")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("data/irrigation_cycles.csv"),
        help="Output CSV path",
    )
    parser.add_argument(
        "--timeseries-out",
        type=Path,
        default=None,
        help="Optional VWC time series CSV (requires --url and --token)",
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=None,
        help="Path to home-assistant_v2.db (recommended)",
    )
    parser.add_argument(
        "--url",
        default=os.environ.get("HA_URL", ""),
        help="Home Assistant URL (or HA_URL env)",
    )
    parser.add_argument(
        "--token",
        default=os.environ.get("HA_TOKEN", ""),
        help="Long-lived access token (or HA_TOKEN env)",
    )
    parser.add_argument(
        "--apply-to-ha",
        action="store_true",
        help="Set input_datetime.irrigation_analysis_last_export on HA",
    )
    args = parser.parse_args()

    cycles: list[dict[str, Any]] = []
    if args.db:
        cycles = export_from_sqlite(args.db, args.days)
        print(f"SQLite: found {len(cycles)} {EVENT_TYPE} events in last {args.days} days")
    elif args.url and args.token:
        cycles = export_from_logbook(args.url, args.token, args.days)
        print(f"Logbook API: found {len(cycles)} {EVENT_TYPE} events in last {args.days} days")
        if not cycles:
            print(
                "No events via logbook. For full event_data, use --db with the recorder SQLite file.",
                file=sys.stderr,
            )
    else:
        print("Provide --db or both --url and --token (HA_URL / HA_TOKEN).", file=sys.stderr)
        return 1

    if not cycles:
        print("No cycle events exported.", file=sys.stderr)
        return 2

    write_csv(args.out, CYCLE_COLUMNS, cycles)
    print(f"Wrote {len(cycles)} rows to {args.out}")

    if args.timeseries_out:
        if not (args.url and args.token):
            print("--timeseries-out requires --url and --token", file=sys.stderr)
            return 1
        ts_rows = export_timeseries(args.url, args.token, cycles, 30, 90)
        write_csv(args.timeseries_out, TIMESERIES_COLUMNS, ts_rows)
        print(f"Wrote {len(ts_rows)} time series rows to {args.timeseries_out}")

    if args.apply_to_ha:
        if not (args.url and args.token):
            print("--apply-to-ha requires --url and --token", file=sys.stderr)
            return 1
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        try:
            _ha_call_service(args.url, args.token, "input_datetime", "set_datetime", {
                "entity_id": "input_datetime.irrigation_analysis_last_export",
                "datetime": now,
            })
            print("Updated irrigation_analysis_last_export on Home Assistant.")
        except urllib.error.URLError as exc:
            print(f"Failed to update HA: {exc}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
