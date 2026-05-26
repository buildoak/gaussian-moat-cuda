#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  remote_cuda_static_reach_equivalence_campaign.sh [--repo DIR]
      [--out-dir DIR] [--build-dir DIR] [--timeout-seconds N]
      [--cuda-arch ARCH] [--include-diagnostic-sub4096]
      [--include-full-static-production] [--static-reach-resident-width W]

Build and run CUDA static-reach stitching equivalence gates on a remote CUDA
host. Default rows use production-width microbands only. Outputs are
diagnostic/non-claim.
USAGE
}

repo_dir="$(pwd)"
out_dir="/workspace/cuda-static-reach-equivalence"
build_dir="/tmp/gm-cuda-static-reach-equivalence"
timeout_seconds="5400"
cuda_arch="89"
include_diagnostic_sub4096="0"
include_full_static_production="0"
static_reach_resident_width="4096"

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
    --cuda-arch)
      cuda_arch="$2"
      shift 2
      ;;
    --include-diagnostic-sub4096)
      include_diagnostic_sub4096="1"
      shift
      ;;
    --include-full-static-production)
      include_full_static_production="1"
      shift
      ;;
    --static-reach-resident-width)
      static_reach_resident_width="$2"
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
if ! [[ "$cuda_arch" =~ ^[0-9]+$ ]] || [[ "$cuda_arch" == "0" ]]; then
  echo "--cuda-arch must be positive" >&2
  exit 2
fi
if ! [[ "$static_reach_resident_width" =~ ^[0-9]+$ ]] || [[ "$static_reach_resident_width" == "0" ]]; then
  echo "--static-reach-resident-width must be positive" >&2
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
    echo "cuda_arch=$cuda_arch"
    echo "include_diagnostic_sub4096=$include_diagnostic_sub4096"
    echo "include_full_static_production=$include_full_static_production"
    echo "static_reach_resident_width=$static_reach_resident_width"
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
  shift 4
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
    "--static-reach-resident-width=$static_reach_resident_width"
    --chunk-size=200000
    --max-atoms=1000000000
    "$@"
  )

  log "CASE ${label} proof_status=DIAGNOSTIC_NON_CLAIM r_start=${r_start} r_final=${r_final} microband_width=${width} artifacts=cases/${label}.*"
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
  printf '{"schema":"cuda_static_reach_equivalence_campaign_case_v2","proof_status":"DIAGNOSTIC_NON_CLAIM","label":"%s","r_start":%s,"r_final":%s,"microband_width":%s,"exit_code":%s,"stdout":"cases/%s.stdout.json","stderr":"cases/%s.stderr.log","time":"cases/%s.time.txt","argv":"cases/%s.argv.txt","exit_code_file":"cases/%s.exit_code.txt"}\n' \
    "$label" "$r_start" "$r_final" "$width" "$rc" "$label" "$label" "$label" \
    "$label" "$label" \
    >> "$out_dir/cases.jsonl"
  log "END_CASE ${label} exit=${rc} wall_seconds=$((end_epoch - begin_epoch))"
}

summarize() {
  python3 - "$out_dir" <<'PY'
import json
import pathlib
import sys

out = pathlib.Path(sys.argv[1])
required_payload_scalars = {
    "phase0_schema": "cuda_static_reach_equivalence_phase0_telemetry_v1",
    "runner_id": "cuda_static_reach_equivalence_v1",
    "proof_status": "DIAGNOSTIC_NON_CLAIM",
}
required_numeric_fields = [
    "static_reach_resident_width",
    "resident_subsegments",
    "rss_bytes",
    "peak_rss_bytes",
    "max_resident_microband_tiles",
    "max_resident_tileops",
    "max_resident_port_atoms",
    "max_resident_edges",
    "max_live_frontier_atoms",
    "max_components",
    "wall_share_full_compositor",
    "wall_share_full_static",
    "wall_share_stitched",
    "wall_share_materialization",
    "wall_share_handoff_hash",
]
required_timing_fields = [
    "full",
    "full_static",
    "stitched",
    "materialization",
    "handoff_hash",
    "pre_handoff_total",
    "total",
]

def telemetry_errors(payload):
    errors = []
    if not isinstance(payload, dict):
        return ["payload_missing_or_not_object"]
    for key, expected in required_payload_scalars.items():
        if payload.get(key) != expected:
            errors.append(f"{key}_mismatch")
    if payload.get("static_reach_materialization_mode") not in {
        "streaming_dsu",
        "materialized_band",
    }:
        errors.append("static_reach_materialization_mode_missing_or_invalid")
    for key in required_numeric_fields:
        value = payload.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or value < 0:
            errors.append(f"{key}_missing_or_negative")
    timings = payload.get("timings_ms")
    if not isinstance(timings, dict):
        errors.append("timings_ms_missing")
    else:
        for key in required_timing_fields:
            value = timings.get(key)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or value < 0:
                errors.append(f"timings_ms.{key}_missing_or_negative")
        if (
            isinstance(timings.get("total"), (int, float))
            and isinstance(timings.get("pre_handoff_total"), (int, float))
            and timings["total"] < timings["pre_handoff_total"]
        ):
            errors.append("timings_ms.total_lt_pre_handoff_total")
    return errors

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
    row["telemetry_errors"] = telemetry_errors(payload)
    cases.append(row)

status = "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_PASS"
if any(row["exit_code"] != 0 for row in cases):
    status = "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_FAILED"
elif any((row.get("payload") or {}).get("status") != "CUDA_STATIC_REACH_EQUIVALENCE_PASS"
         for row in cases):
    status = "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_MISMATCH"
elif any(row["telemetry_errors"] for row in cases):
    status = "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_TELEMETRY_INVALID"

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
        f"`{payload.get('equivalent')}`, resident_width "
        f"`{payload.get('static_reach_resident_width')}`, mode "
        f"`{payload.get('static_reach_materialization_mode')}`, total_ms "
        f"`{(payload.get('timings_ms') or {}).get('total')}`"
    )
(out / "summary.md").write_text("\n".join(lines) + "\n")
print(status)
PY
}

echo "CUDA_STATIC_REACH_EQUIVALENCE_CAMPAIGN_RUNNING" > "$out_dir/status.txt"
write_environment
run_logged cmake-configure cmake -S "$cuda_dir" -B "$build_dir" \
  -DK_SQ=36 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES="$cuda_arch"
run_logged cmake-build cmake --build "$build_dir" \
  --target cuda_static_reach_equivalence -j"$(nproc 2>/dev/null || echo 8)"

if [[ "$include_diagnostic_sub4096" == "1" ]]; then
  run_case diagnostic_sub4096_smoke_r5000_w1024 5000 10000 1024 \
    --allow-diagnostic-microband-width
fi

run_case phase0_r60000000_w8192_split4096_full_static 60000000 60008192 4096 \
  --enable-full-static-handoff

if [[ "$include_full_static_production" == "1" ]]; then
  run_case phase0_r60000000_w32768_split8192_full_static 60000000 60032768 8192 \
    --enable-full-static-handoff
  run_case phase0_r80000000_w32768_split8192_full_static 80000000 80032768 8192 \
    --enable-full-static-handoff
else
  run_case phase0_r60000000_w32768_split8192_telemetry 60000000 60032768 8192
  run_case phase0_r80000000_w32768_split8192_telemetry 80000000 80032768 8192
fi

summary_status="$(summarize)"
echo "$summary_status" > "$out_dir/status.txt"
echo "cuda static-reach equivalence artifacts: $out_dir"
