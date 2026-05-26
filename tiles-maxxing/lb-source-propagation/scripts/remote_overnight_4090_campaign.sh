#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  remote_overnight_4090_campaign.sh [--repo DIR] [--out-dir DIR]
                                    [--wall-seconds N]

Run the LB source-propagation overnight 4090 benchmarking/profiling campaign
on an already-rented remote host. This script performs no Vast API actions.

The output is diagnostic/non-claim. It does not produce SOURCE_DEAD_CERT and
does not claim a moat result.
USAGE
}

repo_dir="$(pwd)"
out_dir="/workspace/lb-opt-4090-overnight"
wall_seconds="43200"

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
    --wall-seconds)
      wall_seconds="$2"
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

if ! [[ "$wall_seconds" =~ ^[0-9]+$ ]] || [[ "$wall_seconds" == "0" ]]; then
  echo "--wall-seconds must be a positive integer" >&2
  exit 2
fi

repo_dir="$(cd "$repo_dir" && pwd)"
sidecar_dir="$repo_dir/tiles-maxxing/lb-source-propagation"
verification_dir="$repo_dir/verification"
cuda_dir="$repo_dir/tiles-maxxing/cuda-campaign-v2-sqrt-36"
cpp_dir="$repo_dir/tiles-maxxing/cpp-campaign-v2"
build_dir="/tmp/gm-lbsp-overnight-k26"
verify_build_dir="/tmp/gm-lbsp-overnight-verify"
cuda_build_dir="/tmp/gm-cuda-overnight-k36"
cpu_build_dir="/tmp/gm-cpp-overnight-k36"
start_epoch="$(date +%s)"

mkdir -p "$out_dir"/{build,correctness,k26-baseline,high-radius,r60-r400,cuda-profile,sweep-matrix,k26-long-run,logs}

log() {
  printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" \
    | tee -a "$out_dir/logs/campaign.log"
}

remaining_seconds() {
  local now elapsed remaining
  now="$(date +%s)"
  elapsed=$((now - start_epoch))
  remaining=$((wall_seconds - elapsed))
  if (( remaining < 0 )); then
    remaining=0
  fi
  printf '%s\n' "$remaining"
}

write_status() {
  printf '%s\n' "$1" > "$out_dir/status.txt"
}

write_environment() {
  {
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "hostname=$(hostname)"
    echo "repo=$repo_dir"
    echo "branch=$(git -C "$repo_dir" branch --show-current 2>/dev/null || echo unknown)"
    echo "commit=$(git -C "$repo_dir" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "commit_short=$(git -C "$repo_dir" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "dirty_begin"
    git -C "$repo_dir" status --short --untracked-files=all 2>/dev/null || true
    echo "dirty_end"
    echo "remote_kind=remote_cpu_sidecar_and_optional_cuda_kernel"
    echo "proof_status=DIAGNOSTIC_NON_CLAIM"
    echo "wall_seconds=$wall_seconds"
    echo "nproc=$(nproc 2>/dev/null || echo unknown)"
    echo "memory_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo unknown)"
    if command -v nvidia-smi >/dev/null 2>&1; then
      echo "nvidia_smi_begin"
      nvidia-smi || true
      echo "nvidia_smi_end"
      echo "nvidia_smi_query_begin"
      nvidia-smi --query-gpu=name,driver_version,memory.total,utilization.gpu \
        --format=csv,noheader || true
      echo "nvidia_smi_query_end"
    else
      echo "nvidia_smi=unavailable"
    fi
  } > "$out_dir/remote-environment.txt"
}

run_logged() {
  local label="$1"
  local dir="$2"
  shift 2
  mkdir -p "$dir"
  log "RUN $label: $*"
  printf '%q ' "$@" > "$dir/command.argv.txt"
  printf '\n' >> "$dir/command.argv.txt"
  set +e
  "$@" > "$dir/stdout.log" 2> "$dir/stderr.log"
  local status="$?"
  set -e
  echo "$status" > "$dir/exit_code.txt"
  log "END $label exit=$status"
  return "$status"
}

run_timed() {
  local label="$1"
  local dir="$2"
  local timeout_seconds="$3"
  shift 3
  mkdir -p "$dir"
  log "RUN $label timeout=${timeout_seconds}: $*"
  printf '%q ' "$@" > "$dir/command.argv.txt"
  printf '\n' >> "$dir/command.argv.txt"
  set +e
  local begin_epoch end_epoch
  begin_epoch="$(date +%s)"
  if command -v /usr/bin/time >/dev/null 2>&1; then
    if [[ "$timeout_seconds" != "0" ]]; then
      /usr/bin/time -v -o "$dir/time.txt" timeout "$timeout_seconds" "$@" \
        > "$dir/stdout.log" 2> "$dir/stderr.log"
    else
      /usr/bin/time -v -o "$dir/time.txt" "$@" \
        > "$dir/stdout.log" 2> "$dir/stderr.log"
    fi
  else
    echo "time_binary=unavailable" > "$dir/time.txt"
    if [[ "$timeout_seconds" != "0" ]]; then
      timeout "$timeout_seconds" "$@" > "$dir/stdout.log" 2> "$dir/stderr.log"
    else
      "$@" > "$dir/stdout.log" 2> "$dir/stderr.log"
    fi
  fi
  local status="$?"
  end_epoch="$(date +%s)"
  set -e
  {
    echo "begin_epoch=$begin_epoch"
    echo "end_epoch=$end_epoch"
    echo "wall_seconds=$((end_epoch - begin_epoch))"
  } >> "$dir/time.txt"
  echo "$status" > "$dir/exit_code.txt"
  log "END $label exit=$status"
  return "$status"
}

record_row() {
  local name="$1"
  local dir="$2"
  local remote_kind="$3"
  local proof_status="$4"
  local required="$5"
  local status
  status="$(cat "$dir/exit_code.txt" 2>/dev/null || echo missing)"
  printf '{"schema":"lb_overnight_row_v1","name":"%s","dir":"%s","remote_kind":"%s","proof_status":"%s","required":%s,"exit_code":"%s"}\n' \
    "$name" "${dir#$out_dir/}" "$remote_kind" "$proof_status" "$required" "$status" \
    >> "$out_dir/campaign-rows.jsonl"
}

hash_artifacts() {
  (cd "$out_dir" && find . -type f ! -name artifact-ledger.sha256 -print0 \
    | sort -z | xargs -0 sha256sum > artifact-ledger.sha256)
}

summarize_json() {
  python3 - "$out_dir" <<'PY'
import json
import pathlib
import sys

out = pathlib.Path(sys.argv[1])
rows = []
rows_path = out / "campaign-rows.jsonl"
if rows_path.exists():
    for line in rows_path.read_text().splitlines():
        if line.strip():
            rows.append(json.loads(line))

def read_text(name):
    p = out / name
    return p.read_text(errors="replace") if p.exists() else ""

summary = {
    "schema": "lb_overnight_4090_campaign_summary_v1",
    "proof_status": "DIAGNOSTIC_NON_CLAIM",
    "status": read_text("status.txt").strip() or "UNKNOWN",
    "rows": rows,
    "environment": "remote-environment.txt",
    "artifact_ledger": "artifact-ledger.sha256",
}
(out / "campaign-summary.json").write_text(
    json.dumps(summary, indent=2, sort_keys=True) + "\n"
)

lines = [
    "# LB Overnight 4090 Campaign Summary",
    "",
    f"Status: `{summary['status']}`",
    "",
    "Proof status: `DIAGNOSTIC_NON_CLAIM`",
    "",
    "## Rows",
    "",
]
for row in rows:
    lines.append(
        f"- `{row['name']}`: exit `{row['exit_code']}`, "
        f"remote_kind `{row['remote_kind']}`, required `{row['required']}`"
    )
lines.extend([
    "",
    "This artifact is diagnostic benchmarking/profiling evidence only.",
])
(out / "campaign-summary.md").write_text("\n".join(lines) + "\n")
PY
}

critical_fail() {
  local label="$1"
  write_status "LB_OVERNIGHT_4090_BLOCKED_${label}"
  hash_artifacts || true
  summarize_json || true
  exit 1
}

write_environment
: > "$out_dir/campaign-rows.jsonl"
write_status "LB_OVERNIGHT_4090_RUNNING"
log "campaign start out_dir=$out_dir"

if [[ ! -f "$sidecar_dir/CMakeLists.txt" || ! -f "$verification_dir/CMakeLists.txt" ]]; then
  critical_fail "PATH_PREFLIGHT"
fi

log "building sidecar K26"
run_logged cmake-sidecar-configure "$out_dir/build/sidecar-configure" \
  cmake -S "$sidecar_dir" -B "$build_dir" -DK_SQ=26 -DCMAKE_BUILD_TYPE=Release \
  || critical_fail "SIDECAR_CONFIGURE"
record_row cmake-sidecar-configure "$out_dir/build/sidecar-configure" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true

run_logged cmake-sidecar-build "$out_dir/build/sidecar-build" \
  cmake --build "$build_dir" -j"$(nproc)" \
  || critical_fail "SIDECAR_BUILD"
record_row cmake-sidecar-build "$out_dir/build/sidecar-build" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true

log "building verification"
run_logged cmake-verification-configure "$out_dir/build/verification-configure" \
  cmake -S "$verification_dir" -B "$verify_build_dir" -DCMAKE_BUILD_TYPE=Release \
  || critical_fail "VERIFY_CONFIGURE"
record_row cmake-verification-configure "$out_dir/build/verification-configure" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true

run_logged cmake-verification-build "$out_dir/build/verification-build" \
  cmake --build "$verify_build_dir" -j"$(nproc)" \
  || critical_fail "VERIFY_BUILD"
record_row cmake-verification-build "$out_dir/build/verification-build" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true

run_logged sidecar-ctest "$out_dir/correctness/sidecar-ctest" \
  ctest --test-dir "$build_dir" --output-on-failure \
  || critical_fail "SIDECAR_CTEST"
record_row sidecar-ctest "$out_dir/correctness/sidecar-ctest" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true

run_logged verification-ctest "$out_dir/correctness/verification-ctest" \
  ctest --test-dir "$verify_build_dir" --output-on-failure \
  || critical_fail "VERIFY_CTEST"
record_row verification-ctest "$out_dir/correctness/verification-ctest" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true

run_timed wide-equivalence "$out_dir/correctness/wide-equivalence" 2400 \
  "$sidecar_dir/scripts/check_tileop_port_wide_band_equivalence.sh" \
  "$build_dir/source_tileop_port_runner" \
  --r-start 8192 --segments 5 --segment-width 8192 \
  --max-atoms 50000000 --tileop-threads 32 \
  --out-dir "$out_dir/correctness/wide-equivalence/artifacts" \
  || critical_fail "WIDE_EQUIVALENCE"
record_row wide-equivalence "$out_dir/correctness/wide-equivalence" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true
hash_artifacts || true

run_timed k26-budget-probe "$out_dir/k26-baseline/budget-probe" 3600 \
  "$sidecar_dir/scripts/remote_k26_timing_probe.sh" \
  --repo "$repo_dir" \
  --build-dir "$build_dir" \
  --verify-build-dir "$verify_build_dir" \
  --out-dir "$out_dir/k26-baseline/budget-probe/artifacts" \
  --chunk-bands 5 \
  --timeout-seconds 1200 \
  --max-runtime-seconds 1800 \
  --tileop-threads 32 \
  || true
record_row k26-budget-probe "$out_dir/k26-baseline/budget-probe" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true
hash_artifacts || true

runner="$build_dir/source_tileop_port_runner"

run_timed high-radius-r1m "$out_dir/high-radius/r999424-w8192" 1800 \
  "$runner" \
  --r-start 999424 --r-final 1007616 --band-width 8192 \
  --schedule-radii 999424,1007616 \
  --seed-inner-flags \
  --max-atoms 50000000 \
  --tileop-threads 32 \
  --target-a 376039 --target-b 943460 \
  --progress-out "$out_dir/high-radius/r999424-w8192/progress.jsonl" \
  || true
record_row high-radius-r1m "$out_dir/high-radius/r999424-w8192" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true
hash_artifacts || true

run_timed r60-w128 "$out_dir/r60-r400/r60-w128" 2400 \
  "$runner" \
  --r-start 60000000 --r-final 60000128 --band-width 128 \
  --schedule-radii 60000000,60000128 \
  --seed-inner-flags \
  --max-atoms 20000000 \
  --tileop-threads 32 \
  --progress-out "$out_dir/r60-r400/r60-w128/progress.jsonl" \
  || true
record_row r60-w128 "$out_dir/r60-r400/r60-w128" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true
hash_artifacts || true

run_timed r400-w16 "$out_dir/r60-r400/r400-w16" 2400 \
  "$runner" \
  --r-start 400000000 --r-final 400000016 --band-width 16 \
  --schedule-radii 400000000,400000016 \
  --seed-inner-flags \
  --max-atoms 20000000 \
  --tileop-threads 32 \
  --progress-out "$out_dir/r60-r400/r400-w16/progress.jsonl" \
  || true
record_row r400-w16 "$out_dir/r60-r400/r400-w16" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true
hash_artifacts || true

for threads in 16 32 0; do
  run_timed "thread-sweep-${threads}" "$out_dir/sweep-matrix/thread-${threads}" 1200 \
    "$runner" \
    --r-start 245760 --r-final 253952 --band-width 8192 \
    --schedule-radii 245760,253952 \
    --seed-inner-flags \
    --max-atoms 50000000 \
    --tileop-threads "$threads" \
    --progress-out "$out_dir/sweep-matrix/thread-${threads}/progress.jsonl" \
    || true
  record_row "thread-sweep-${threads}" "$out_dir/sweep-matrix/thread-${threads}" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM false
  hash_artifacts || true
done

for width in 4096 8192 16384; do
  r_final=$((245760 + width))
  run_timed "band-width-${width}" "$out_dir/sweep-matrix/band-width-${width}" 1800 \
    "$runner" \
    --r-start 245760 --r-final "$r_final" --band-width "$width" \
    --schedule-radii "245760,$r_final" \
    --seed-inner-flags \
    --max-atoms 50000000 \
    --tileop-threads 32 \
    --progress-out "$out_dir/sweep-matrix/band-width-${width}/progress.jsonl" \
    || true
  record_row "band-width-${width}" "$out_dir/sweep-matrix/band-width-${width}" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM false
  hash_artifacts || true
done

if command -v nvcc >/dev/null 2>&1 && [[ -f "$cuda_dir/CMakeLists.txt" ]]; then
  log "building CUDA campaign K36"
  if run_logged cuda-configure "$out_dir/cuda-profile/configure" \
      cmake -S "$cuda_dir" -B "$cuda_build_dir" -DK_SQ=36 -DCMAKE_BUILD_TYPE=Release; then
    record_row cuda-configure "$out_dir/cuda-profile/configure" cuda_kernel DIAGNOSTIC_NON_CLAIM false
    if run_logged cuda-build "$out_dir/cuda-profile/build" \
        cmake --build "$cuda_build_dir" -j"$(nproc)"; then
      record_row cuda-build "$out_dir/cuda-profile/build" cuda_kernel DIAGNOSTIC_NON_CLAIM false
      run_logged cuda-ctest "$out_dir/cuda-profile/ctest" \
        ctest --test-dir "$cuda_build_dir" --output-on-failure || true
      record_row cuda-ctest "$out_dir/cuda-profile/ctest" cuda_kernel DIAGNOSTIC_NON_CLAIM false
      if [[ -f "$cpp_dir/CMakeLists.txt" ]]; then
        run_logged cpu-k36-configure "$out_dir/cuda-profile/cpu-k36-configure" \
          cmake -S "$cpp_dir" -B "$cpu_build_dir" -DK_SQ=36 -DCMAKE_BUILD_TYPE=Release || true
        record_row cpu-k36-configure "$out_dir/cuda-profile/cpu-k36-configure" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM false
        run_logged cpu-k36-build "$out_dir/cuda-profile/cpu-k36-build" \
          cmake --build "$cpu_build_dir" --target campaign_main -j"$(nproc)" || true
        record_row cpu-k36-build "$out_dir/cuda-profile/cpu-k36-build" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM false
      fi
      run_timed cuda-snapshot-smoke "$out_dir/cuda-profile/snapshot-smoke" 3600 \
        "$cuda_dir/scripts/run_snapshot_sha_gate.sh" \
        --smoke \
        --cpu-bin "$cpu_build_dir/campaign_main" \
        --cuda-bin "$cuda_build_dir/campaign_main_cuda" \
        --diff-bin "$cuda_build_dir/cuda_vs_cpu_diff" \
        --work-dir "$out_dir/cuda-profile/snapshot-smoke/work" || true
      record_row cuda-snapshot-smoke "$out_dir/cuda-profile/snapshot-smoke" cuda_kernel DIAGNOSTIC_NON_CLAIM false
    else
      record_row cuda-build "$out_dir/cuda-profile/build" cuda_kernel DIAGNOSTIC_NON_CLAIM false
    fi
  else
    record_row cuda-configure "$out_dir/cuda-profile/configure" cuda_kernel DIAGNOSTIC_NON_CLAIM false
  fi
else
  echo "CUDA tooling unavailable or CUDA campaign missing" \
    > "$out_dir/cuda-profile/BLOCKED.txt"
fi
hash_artifacts || true

rem="$(remaining_seconds)"
if (( rem > 1800 )); then
  long_runtime=$((rem - 900))
  if (( long_runtime > 39600 )); then
    long_runtime=39600
  fi
  run_timed k26-long-run "$out_dir/k26-long-run/full-bundle" "$long_runtime" \
    "$sidecar_dir/scripts/run_k26_full_source_bundle.sh" \
    --build-dir "$build_dir" \
    --out-dir "$out_dir/k26-long-run/full-bundle/artifacts" \
    --continuation-chunk-bands 5 \
    --resume-existing \
    --timeout-seconds 0 \
    --max-runtime-seconds "$long_runtime" \
    --tileop-threads 32 \
    --source-dead-gap-checker "$verify_build_dir/source_dead_gap_check" \
    --source-dead-checker "$verify_build_dir/source_dead_cert_check" \
    || true
  record_row k26-long-run "$out_dir/k26-long-run/full-bundle" remote_cpu_sidecar DIAGNOSTIC_NON_CLAIM true
else
  echo "skipped: insufficient remaining seconds $rem" \
    > "$out_dir/k26-long-run/BLOCKED.txt"
fi

hash_artifacts || true
write_status "LB_OVERNIGHT_4090_FINISHED_DIAGNOSTIC_NON_CLAIM"
summarize_json
log "campaign finished out_dir=$out_dir"
hash_artifacts || true
