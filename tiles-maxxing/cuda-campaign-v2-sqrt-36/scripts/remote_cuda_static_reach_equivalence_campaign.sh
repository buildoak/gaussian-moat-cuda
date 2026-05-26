#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  remote_cuda_static_reach_equivalence_campaign.sh [--repo DIR]
      [--out-dir DIR] [--build-dir DIR] [--timeout-seconds N]

Build and run CUDA static-reach stitching equivalence gates on a remote CUDA
host. Outputs are diagnostic/non-claim.
USAGE
}

repo_dir="$(pwd)"
out_dir="/workspace/cuda-static-reach-equivalence"
build_dir="/tmp/gm-cuda-static-reach-equivalence"
timeout_seconds="5400"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      repo_dir="$2"
      shift 2
      ;;
    --out-dir)
      out_dir="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --timeout-seconds)
      timeout_seconds="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! [[ "$timeout_seconds" =~ ^[0-9]+$ ]] || [[ "$timeout_seconds" == "0" ]]; then
  echo "--timeout-seconds must be positive" >&2
  exit 2
fi

repo_dir="$(cd "$repo_dir" && pwd)"
cuda_dir="$repo_dir/tiles-maxxing/cuda-campaign-v2-sqrt-36"
mkdir -p "$out_dir"/{logs,cases}
: > "$out_dir/cases.jsonl"

log() {
  printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" \
    | tee -a "$out_dir/logs/campaign.log"
}

write_environment() {
  {
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "hostname=$(hostname)"
    echo "repo=$repo_dir"
    echo "cuda_dir=$cuda_dir"
    echo "build_dir=$build_dir"
    echo "branch=$(git -C "$repo_dir" branch --show-current 2>/dev/null || echo unknown)"
    echo "commit=$(git -C "$repo_dir" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "timeout_seconds=$timeout_seconds"
    echo "proof_status=DIAGNOSTIC_NON_CLAIM"
    echo "time_bin=$(command -v time || true)"
    echo "timeout_bin=$(command -v timeout || true)"
    if command -v nvidia-smi >/dev/null 2>&1; then
      echo "nvidia_smi_begin"
      nvidia-smi || true
      echo "nvidia_smi_end"
    else
      echo "nvidia_smi=unavailable"
    fi
  } > "$out_dir/environment.txt"
}

run_logged() {
  local label="$1"
  shift
  log "RUN ${label}: $*"
  set +e
  "$@" > "$out_dir/logs/${label}.stdout.log" \
    2> "$out_dir/logs/${label}.stderr.log"
  local rc="$?"
  set -e
  echo "$rc" > "$out_dir/logs/${label}.exit_code.txt"
  log "END ${label} exit=${rc}"
  return "$rc"
}

run_case() {
  local label="$1"
  local r_start="$2"
  local r_final="$3"
  local width="$4"
  local stdout="$out_dir/cases/${label}.stdout.json"
  local stderr="$out_dir/cases/${label}.stderr.log"
  local time_file="$out_dir/cases/${label}.time.txt"
  local exit_file="$out_dir/cases/${label}.exit_code.txt"
  local begin_epoch end_epoch rc timeout_bin time_bin
  local cmd=(
    "$build_dir/cuda_static_reach_equivalence"
    "--r-start=$r_start"
    "--r-final=$r_final"
    "--microband-width=$width"
    --chunk-size=200000
    --max-atoms=1000000000
  )

  log "CASE ${label} r_start=${r_start} r_final=${r_final} microband_width=${width}"
  printf '%q ' "${cmd[@]}" > "$out_dir/cases/${label}.argv.txt"
  printf '\n' >> "$out_dir/cases/${label}.argv.txt"
  begin_epoch="$(date +%s)"
  timeout_bin="$(command -v timeout || true)"
  time_bin="$(command -v time || true)"
  : > "$time_file"
  set +e
  if [[ -n "$time_bin" && "$time_bin" == /* && -n "$timeout_bin" ]]; then
    "$time_bin" -v -o "$time_file" "$timeout_bin" "$timeout_seconds" "${cmd[@]}" \
      > "$stdout" 2> "$stderr"
  elif [[ -n "$timeout_bin" ]]; then
    "$timeout_bin" "$timeout_seconds" "${cmd[@]}" > "$stdout" 2> "$stderr"
  elif [[ -n "$time_bin" && "$time_bin" == /* ]]; then
    "$time_bin" -v -o "$time_file" "${cmd[@]}" > "$stdout" 2> "$stderr"
  else
    "${cmd[@]}" > "$stdout" 2> "$stderr"
  fi
  rc="$?"
  end_epoch="$(date +%s)"
  set -e
  {
    echo "begin_epoch=$begin_epoch"
    echo "end_epoch=$end_epoch"
    echo "wall_seconds=$((end_epoch - begin_epoch))"
  } >> "$time_file"
  echo "$rc" > "$exit_file"
  printf '{"label":"%s","r_start":%s,"r_final":%s,"microband_width":%s,"exit_code":%s,"stdout":"cases/%s.stdout.json","stderr":"cases/%s.stderr.log","time":"cases/%s.time.txt"}\n' \
    "$label" "$r_start" "$r_final" "$width" "$rc" "$label" "$label" "$label" \
    >> "$out_dir/cases.jsonl"
  log "END_CASE ${label} exit=${rc} wall_seconds=$((end_epoch - begin_epoch))"
}

summarize() {
  python3 - "$out_dir" <<'PY'
import json
import pathlib
import sys

out = pathlib.Path(sys.argv[1])
cases = []
for line in (out / "cases.jsonl").read_text().splitlines():
    if not line.strip():
        continue
    row = json.loads(line)
    stdout = out / row["stdout"]
    payload = None
    if stdout.exists() and stdout.read_text().strip():
        try:
            payload = json.loads(stdout.read_text())
        except Exception as exc:
            payload = {"parse_error": str(exc)}
    row["payload"] = payload
    cases.append(row)

status = "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_PASS"
if any(row["exit_code"] != 0 for row in cases):
    status = "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_FAILED"
elif any((row.get("payload") or {}).get("status") != "CUDA_STATIC_REACH_EQUIVALENCE_PASS"
         for row in cases):
    status = "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_MISMATCH"

summary = {
    "schema": "cuda_static_reach_equivalence_campaign_summary_v1",
    "proof_status": "DIAGNOSTIC_NON_CLAIM",
    "status": status,
    "cases": cases,
}
(out / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
lines = [
    "# CUDA Static-Reach Equivalence Campaign",
    "",
    f"Status: `{status}`",
    "",
    "Proof status: `DIAGNOSTIC_NON_CLAIM`",
    "",
]
for row in cases:
    payload = row.get("payload") or {}
    lines.append(
        f"- `{row['label']}`: exit `{row['exit_code']}`, status "
        f"`{payload.get('status')}`, full `{payload.get('full_spanning')}`, "
        f"stitched `{payload.get('stitched_spanning')}`, equivalent "
        f"`{payload.get('equivalent')}`, total_ms `{(payload.get('timings_ms') or {}).get('total')}`"
    )
(out / "summary.md").write_text("\n".join(lines) + "\n")
print(status)
PY
}

echo "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_RUNNING" > "$out_dir/status.txt"
write_environment
run_logged cmake-configure cmake -S "$cuda_dir" -B "$build_dir" \
  -DK_SQ=36 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=89
run_logged cmake-build cmake --build "$build_dir" \
  --target cuda_static_reach_equivalence -j"$(nproc 2>/dev/null || echo 8)"

run_case smoke_r5000_w1024 5000 10000 1024
run_case r60000000_w32768_m1024 60000000 60032768 1024
run_case r80000000_w32768_m1024 80000000 80032768 1024

summary_status="$(summarize)"
echo "$summary_status" > "$out_dir/status.txt"
echo "cuda static-reach equivalence artifacts: $out_dir"
