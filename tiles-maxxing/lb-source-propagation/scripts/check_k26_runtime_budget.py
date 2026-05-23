#!/usr/bin/env python3
"""Check K26 continuation progress against the paid-run runtime budget."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys
from typing import Any


def positive_int(text: str) -> int:
    try:
        value = int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if value <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return value


def nonnegative_float(text: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if value < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return value


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                row = json.loads(stripped)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if not isinstance(row, dict):
                raise ValueError(f"{path}:{line_no}: JSONL row is not an object")
            rows.append(row)
    return rows


def completed_band_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    seen: set[int] = set()
    for row in rows:
        if row.get("schema") != "lb_source_tileop_port_progress_v1":
            continue
        if row.get("accepted") is not True:
            continue
        band_index = row.get("band_index")
        total_ms = row.get("total_ms")
        if not isinstance(band_index, int) or band_index < 0:
            continue
        if not isinstance(total_ms, int) or total_ms <= 0:
            continue
        if band_index in seen:
            raise ValueError(f"duplicate completed progress band_index={band_index}")
        seen.add(band_index)
        out.append(row)
    out.sort(key=lambda item: item["band_index"])
    return out


def latest_phase(rows: list[dict[str, Any]]) -> dict[str, Any] | None:
    phase_rows = [
        row
        for row in rows
        if row.get("schema") == "lb_source_tileop_port_phase_v1"
        and isinstance(row.get("phase"), str)
        and isinstance(row.get("event"), str)
    ]
    return phase_rows[-1] if phase_rows else None


def count_chunk_rows(path: Path | None) -> int:
    if path is None:
        return 0
    rows = load_jsonl(path)
    count = 0
    for row in rows:
        if row.get("schema") == "lb_source_k26_continuation_chunk_v1":
            count += 1
    return count


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--progress", required=True, type=Path)
    parser.add_argument("--chunk-ledger", type=Path)
    parser.add_argument("--schedule-segment-count", type=positive_int, default=123)
    parser.add_argument("--max-runtime-seconds", type=nonnegative_float, default=14000.0)
    parser.add_argument("--min-completed-bands", type=positive_int, default=1)
    args = parser.parse_args(argv)

    try:
        rows = load_jsonl(args.progress)
        complete = completed_band_rows(rows)
        chunk_count = count_chunk_rows(args.chunk_ledger)
    except (OSError, ValueError) as exc:
        print(f"K26_RUNTIME_BUDGET_REJECT: {exc}", file=sys.stderr)
        return 1

    phase = latest_phase(rows)
    if len(complete) < args.min_completed_bands:
        print(
            json.dumps(
                {
                    "status": "K26_RUNTIME_BUDGET_INSUFFICIENT_PROGRESS",
                    "completed_band_count": len(complete),
                    "required_completed_band_count": args.min_completed_bands,
                    "schedule_segment_count": args.schedule_segment_count,
                    "active_phase": phase,
                    "claim_grade": False,
                },
                separators=(",", ":"),
                sort_keys=True,
            )
        )
        return 2

    elapsed_ms = sum(int(row["total_ms"]) for row in complete)
    mean_ms = elapsed_ms / len(complete)
    projected_seconds = (mean_ms * args.schedule_segment_count) / 1000.0
    observed_seconds = elapsed_ms / 1000.0
    last = complete[-1]
    margin = args.max_runtime_seconds - projected_seconds
    status = (
        "K26_RUNTIME_BUDGET_PASS"
        if projected_seconds <= args.max_runtime_seconds
        else "K26_RUNTIME_BUDGET_REJECT"
    )
    payload = {
        "status": status,
        "proof_status": "RUNTIME_BUDGET_DIAGNOSTIC_NON_CLAIM",
        "claim_label": "SOURCE_ORIGIN_K26",
        "completed_band_count": len(complete),
        "schedule_segment_count": args.schedule_segment_count,
        "observed_seconds": round(observed_seconds, 3),
        "mean_band_seconds": round(mean_ms / 1000.0, 3),
        "projected_total_seconds": math.ceil(projected_seconds),
        "max_runtime_seconds": args.max_runtime_seconds,
        "budget_margin_seconds": math.floor(margin),
        "last_completed_band_index": last["band_index"],
        "last_completed_r_outer": last.get("r_outer"),
        "last_completed_has_source_carry": last.get("has_source_carry"),
        "last_completed_terminal_source_dead": last.get("terminal_source_dead"),
        "chunk_ledger_rows": chunk_count,
        "active_phase": phase,
        "claim_grade": False,
        "non_claim": (
            "runtime projection from completed continuation progress rows; "
            "not a sqrt(26) source/origin result and not a SOURCE_DEAD_CERT"
        ),
    }
    print(json.dumps(payload, separators=(",", ":"), sort_keys=True))
    return 0 if status == "K26_RUNTIME_BUDGET_PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
