#!/usr/bin/env python3
"""Analyze exported irrigation cycles and recommend timing / leak-detection parameters."""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import statistics
import sys
import urllib.error
import urllib.request
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SAFE_BOUNDS = {
    "duration_min": (3, 30),
    "settle_min": (10, 45),
    "leak_check_delay_min": (15, 45),
    "min_vwc_delta": (0.3, 3.0),
}

DEFAULT_MIN_GALLONS = 0.5


def _parse_float(value: Any, default: float = 0.0) -> float:
    if value is None or value == "":
        return default
    if isinstance(value, bool):
        return float(value)
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _parse_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).lower() in ("true", "1", "yes")


def _clamp(name: str, value: float) -> int | float:
    lo, hi = SAFE_BOUNDS[name]
    clipped = max(lo, min(hi, value))
    if name == "min_vwc_delta":
        return round(clipped, 1)
    return int(round(clipped))


def load_cycles(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def _gallons(row: dict[str, str]) -> float:
    actual = _parse_float(row.get("gallons_actual"))
    if actual > 0:
        return actual
    return _parse_float(row.get("gallons_estimated"))


def _s0_delta_60m(row: dict[str, str]) -> float:
    before = _parse_float(row.get("s0_vwc_before"))
    at_60m = _parse_float(row.get("s0_vwc_60m"))
    if at_60m > 0:
        return at_60m - before
    return _parse_float(row.get("s0_delta_check"))


def _median(values: list[float]) -> float | None:
    cleaned = [v for v in values if v > 0 or v < 0]
    if not cleaned:
        return None
    return statistics.median(cleaned)


def group_dry_events(cycles: list[dict[str, str]]) -> list[list[dict[str, str]]]:
    """Group cycles into dry events (sequences ending in target_reached or counter reset)."""
    events: list[list[dict[str, str]]] = []
    current: list[dict[str, str]] = []
    for row in cycles:
        current.append(row)
        if row.get("outcome") == "target_reached":
            events.append(current)
            current = []
    if current:
        events.append(current)
    return events


def compute_s0_lag_min(timeseries_path: Path | None, cycles: list[dict[str, str]]) -> float | None:
    """Minutes from valve close until S0 rises >= 0.5% (requires time series export)."""
    if not timeseries_path or not timeseries_path.is_file():
        return None

    by_cycle: dict[str, list[dict[str, str]]] = defaultdict(list)
    with timeseries_path.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            by_cycle[row.get("cycle_id", "")].append(row)

    lags: list[float] = []
    s0_entity = "sensor.irrigation_sensor_0_vwc_resolved"
    for cycle in cycles:
        cycle_id = cycle.get("cycle_id", "")
        points = by_cycle.get(cycle_id, [])
        if not points:
            continue
        before = _parse_float(cycle.get("s0_vwc_before"))
        threshold = before + 0.5
        end_ts = None
        rise_ts = None
        for point in sorted(points, key=lambda p: p.get("timestamp", "")):
            entity = point.get("entity_id", "")
            if entity.endswith("valve_state_resolved") and point.get("state") == "settling":
                end_ts = point.get("timestamp")
            if entity == s0_entity or entity.endswith("sensor_0_vwc"):
                vwc = _parse_float(point.get("state"))
                if vwc >= threshold and rise_ts is None and end_ts:
                    rise_ts = point.get("timestamp")
        if end_ts and rise_ts:
            try:
                end_dt = datetime.fromisoformat(end_ts.replace("Z", "+00:00"))
                rise_dt = datetime.fromisoformat(rise_ts.replace("Z", "+00:00"))
                if end_dt.tzinfo is None:
                    end_dt = end_dt.replace(tzinfo=timezone.utc)
                if rise_dt.tzinfo is None:
                    rise_dt = rise_dt.replace(tzinfo=timezone.utc)
                lag = (rise_dt - end_dt).total_seconds() / 60
                if lag >= 0:
                    lags.append(lag)
            except ValueError:
                continue
    return _median(lags)


def would_leak_suspect(
    row: dict[str, str],
    leak_delay_min: float,
    min_vwc_delta: float,
    min_gallons: float = DEFAULT_MIN_GALLONS,
) -> bool:
    """Simulate post-settle leak check using check-time deltas (conservative)."""
    gallons = _gallons(row)
    d0 = _parse_float(row.get("s0_delta_check"))
    d1 = _parse_float(row.get("s1_delta_check"))
    # If 60m data shows moisture arrived after check window, credit at 60m for delay sweep.
    if leak_delay_min > _parse_float(row.get("leak_check_delay_min", 20), 20):
        d0_60 = _parse_float(row.get("s0_vwc_60m")) - _parse_float(row.get("s0_vwc_before"))
        d1_60 = _parse_float(row.get("s1_vwc_60m")) - _parse_float(row.get("s1_vwc_before"))
        if d0_60 > d0:
            d0 = d0_60
        if d1_60 > d1:
            d1 = d1_60
    max_delta = max(d0, d1)
    return gallons >= min_gallons and max_delta < min_vwc_delta


def simulate_leak_params(cycles: list[dict[str, str]]) -> dict[str, Any]:
    current_delay = _parse_float(cycles[-1].get("leak_check_delay_min", 20), 20) if cycles else 20
    current_delta = _parse_float(cycles[-1].get("min_vwc_delta", 1.0), 1.0) if cycles else 1.0
    # leak_check_delay_min not in cycle events — use typical default for simulation baseline
    if cycles and "leak_check_delay_min" not in cycles[0]:
        current_delay = 20.0
    if cycles and "min_vwc_delta" not in cycles[0]:
        current_delta = 1.0

    false_pos_at_current = 0
    for row in cycles:
        if row.get("outcome") == "normal" and would_leak_suspect(row, current_delay, current_delta):
            false_pos_at_current += 1

    best: dict[str, Any] | None = None
    real_leaks = {i for i, r in enumerate(cycles) if r.get("outcome") == "leak_suspect"}

    for delay in range(15, 46, 5):
        for delta_10 in range(3, 31):  # 0.3 .. 3.0 step 0.1
            delta = delta_10 / 10
            false_pos = 0
            caught = 0
            for i, row in enumerate(cycles):
                sim = would_leak_suspect(row, delay, delta)
                if row.get("outcome") == "normal" and sim:
                    false_pos += 1
                if i in real_leaks and sim:
                    caught += 1
            if real_leaks and caught < len(real_leaks):
                continue
            if best is None or false_pos < best["false_positives"]:
                best = {
                    "leak_check_delay_min": delay,
                    "min_vwc_delta": round(delta, 1),
                    "false_positives": false_pos,
                }

    if best is None:
        best = {
            "leak_check_delay_min": int(_clamp("leak_check_delay_min", current_delay + 5)),
            "min_vwc_delta": _clamp("min_vwc_delta", current_delta),
            "false_positives": false_pos_at_current,
        }

    false_pos_recommended = best["false_positives"]
    return {
        "false_positives_at_current": false_pos_at_current,
        "at_recommended": false_pos_recommended,
        "recommended_leak_check_delay_min": best["leak_check_delay_min"],
        "recommended_min_vwc_delta": best["min_vwc_delta"],
    }


def recommend_parameters(cycles: list[dict[str, str]], dry_events: list[list[dict[str, str]]]) -> dict[str, Any]:
    if not cycles:
        raise ValueError("No cycles to analyze")

    last = cycles[-1]
    current_duration = int(_parse_float(last.get("duration_min_setting", last.get("duration_min", 10)), 10))
    current_settle = int(_parse_float(last.get("settle_min_setting", 20), 20))

    s0_per_gal_check = [
        _parse_float(r.get("s0_delta_check")) / g
        for r in cycles
        for g in [_gallons(r)]
        if g > 0 and _parse_float(r.get("s0_delta_check")) != 0
    ]
    s0_per_gal_60m = [
        _s0_delta_60m(r) / g
        for r in cycles
        for g in [_gallons(r)]
        if g > 0 and _s0_delta_60m(r) != 0
    ]

    median_per_gal_60m = _median(s0_per_gal_60m) or _median(s0_per_gal_check) or 0.4
    median_per_gal_check = _median(s0_per_gal_check) or median_per_gal_60m

    # Duration: if dry events need many cycles or S0 gains slowly per cycle, increase on-time.
    duration_rec = current_duration
    incomplete = [ev for ev in dry_events if ev[-1].get("outcome") != "target_reached"]
    if incomplete:
        duration_rec = min(30, current_duration + 2)
    if median_per_gal_check < 0.35 and current_duration < 30:
        duration_rec = min(30, current_duration + 2)

    # Saturation: near sensor (S1) hitting max while S0 dry
    s1_saturation = sum(
        1
        for r in cycles
        if _parse_bool(r.get("early_stop"))
        or _parse_float(r.get("s1_vwc_end")) >= _parse_float(r.get("max_vwc_1", 50), 50) - 1
    )
    if s1_saturation >= max(1, len(cycles) // 4) and duration_rec > 3:
        duration_rec = max(3, duration_rec - 2)

    # Settle: if S0 still rising between check and 60m, allow more settle time.
    settle_rec = current_settle
    rising_late = 0
    for r in cycles:
        d_check = _parse_float(r.get("s0_delta_check"))
        d_60 = _s0_delta_60m(r)
        if d_60 - d_check >= 1.0:
            rising_late += 1
    if rising_late >= max(1, len(cycles) // 3):
        settle_rec = min(45, current_settle + 5)

    leak_sim = simulate_leak_params(cycles)

    rationale: list[str] = []
    rationale.append(
        f"Analyzed {len(cycles)} cycles; S0 median VWC gain per gallon at 60m: {median_per_gal_60m:.2f}%/gal."
    )
    if incomplete:
        rationale.append(
            f"{len(incomplete)} dry event(s) did not reach target — consider longer on-time or more cycles."
        )
    if s1_saturation:
        rationale.append(
            f"{s1_saturation} cycle(s) show near-sensor (S1) saturation risk — shorter pulses may help."
        )
    if leak_sim["false_positives_at_current"]:
        rationale.append(
            f"Leak simulation: {leak_sim['false_positives_at_current']} false positive(s) at current settings; "
            f"{leak_sim['at_recommended']} at recommended leak params."
        )

    # Predict gallons to bring S0 to target from typical dry start
    target_0 = _parse_float(last.get("target_vwc_0"), 44)
    typical_before = _median([_parse_float(r.get("s0_vwc_before")) for r in cycles]) or 32
    gallons_needed = (target_0 - typical_before) / median_per_gal_60m if median_per_gal_60m > 0 else 0
    gallons_per_cycle = current_duration * _parse_float(last.get("system_gph", 12), 12) / 60
    cycles_needed = math.ceil(gallons_needed / gallons_per_cycle) if gallons_per_cycle > 0 else 0

    if cycles_needed > 4:
        rationale.append(
            f"Estimated {cycles_needed} cycles to reach S0 target from {typical_before:.1f}% "
            f"(>{4} max) — consider raising duration_min or max_cycles_per_event."
        )

    return {
        "duration_min": int(_clamp("duration_min", duration_rec)),
        "settle_min": int(_clamp("settle_min", settle_rec)),
        "leak_check_delay_min": int(_clamp("leak_check_delay_min", leak_sim["recommended_leak_check_delay_min"])),
        "min_vwc_delta": _clamp("min_vwc_delta", leak_sim["recommended_min_vwc_delta"]),
        "rationale": rationale,
        "median_per_gal_60m": round(median_per_gal_60m, 3),
        "median_per_gal_check": round(median_per_gal_check, 3),
        "gallons_needed_s0_estimate": round(gallons_needed, 2),
        "cycles_needed_estimate": cycles_needed,
        "leak_simulation": leak_sim,
    }


def build_report(
    cycles: list[dict[str, str]],
    timeseries_path: Path | None,
) -> dict[str, Any]:
    dry_events = group_dry_events(cycles)
    rec = recommend_parameters(cycles, dry_events)
    s0_lag = compute_s0_lag_min(timeseries_path, cycles)

    dry_event_summaries = []
    for i, ev in enumerate(dry_events, start=1):
        total_gal = sum(_gallons(r) for r in ev)
        dry_event_summaries.append({
            "event": i,
            "cycles": len(ev),
            "total_gallons": round(total_gal, 2),
            "reached_target": ev[-1].get("outcome") == "target_reached",
            "s1_early_stops": sum(1 for r in ev if _parse_bool(r.get("early_stop"))),
        })

    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "cycles_analyzed": len(cycles),
        "s0_median_vwc_per_gal_60m": rec["median_per_gal_60m"],
        "s0_median_vwc_per_gal_check": rec["median_per_gal_check"],
        "s0_median_lag_min": s0_lag,
        "dry_events": dry_event_summaries,
        "recommended": {
            "duration_min": rec["duration_min"],
            "settle_min": rec["settle_min"],
            "leak_check_delay_min": rec["leak_check_delay_min"],
            "min_vwc_delta": rec["min_vwc_delta"],
        },
        "prediction": {
            "gallons_needed_s0": rec["gallons_needed_s0_estimate"],
            "cycles_needed": rec["cycles_needed_estimate"],
        },
        "rationale": rec["rationale"],
        "leak_simulation": rec["leak_simulation"],
    }


def format_summary(report: dict[str, Any]) -> str:
    rec = report["recommended"]
    lines = [
        f"{report['cycles_analyzed']} cycles analyzed.",
        f"S0: {report['s0_median_vwc_per_gal_60m']}%/gal @60m.",
        f"Rec: {rec['duration_min']}m on, {rec['settle_min']}m settle.",
        f"Leak: {rec['leak_check_delay_min']}m delay, {rec['min_vwc_delta']}% min delta.",
    ]
    if report.get("s0_median_lag_min") is not None:
        lines.append(f"S0 lag ~{report['s0_median_lag_min']:.0f}m.")
    return " ".join(lines)[:255]


def _ha_set_state(url: str, token: str, entity_id: str, state: str | float) -> None:
    payload = json.dumps({"entity_id": entity_id, "state": state}).encode()
    req = urllib.request.Request(
        f"{url.rstrip('/')}/api/states/{entity_id}",
        data=payload,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=30):
        pass


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


def apply_to_ha(url: str, token: str, report: dict[str, Any]) -> None:
    rec = report["recommended"]
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    _ha_call_service(url, token, "input_datetime", "set_datetime", {
        "entity_id": "input_datetime.irrigation_analysis_last_run",
        "datetime": now,
    })
    _ha_call_service(url, token, "input_text", "set_value", {
        "entity_id": "input_text.irrigation_analysis_summary",
        "value": format_summary(report),
    })
    for entity_suffix, key in (
        ("duration_min", "duration_min"),
        ("settle_min", "settle_min"),
        ("leak_check_delay_min", "leak_check_delay_min"),
        ("min_vwc_delta", "min_vwc_delta"),
    ):
        _ha_call_service(url, token, "input_number", "set_value", {
            "entity_id": f"input_number.irrigation_analysis_rec_{entity_suffix}",
            "value": rec[key],
        })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cycles",
        type=Path,
        default=Path("data/irrigation_cycles.csv"),
        help="Cycle CSV from export_irrigation_cycles.py",
    )
    parser.add_argument(
        "--timeseries",
        type=Path,
        default=None,
        help="Optional vwc_timeseries.csv for lag analysis",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("data/analysis_report.json"),
        help="Output JSON report path",
    )
    parser.add_argument("--apply-to-ha", action="store_true", help="Push summary to HA helpers")
    parser.add_argument("--url", default=os.environ.get("HA_URL", ""))
    parser.add_argument("--token", default=os.environ.get("HA_TOKEN", ""))
    parser.add_argument(
        "--min-cycles",
        type=int,
        default=3,
        help="Minimum cycles required (default: 3)",
    )
    args = parser.parse_args()

    if not args.cycles.is_file():
        print(f"Cycle file not found: {args.cycles}", file=sys.stderr)
        print("Run scripts/export_irrigation_cycles.py first.", file=sys.stderr)
        return 1

    cycles = load_cycles(args.cycles)
    if len(cycles) < args.min_cycles:
        print(
            f"Insufficient data: {len(cycles)} cycles (need >= {args.min_cycles}).",
            file=sys.stderr,
        )
        return 2

    report = build_report(cycles, args.timeseries)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)
    print(f"Wrote analysis report to {args.out}")
    print(format_summary(report))

    if args.apply_to_ha:
        if not (args.url and args.token):
            print("--apply-to-ha requires --url and --token", file=sys.stderr)
            return 1
        try:
            apply_to_ha(args.url, args.token, report)
            print("Pushed recommendations to Home Assistant analysis helpers.")
        except urllib.error.URLError as exc:
            print(f"Failed to push to HA: {exc}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
