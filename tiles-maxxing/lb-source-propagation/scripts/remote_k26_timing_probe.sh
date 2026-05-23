#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  remote_k26_timing_probe.sh [--repo DIR] [--build-dir DIR] [--out-dir DIR]
                             [--chunk-bands N]
                             [--timeout-seconds N]
                             [--max-runtime-seconds N]

Run a bounded, non-claim sqrt(26) source/origin timing probe on a remote host.
This performs no Vast API actions. It builds the LB source-propagation sidecar
with -DK_SQ=26, runs the chunked K26 bundle harness with resume support, records
runtime-budget diagnostics, and preserves all artifacts under OUT_DIR.

Expected nonzero harness blockers such as continuation timeout, runtime limit,
source still live, missing source-dead cert, or target not reached are accepted
as timing-probe evidence. This script does not produce SOURCE_DEAD_CERT and does
not claim a moat result.

Defaults:
  --repo                current working directory
  --build-dir           /tmp/gm-lbsp-remote-k26
  --out-dir             /workspace/lb-source-k26-timing-probe
  --chunk-bands         8
  --timeout-seconds     1200
  --max-runtime-seconds 14000
USAGE
}

repo_dir="$(pwd)"
build_dir="/tmp/gm-lbsp-remote-k26"
out_dir="/workspace/lb-source-k26-timing-probe"
chunk_bands="8"
timeout_seconds="1200"
max_runtime_seconds="14000"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      repo_dir="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --out-dir)
      out_dir="$2"
      shift 2
      ;;
    --chunk-bands)
      chunk_bands="$2"
      shift 2
      ;;
    --timeout-seconds)
      timeout_seconds="$2"
      shift 2
      ;;
    --max-runtime-seconds)
      max_runtime_seconds="$2"
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

require_nonnegative_integer() {
  local value="$1"
  local label="$2"
  if ! [[ "$value" =~ ^[0-9]+$ ]]; then
    echo "${label} must be a nonnegative integer: $value" >&2
    exit 2
  fi
}

require_positive_integer() {
  local value="$1"
  local label="$2"
  require_nonnegative_integer "$value" "$label"
  if [[ "$value" == "0" ]]; then
    echo "${label} must be positive" >&2
    exit 2
  fi
}

require_positive_integer "$chunk_bands" "--chunk-bands"
require_nonnegative_integer "$timeout_seconds" "--timeout-seconds"
require_nonnegative_integer "$max_runtime_seconds" "--max-runtime-seconds"

sidecar_dir="$repo_dir/tiles-maxxing/lb-source-propagation"
if [[ ! -f "$sidecar_dir/CMakeLists.txt" ]]; then
  echo "sidecar CMakeLists.txt not found at $sidecar_dir" >&2
  exit 2
fi

mkdir -p "$out_dir"

{
  echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "repo=$repo_dir"
  echo "commit=$(git -C "$repo_dir" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "branch=$(git -C "$repo_dir" branch --show-current 2>/dev/null || echo unknown)"
  echo "k_sq=26"
  echo "chunk_bands=$chunk_bands"
  echo "timeout_seconds=$timeout_seconds"
  echo "max_runtime_seconds=$max_runtime_seconds"
  if command -v nvidia-smi >/dev/null 2>&1; then
    echo "nvidia_smi_begin"
    nvidia-smi || true
    echo "nvidia_smi_end"
  else
    echo "nvidia_smi=unavailable"
  fi
} > "$out_dir/environment.txt"

cmake -S "$sidecar_dir" -B "$build_dir" -DK_SQ=26 \
  | tee "$out_dir/cmake-configure.log"
cmake --build "$build_dir" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" \
  | tee "$out_dir/cmake-build.log"

set +e
"$sidecar_dir/scripts/run_k26_full_source_bundle.sh" \
  --build-dir "$build_dir" \
  --out-dir "$out_dir" \
  --continuation-chunk-bands "$chunk_bands" \
  --resume-existing \
  --timeout-seconds "$timeout_seconds" \
  --max-runtime-seconds "$max_runtime_seconds" \
  > "$out_dir/k26-bundle-harness.log" \
  2> "$out_dir/k26-bundle-harness.err"
harness_status="$?"
set -e

{
  echo "harness_exit_code=$harness_status"
  if [[ -f "$out_dir/status.txt" ]]; then
    sed -n '1,80p' "$out_dir/status.txt"
  else
    echo "missing_status_txt=true"
  fi
} > "$out_dir/harness-status-summary.txt"

if [[ ! -f "$out_dir/status.txt" ]]; then
  echo "K26 timing probe did not produce status.txt" >&2
  exit 1
fi

status_line="$(sed -n '1p' "$out_dir/status.txt")"
case "$status_line" in
  K26_FULL_RUN_BUNDLE_BLOCKED_*|K26_FULL_RUN_BUNDLE_PASS)
    ;;
  *)
    echo "unexpected K26 timing probe status: $status_line" >&2
    exit 1
    ;;
esac

if [[ -f "$out_dir/k26-continuation-progress.jsonl" ]]; then
  set +e
  "$sidecar_dir/scripts/check_k26_runtime_budget.py" \
    --progress "$out_dir/k26-continuation-progress.jsonl" \
    --chunk-ledger "$out_dir/k26-continuation-chunks.jsonl" \
    --schedule-segment-count 123 \
    --max-runtime-seconds "$max_runtime_seconds" \
    > "$out_dir/k26-runtime-budget-check.manual.log" \
    2> "$out_dir/k26-runtime-budget-check.manual.err"
  manual_runtime_status="$?"
  set -e
  echo "manual_runtime_budget_exit_code=$manual_runtime_status" \
    > "$out_dir/k26-runtime-budget-check.manual.meta"
fi

cat > "$out_dir/remote-k26-timing-probe-status.txt" <<'STATUS'
REMOTE_K26_TIMING_PROBE_PASS
Scope: bounded non-claim K26 source/origin timing probe using the chunked bundle
harness. Expected bundle blockers are timing evidence only.
Non-claim: this is not SOURCE_DEAD_CERT and not a moat result.
STATUS

echo "remote K26 timing probe artifacts: $out_dir"
