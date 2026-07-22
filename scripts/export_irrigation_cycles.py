#!/usr/bin/env python3
"""Export irrigation settle snapshots or legacy cycle events from Home Assistant."""

from __future__ import annotations

import argparse
import ast
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
from typing import Any, Callable

EVENT_SETTLE = "irrigation_settle_snapshot"
EVENT_LEGACY = "irrigation_cycle_complete"

SETTLE_COLUMNS = [
    "recorded_at",
    "actual_pulse_s",
    "requested_pulse_s",
    "pulse_shortfall_s",
    "s0_vwc",
    "s0_vwc_before",
    "s0_delta",
    "s1_vwc",
    "s1_vwc_before",
    "s1_delta",
    "s2_vwc",
    "s2_vwc_before",
    "s2_delta",
    "s3_vwc",
    "s3_vwc_before",
    "s3_delta",
    "s4_vwc",
    "s4_vwc_before",
    "s4_delta",
    "s5_vwc",
    "s5_vwc_before",
    "s5_delta",
    "min_delta",
    "max_delta",
    "avg_delta",
    "limiting_sensor",
]

LEGACY_COLUMNS = [
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
    "sensor.irrigation_sensor_0_vwc",
    "sensor.irrigation_sensor_1_vwc",
    "sensor.irrigation_sensor_2_vwc",
    "sensor.irrigation_sensor_3_vwc",
    "sensor.irrigation_sensor_4_vwc",
    "sensor.irrigation_sensor_5_vwc",
    "sensor.irrigation_valve_state",
]

TIMESERIES_COLUMNS = ["recorded_at", "timestamp", "entity_id", "state"]


def _parse_float(value: Any, default: float = 0.0) -> float:
    if value is None or value == "":
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _parse_sensors(value: Any) -> list[dict[str, Any]]:
    if value is None or value == "":
        return []
    if isinstance(value, list):
        return [s for s in value if isinstance(s, dict)]
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return []
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            try:
                parsed = ast.literal_eval(text)
            except (SyntaxError, ValueError):
                return []
        if isinstance(parsed, list):
            return [s for s in parsed if isinstance(s, dict)]
    return []


def _coerce_settle_row(raw: dict[str, Any], time_fired: str = "") -> dict[str, Any]:
    recorded_at = raw.get("recorded_at") or time_fired
    actual = int(_parse_float(raw.get("actual_pulse_s")))
    requested = int(_parse_float(raw.get("requested_pulse_s")))
    row: dict[str, Any] = {
        "recorded_at": recorded_at,
        "actual_pulse_s": actual,
        "requested_pulse_s": requested,
        "pulse_shortfall_s": max(0, requested - actual),
    }

    deltas: list[tuple[int, float]] = []
    for sensor in _parse_sensors(raw.get("sensors")):
        sid = int(_parse_float(sensor.get("id"), -1))
        if sid < 0 or sid > 5:
            continue
        vwc = _parse_float(sensor.get("vwc"))
        before = _parse_float(sensor.get("vwc_before"))
        delta = _parse_float(sensor.get("delta"), vwc - before)
        row[f"s{sid}_vwc"] = round(vwc, 2)
        row[f"s{sid}_vwc_before"] = round(before, 2)
        row[f"s{sid}_delta"] = round(delta, 2)
        deltas.append((sid, delta))

    if deltas:
        row["min_delta"] = round(min(d for _, d in deltas), 2)
        row["max_delta"] = round(max(d for _, d in deltas), 2)
        row["avg_delta"] = round(sum(d for _, d in deltas) / len(deltas), 2)
        row["limiting_sensor"] = min(deltas, key=lambda t: t[1])[0]
    else:
        row["min_delta"] = ""
        row["max_delta"] = ""
        row["avg_delta"] = ""
        row["limiting_sensor"] = ""

    return row


def _coerce_legacy_row(raw: dict[str, Any], _time_fired: str = "") -> dict[str, Any]:
    row = {col: raw.get(col, "") for col in LEGACY_COLUMNS if col in (
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


def _load_events_from_sqlite(
    db_path: Path,
    days: int,
    event_type: str,
    coerce: Callable[[dict[str, Any], str], dict[str, Any]],
) -> list[dict[str, Any]]:
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
            (event_type, since_iso),
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
        if event_type == EVENT_LEGACY and not event_data.get("ended_at"):
            event_data["ended_at"] = row["time_fired"]
        events.append(coerce(event_data, row["time_fired"]))
    return events


def _ha_request(url: str, token: str, path: str, params: dict[str, str] | None = None) -> Any:
    query = f"?{urllib.parse.urlencode(params)}" if params else ""
    req = urllib.request.Request(
        f"{url.rstrip('/')}{path}{query}",
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        return json.loads(resp.read().decode())


def _load_events_from_logbook(
    url: str,
    token: str,
    days: int,
    event_type: str,
    coerce: Callable[[dict[str, Any], str], dict[str, Any]],
) -> list[dict[str, Any]]:
    start = datetime.now(timezone.utc) - timedelta(days=days)
    end = datetime.now(timezone.utc)
    params = {"end_time": end.isoformat()}
    entries = _ha_request(url, token, f"/api/logbook/{start.isoformat()}", params)

    events: list[dict[str, Any]] = []
    for entry in entries:
        name = entry.get("name") or entry.get("context_event_type") or ""
        if name != event_type:
            continue
        event_data = entry.get("context_event_data") or {}
        if not event_data:
            continue
        when = entry.get("when", "")
        if event_type == EVENT_LEGACY and not event_data.get("ended_at"):
            event_data["ended_at"] = when
        events.append(coerce(event_data, when))
    return events


def export_timeseries(
    url: str,
    token: str,
    settles: list[dict[str, Any]],
    window_before_min: int,
    window_after_min: int,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for settle in settles:
        recorded_at = settle.get("recorded_at")
        if not recorded_at:
            continue
        try:
            center = datetime.fromisoformat(str(recorded_at).replace("Z", "+00:00"))
        except ValueError:
            continue
        if center.tzinfo is None:
            center = center.replace(tzinfo=timezone.utc)
        range_start = center - timedelta(minutes=window_before_min)
        range_end = center + timedelta(minutes=window_after_min)
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
            print(f"Warning: history fetch failed for {recorded_at}: {exc}", file=sys.stderr)
            continue
        for entity_history in history:
            if not entity_history:
                continue
            entity_id = entity_history[0].get("entity_id", "")
            for point in entity_history:
                rows.append({
                    "recorded_at": recorded_at,
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--days", type=int, default=90, help="Lookback window (default: 90)")
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output CSV path (default: data/irrigation_settles.csv or legacy cycles path)",
    )
    parser.add_argument(
        "--timeseries-out",
        type=Path,
        default=None,
        help="Optional VWC time series CSV (requires --url and --token)",
    )
    parser.add_argument("--db", type=Path, default=None, help="Path to home-assistant_v2.db")
    parser.add_argument("--url", default=os.environ.get("HA_URL", ""))
    parser.add_argument("--token", default=os.environ.get("HA_TOKEN", ""))
    parser.add_argument(
        "--legacy",
        action="store_true",
        help="Export irrigation_cycle_complete (legacy) instead of settle snapshots",
    )
    args = parser.parse_args()

    if args.legacy:
        event_type = EVENT_LEGACY
        columns = LEGACY_COLUMNS
        coerce = _coerce_legacy_row
        default_out = Path("data/irrigation_cycles.csv")
    else:
        event_type = EVENT_SETTLE
        columns = SETTLE_COLUMNS
        coerce = _coerce_settle_row
        default_out = Path("data/irrigation_settles.csv")

    out_path = args.out or default_out

    if args.db:
        rows = _load_events_from_sqlite(args.db, args.days, event_type, coerce)
        print(f"SQLite: found {len(rows)} {event_type} events in last {args.days} days")
    elif args.url and args.token:
        rows = _load_events_from_logbook(args.url, args.token, args.days, event_type, coerce)
        print(f"Logbook API: found {len(rows)} {event_type} events in last {args.days} days")
        if not rows:
            print(
                "No events via logbook. For full event_data, use --db with the recorder SQLite file.",
                file=sys.stderr,
            )
    else:
        print("Provide --db or both --url and --token (HA_URL / HA_TOKEN).", file=sys.stderr)
        return 1

    if not rows:
        print(f"No {event_type} events exported.", file=sys.stderr)
        return 2

    write_csv(out_path, columns, rows)
    print(f"Wrote {len(rows)} rows to {out_path}")

    if args.timeseries_out:
        if not (args.url and args.token):
            print("--timeseries-out requires --url and --token", file=sys.stderr)
            return 1
        ts_rows = export_timeseries(args.url, args.token, rows, 30, 90)
        write_csv(args.timeseries_out, TIMESERIES_COLUMNS, ts_rows)
        print(f"Wrote {len(ts_rows)} time series rows to {args.timeseries_out}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
