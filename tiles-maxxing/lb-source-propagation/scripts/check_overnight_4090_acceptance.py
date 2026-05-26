#!/usr/bin/env python3
"""Check the local artifact pack from a non-claim overnight 4090 campaign."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


FINAL_STATUS = "LB_OVERNIGHT_4090_FINISHED_DIAGNOSTIC_NON_CLAIM"
NON_CLAIM = "DIAGNOSTIC_NON_CLAIM"

REQUIRED_ROWS = {
    "cmake-sidecar-configure",
    "cmake-sidecar-build",
    "cmake-verification-configure",
    "cmake-verification-build",
    "sidecar-ctest",
    "verification-ctest",
    "wide-equivalence",
    "k26-budget-probe",
    "high-radius-r1m",
    "r60-w128",
    "r400-w16",
    "k26-long-run",
}


def read_text(path: Path) -> str:
    return path.read_text(errors="replace")


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.exists():
        return rows
    for line_no, line in enumerate(read_text(path).splitlines(), start=1):
        if not line.strip():
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError as exc:
            rows.append({
                "_parse_error": f"{path}:{line_no}: {exc}",
                "name": f"parse-error-{line_no}",
            })
    return rows


def check_ledger(remote: Path, issues: list[str], warnings: list[str]) -> dict[str, Any]:
    ledger = remote / "artifact-ledger.sha256"
    result: dict[str, Any] = {
        "path": str(ledger),
        "exists": ledger.exists(),
        "checked": 0,
        "missing": 0,
        "mismatched": 0,
    }
    if not ledger.exists():
        issues.append("missing remote/artifact-ledger.sha256")
        return result

    for line_no, line in enumerate(read_text(ledger).splitlines(), start=1):
        if not line.strip():
            continue
        try:
            expected, rel = line.split(None, 1)
        except ValueError:
            issues.append(f"bad artifact-ledger line {line_no}")
            continue
        rel = rel.strip()
        if rel.startswith("*"):
            rel = rel[1:]
        if rel.startswith("./"):
            rel = rel[2:]
        path = remote / rel
        if not path.exists():
            result["missing"] += 1
            issues.append(f"ledger entry missing locally: {rel}")
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        result["checked"] += 1
        if actual != expected and rel in {"logs/campaign.log", "logs/tmux-stdout.log"}:
            result["mismatched"] += 1
            warnings.append(f"remote ledger has expected post-hash mutable log mismatch: {rel}")
        elif actual != expected:
            result["mismatched"] += 1
            issues.append(f"ledger hash mismatch: {rel}")
    return result


def check_local_supervisor_ledger(root: Path, issues: list[str]) -> dict[str, Any]:
    ledger = root / "local-supervisor-ledger.sha256"
    result: dict[str, Any] = {
        "path": str(ledger),
        "exists": ledger.exists(),
        "checked": 0,
        "missing": 0,
        "mismatched": 0,
    }
    if not ledger.exists():
        issues.append("missing local-supervisor-ledger.sha256")
        return result

    for line_no, line in enumerate(read_text(ledger).splitlines(), start=1):
        if not line.strip():
            continue
        try:
            expected, rel = line.split(None, 1)
        except ValueError:
            issues.append(f"bad local supervisor ledger line {line_no}")
            continue
        rel = rel.strip()
        if rel.startswith("*"):
            rel = rel[1:]
        if rel.startswith("./"):
            rel = rel[2:]
        path = root / rel
        if not path.exists():
            result["missing"] += 1
            issues.append(f"local supervisor ledger entry missing: {rel}")
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        result["checked"] += 1
        if actual != expected:
            result["mismatched"] += 1
            issues.append(f"local supervisor ledger hash mismatch: {rel}")
    return result


def row_map(rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    mapped: dict[str, dict[str, Any]] = {}
    for row in rows:
        name = str(row.get("name", ""))
        if name:
            mapped[name] = row
    return mapped


def require_file(path: Path, issues: list[str], label: str) -> bool:
    if path.exists():
        return True
    issues.append(f"missing {label}: {path}")
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact_dir", type=Path)
    parser.add_argument(
        "--allow-running",
        action="store_true",
        help="report partial status without failing on unfinished campaign rows",
    )
    parser.add_argument("--write-json", type=Path)
    parser.add_argument("--write-md", type=Path)
    args = parser.parse_args()

    root = args.artifact_dir
    remote = root / "remote"
    issues: list[str] = []
    warnings: list[str] = []

    require_file(remote / "remote-environment.txt", issues, "remote environment")
    require_file(remote / "vast-instance.json", issues, "remote Vast metadata")
    require_file(root / "vast-instance-started.json", issues, "local Vast start metadata")
    require_file(remote / "campaign-rows.jsonl", issues, "campaign rows")

    status = read_text(remote / "status.txt").strip() if (remote / "status.txt").exists() else ""
    if args.allow_running:
        if not status:
            issues.append("missing remote/status.txt")
    elif status != FINAL_STATUS:
        issues.append(f"final status is {status!r}, expected {FINAL_STATUS!r}")

    env_text = read_text(remote / "remote-environment.txt") if (remote / "remote-environment.txt").exists() else ""
    if f"proof_status={NON_CLAIM}" not in env_text:
        issues.append("remote environment does not declare diagnostic non-claim proof status")
    if "remote_kind=remote_cpu_sidecar_and_optional_cuda_kernel" not in env_text:
        issues.append("remote environment does not bind CPU sidecar vs optional CUDA kind")

    summary_path = remote / "campaign-summary.json"
    summary: dict[str, Any] = {}
    if summary_path.exists():
        summary = json.loads(read_text(summary_path))
        if summary.get("proof_status") != NON_CLAIM:
            issues.append("campaign summary proof_status is not DIAGNOSTIC_NON_CLAIM")
    elif not args.allow_running:
        issues.append("missing remote/campaign-summary.json")

    rows = load_jsonl(remote / "campaign-rows.jsonl")
    rows_by_name = row_map(rows)
    if any("_parse_error" in row for row in rows):
        issues.extend(str(row["_parse_error"]) for row in rows if "_parse_error" in row)

    missing_rows = sorted(REQUIRED_ROWS - set(rows_by_name))
    if missing_rows and not args.allow_running:
        issues.append("missing required rows: " + ", ".join(missing_rows))

    required_exit_failures = []
    for name, row in rows_by_name.items():
        if row.get("proof_status") != NON_CLAIM:
            issues.append(f"row {name} proof_status is not {NON_CLAIM}")
        if row.get("required") is True and str(row.get("exit_code")) != "0":
            required_exit_failures.append((name, str(row.get("exit_code"))))
    if required_exit_failures:
        details = ", ".join(f"{name}={code}" for name, code in required_exit_failures)
        warnings.append(f"required rows with nonzero exit_code: {details}")

    for name in ("r60-w128", "r400-w16"):
        row = rows_by_name.get(name)
        if row is None:
            if not args.allow_running:
                issues.append(f"missing {name} high-radius row")
            continue
        row_dir = remote / str(row.get("dir", ""))
        require_file(row_dir / "time.txt", issues, f"{name} time.txt")
        require_file(row_dir / "progress.jsonl", issues, f"{name} progress")
        exit_code = str(row.get("exit_code"))
        if exit_code != "0":
            require_file(row_dir / "stderr.log", issues, f"{name} timeout/block stderr")

    budget_dir = remote / "k26-baseline" / "budget-probe" / "artifacts"
    require_file(budget_dir / "k26-continuation-progress.jsonl", issues, "K26 budget progress JSONL")
    require_file(budget_dir / "k26-runtime-budget-check.manual.log", issues, "manual K26 runtime budget log")
    require_file(budget_dir / "k26-runtime-budget-check.manual.meta", issues, "manual K26 runtime budget meta")
    if (budget_dir / "remote-k26-timing-probe-status.txt").exists():
        if "REMOTE_K26_TIMING_PROBE_PASS" not in read_text(budget_dir / "remote-k26-timing-probe-status.txt"):
            issues.append("K26 timing probe status file does not report pass")
    else:
        issues.append("missing remote K26 timing probe status")

    cuda_block = remote / "cuda-profile" / "BLOCKED.txt"
    cuda_rows = [row for row in rows if row.get("remote_kind") == "cuda_kernel"]
    if not cuda_block.exists() and not cuda_rows and not args.allow_running:
        issues.append("missing CUDA profile row or explicit cuda-profile/BLOCKED.txt")

    cleanup = root / "cleanup-status.txt"
    if args.allow_running:
        if cleanup.exists():
            warnings.append("cleanup-status.txt already exists while allow-running was used")
    else:
        require_file(cleanup, issues, "cleanup status")

    ledger_result = check_ledger(remote, issues, warnings) if (remote / "artifact-ledger.sha256").exists() else {
        "exists": False,
        "checked": 0,
        "missing": 0,
        "mismatched": 0,
    }
    if not ledger_result["exists"] and not args.allow_running:
        issues.append("missing remote/artifact-ledger.sha256")

    local_ledger_result = {
        "exists": False,
        "checked": 0,
        "missing": 0,
        "mismatched": 0,
    }
    if not args.allow_running:
        local_ledger_result = check_local_supervisor_ledger(root, issues)

    result = {
        "schema": "lb_overnight_4090_acceptance_check_v1",
        "artifact_dir": str(root),
        "status": status or "MISSING",
        "final_status_expected": FINAL_STATUS,
        "allow_running": args.allow_running,
        "ok": not issues,
        "issues": issues,
        "warnings": warnings,
        "rows_seen": sorted(rows_by_name),
        "missing_rows": missing_rows,
        "ledger": ledger_result,
        "local_supervisor_ledger": local_ledger_result,
        "proof_status": NON_CLAIM,
    }

    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.write_json:
        args.write_json.parent.mkdir(parents=True, exist_ok=True)
        args.write_json.write_text(text)
    print(text, end="")

    if args.write_md:
        lines = [
            "# Overnight 4090 Acceptance Check",
            "",
            f"Status: `{result['status']}`",
            f"OK: `{result['ok']}`",
            f"Proof status: `{NON_CLAIM}`",
            "",
            "## Rows Seen",
            "",
        ]
        lines.extend(f"- `{name}`" for name in result["rows_seen"])
        if issues:
            lines.extend(["", "## Issues", ""])
            lines.extend(f"- {issue}" for issue in issues)
        if warnings:
            lines.extend(["", "## Warnings", ""])
            lines.extend(f"- {warning}" for warning in warnings)
        args.write_md.parent.mkdir(parents=True, exist_ok=True)
        args.write_md.write_text("\n".join(lines) + "\n")

    return 0 if not issues else 1


if __name__ == "__main__":
    raise SystemExit(main())
