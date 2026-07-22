#!/usr/bin/env python3
"""Analyze exported irrigation settle snapshots or legacy cycle CSVs."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SAFE_BOUNDS = {
    "duration_min": (3, 360),
    "settle_min": (10, 45),
    "leak_check_delay_min": (15, 45),
    "min_vwc_delta": (0.3, 3.0),
    "min_pulse_min": (1, 60),
    "max_pulse_min": (1, 360),
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


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def is_settle_format(rows: list[dict[str, str]]) -> bool:
    if not rows:
        return False
    headers = set(rows[0].keys())
    return "recorded_at" in headers and "actual_pulse_s" in headers


def _median(values: list[float]) -> float | None:
    cleaned = [v for v in values if v != 0 or v == 0]
    if not cleaned:
        return None
    return statistics.median(cleaned)


def _sensor_ids_from_rows(rows: list[dict[str, str]]) -> list[int]:
    ids: list[int] = []
    if not rows:
        return ids
    for i in range(6):
        if f"s{i}_delta" in rows[0]:
            ids.append(i)
    return ids


def build_settle_report(rows: list[dict[str, str]]) -> dict[str, Any]:
    sensor_ids = _sensor_ids_from_rows(rows)
    actual_pulses = [_parse_float(r.get("actual_pulse_s")) for r in rows]
    requested_pulses = [_parse_float(r.get("requested_pulse_s")) for r in rows]
    shortfalls = [_parse_float(r.get("pulse_shortfall_s")) for r in rows]
    avg_deltas = [_parse_float(r.get("avg_delta")) for r in rows if r.get("avg_delta") not in (None, "")]
    early_stops = sum(1 for r in rows if _parse_float(r.get("pulse_shortfall_s")) > 60)

    per_sensor: dict[str, Any] = {}
    for sid in sensor_ids:
        deltas = [_parse_float(r.get(f"s{sid}_delta")) for r in rows if r.get(f"s{sid}_delta") not in (None, "")]
        rates: list[float] = []
        for r in rows:
            delta = _parse_float(r.get(f"s{sid}_delta"))
            pulse_min = _parse_float(r.get("actual_pulse_s")) / 60.0
            if pulse_min > 0 and r.get(f"s{sid}_delta") not in (None, ""):
                rates.append(delta / pulse_min)
        per_sensor[f"s{sid}"] = {
            "median_delta": round(_median(deltas) or 0, 2),
            "median_delta_per_min": round(_median(rates) or 0, 3),
            "samples": len(deltas),
        }

    median_actual_min = (_median(actual_pulses) or 0) / 60.0
    median_rate = _median([v["median_delta_per_min"] for v in per_sensor.values() if v["samples"]]) or 0

    rationale: list[str] = []
    rationale.append(
        f"Analyzed {len(rows)} settle snapshot(s); median pulse {median_actual_min:.1f} min."
    )
    if early_stops:
        rationale.append(
            f"{early_stops} pulse(s) ended early (actual << requested) — likely stop-on-max or manual close."
        )
    if median_rate > 0:
        rationale.append(f"Median moisture gain ~{median_rate:.2f}% VWC per pulse-minute across sensors.")
    if median_rate > 0 and median_rate < 0.15:
        rationale.append("Low VWC gain per minute — consider longer max pulse or check emitter flow.")
    if median_rate > 0.5:
        rationale.append("High VWC gain per minute — pulses may be longer than needed; review max pulse slider.")

    limiting_counts: dict[str, int] = defaultdict(int)
    for r in rows:
        lim = r.get("limiting_sensor")
        if lim not in (None, ""):
            limiting_counts[str(lim)] += 1

    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "format": "settle_snapshot",
        "snapshots_analyzed": len(rows),
        "median_actual_pulse_min": round(median_actual_min, 1),
        "median_requested_pulse_min": round((_median(requested_pulses) or 0) / 60.0, 1),
        "median_avg_delta": round(_median(avg_deltas) or 0, 2),
        "early_stop_count": early_stops,
        "median_shortfall_s": round(_median(shortfalls) or 0, 0),
        "per_sensor": per_sensor,
        "limiting_sensor_counts": dict(limiting_counts),
        "rationale": rationale,
        "notes": [
            "Review input_number.irrigation_min/target/max_vwc sliders in HA based on observed deltas.",
            "Compare requested_pulse_s vs actual_pulse_s to tune max pulse and stop-on-max behavior.",
        ],
    }


def format_settle_summary(report: dict[str, Any]) -> str:
    lines = [
        f"{report['snapshots_analyzed']} settle snapshots.",
        f"Median pulse {report['median_actual_pulse_min']:.0f}m",
        f"avg Δ {report['median_avg_delta']:.1f}%.",
    ]
    if report.get("early_stop_count"):
        lines.append(f"{report['early_stop_count']} early stop(s).")
    return " ".join(lines)[:255]


# ── Legacy cycle analysis (irrigation_cycle_complete CSV) ─────────────────────

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


def group_dry_events(cycles: list[dict[str, str]]) -> list[list[dict[str, str]]]:
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


def would_leak_suspect(
    row: dict[str, str],
    leak_delay_min: float,
    min_vwc_delta: float,
    min_gallons: float = DEFAULT_MIN_GALLONS,
) -> bool:
    gallons = _gallons(row)
    d0 = _parse_float(row.get("s0_delta_check"))
    d1 = _parse_float(row.get("s1_delta_check"))
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
    current_delay = 20.0
    current_delta = 1.0
    false_pos_at_current = sum(
        1 for row in cycles
        if row.get("outcome") == "normal" and would_leak_suspect(row, current_delay, current_delta)
    )
    best: dict[str, Any] | None = None
    real_leaks = {i for i, r in enumerate(cycles) if r.get("outcome") == "leak_suspect"}

    for delay in range(15, 46, 5):
        for delta_10 in range(3, 31):
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
            "leak_check_delay_min": 20,
            "min_vwc_delta": 1.0,
            "false_positives": false_pos_at_current,
        }

    return {
        "false_positives_at_current": false_pos_at_current,
        "at_recommended": best["false_positives"],
        "recommended_leak_check_delay_min": best["leak_check_delay_min"],
        "recommended_min_vwc_delta": best["min_vwc_delta"],
    }


def recommend_legacy_parameters(
    cycles: list[dict[str, str]],
    dry_events: list[list[dict[str, str]]],
) -> dict[str, Any]:
    last = cycles[-1]
    current_duration = int(_parse_float(last.get("duration_min_setting", last.get("duration_min", 10)), 10))
    current_settle = int(_parse_float(last.get("settle_min_setting", 20), 20))

    s0_per_gal_60m = [
        _s0_delta_60m(r) / g
        for r in cycles
        for g in [_gallons(r)]
        if g > 0 and _s0_delta_60m(r) != 0
    ]
    median_per_gal_60m = _median(s0_per_gal_60m) or 0.4

    duration_rec = current_duration
    incomplete = [ev for ev in dry_events if ev[-1].get("outcome") != "target_reached"]
    if incomplete:
        duration_rec = min(30, current_duration + 2)

    settle_rec = current_settle
    leak_sim = simulate_leak_params(cycles)

    rationale = [
        f"Analyzed {len(cycles)} legacy cycles; S0 median {median_per_gal_60m:.2f}%/gal @60m.",
    ]

    return {
        "duration_min": int(_clamp("duration_min", duration_rec)),
        "settle_min": int(_clamp("settle_min", settle_rec)),
        "leak_check_delay_min": int(_clamp("leak_check_delay_min", leak_sim["recommended_leak_check_delay_min"])),
        "min_vwc_delta": _clamp("min_vwc_delta", leak_sim["recommended_min_vwc_delta"]),
        "rationale": rationale,
        "median_per_gal_60m": round(median_per_gal_60m, 3),
        "leak_simulation": leak_sim,
    }


def build_legacy_report(cycles: list[dict[str, str]]) -> dict[str, Any]:
    dry_events = group_dry_events(cycles)
    rec = recommend_legacy_parameters(cycles, dry_events)
    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "format": "legacy_cycle",
        "cycles_analyzed": len(cycles),
        "recommended": {
            "duration_min": rec["duration_min"],
            "settle_min": rec["settle_min"],
            "leak_check_delay_min": rec["leak_check_delay_min"],
            "min_vwc_delta": rec["min_vwc_delta"],
        },
        "rationale": rec["rationale"],
        "leak_simulation": rec["leak_simulation"],
    }


def format_legacy_summary(report: dict[str, Any]) -> str:
    rec = report["recommended"]
    return (
        f"{report['cycles_analyzed']} legacy cycles. "
        f"Rec: {rec['duration_min']}m on, {rec['settle_min']}m settle."
    )[:255]


def build_report(rows: list[dict[str, str]], _timeseries_path: Path | None) -> dict[str, Any]:
    if is_settle_format(rows):
        return build_settle_report(rows)
    return build_legacy_report(rows)


def format_summary(report: dict[str, Any]) -> str:
    if report.get("format") == "settle_snapshot":
        return format_settle_summary(report)
    return format_legacy_summary(report)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        "--cycles",
        dest="input_path",
        type=Path,
        default=Path("data/irrigation_settles.csv"),
        help="CSV from export_irrigation_cycles.py (settle or legacy)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("data/analysis_report.json"),
        help="Output JSON report path",
    )
    parser.add_argument(
        "--min-rows",
        "--min-cycles",
        dest="min_rows",
        type=int,
        default=3,
        help="Minimum rows required (default: 3)",
    )
    args = parser.parse_args()

    if not args.input_path.is_file():
        print(f"Input file not found: {args.input_path}", file=sys.stderr)
        print("Run: python scripts/export_irrigation_cycles.py --db /path/to/home-assistant_v2.db", file=sys.stderr)
        return 1

    rows = load_rows(args.input_path)
    if len(rows) < args.min_rows:
        print(
            f"Insufficient data: {len(rows)} rows (need >= {args.min_rows}). "
            "Collect ~1 week of auto irrigation first.",
            file=sys.stderr,
        )
        return 2

    report = build_report(rows, None)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)
    print(f"Wrote analysis report to {args.out}")
    print(format_summary(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
