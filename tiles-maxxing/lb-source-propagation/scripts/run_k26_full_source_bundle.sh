#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  run_k26_full_source_bundle.sh --build-dir DIR --out-dir DIR
                                [--max-atoms N]
                                [--timeout-seconds N]
                                [--max-runtime-seconds N]
                                [--tileop-threads N]
                                [--continuation-chunk-bands N]
                                [--resume-existing]
                                [--cert-in PATH]
                                [--source-dead-gap-checker PATH]
                                [--source-dead-checker PATH]

Run the prepared sqrt(26) source/origin bundle contract using an existing
lb-source-propagation build directory. This script performs no Vast API actions
and does not claim a moat result.

Artifacts written under OUT_DIR:
  k26_source_run_commands.json
  k26_bz_schedule_check.json
  k26_source_run_profile.json
  k26-prefix-result.json
  k26-prefix-progress.jsonl
  k26-continuation-result.json
  k26-continuation-progress.jsonl
  k26-continuation-chunks.jsonl, when --continuation-chunk-bands is supplied
  k26-continuation-chunk-*.json and k26-continuation-chunk-*.manifest.txt,
    when --continuation-chunk-bands is supplied
  k26-prefix-manifest.txt
  k26-prefix-witness.txt
  k26-source-dead-gap.json, when the continuation reaches terminal source death
  k26-full-run-artifacts.sha256
  status.txt

If --cert-in is supplied, it is copied to k26-source-dead-cert.json and the
bundle checker is run. Without terminal source death, the script stops after
the continuation with K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_STILL_LIVE. With
terminal source death but no target reachability, it writes the source-dead gap
and stops with K26_FULL_RUN_BUNDLE_BLOCKED_TARGET_NOT_REACHED. With terminal
source death, target reachability, and both independent checkers supplied, it
synthesizes a summary-only non-claim k26-source-dead-cert.json from the prefix
witness and coordinate-port expansion paths, then lets the bundle checker name
the remaining SOURCE_DEAD_CERT blocker. Without checkers or --cert-in, it stops
with K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING.
USAGE
}

build_dir=""
out_dir=""
max_atoms="50000000"
timeout_seconds="0"
max_runtime_seconds="0"
tileop_threads="0"
continuation_chunk_bands="0"
resume_existing="0"
cert_in=""
source_dead_gap_checker=""
source_dead_checker=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --out-dir)
      out_dir="$2"
      shift 2
      ;;
    --max-atoms)
      max_atoms="$2"
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
    --tileop-threads)
      tileop_threads="$2"
      shift 2
      ;;
    --continuation-chunk-bands)
      continuation_chunk_bands="$2"
      shift 2
      ;;
    --resume-existing)
      resume_existing="1"
      shift
      ;;
    --cert-in)
      cert_in="$2"
      shift 2
      ;;
    --source-dead-gap-checker)
      source_dead_gap_checker="$2"
      shift 2
      ;;
    --source-dead-checker)
      source_dead_checker="$2"
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

if [[ -z "$build_dir" || -z "$out_dir" ]]; then
  echo "missing required --build-dir or --out-dir" >&2
  usage >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bundle_checker="$script_dir/check_k26_full_run_bundle.sh"
runtime_budget_checker="$script_dir/check_k26_runtime_budget.py"

required_bins=(
  k26_source_run_commands
  k26_bz_schedule_check
  k26_source_run_profile
  source_origin_cpu_runner
  source_tileop_port_runner
)

for bin in "${required_bins[@]}"; do
  if [[ ! -x "$build_dir/$bin" ]]; then
    echo "required executable not found: $build_dir/$bin" >&2
    exit 2
  fi
done

if [[ ! "$max_atoms" =~ ^[0-9]+$ || "$max_atoms" == "0" ]]; then
  echo "--max-atoms must be a positive integer" >&2
  exit 2
fi
if [[ ! "$timeout_seconds" =~ ^[0-9]+$ ]]; then
  echo "--timeout-seconds must be a nonnegative integer" >&2
  exit 2
fi
if [[ ! "$max_runtime_seconds" =~ ^[0-9]+$ ]]; then
  echo "--max-runtime-seconds must be a nonnegative integer" >&2
  exit 2
fi
if [[ ! "$tileop_threads" =~ ^[0-9]+$ ]]; then
  echo "--tileop-threads must be a nonnegative integer" >&2
  exit 2
fi
if [[ ! "$continuation_chunk_bands" =~ ^[0-9]+$ ]]; then
  echo "--continuation-chunk-bands must be a nonnegative integer" >&2
  exit 2
fi

require_k26_cmake_cache() {
  local cache="$build_dir/CMakeCache.txt"
  local configured_k_sq
  if [[ ! -f "$cache" ]]; then
    return
  fi
  configured_k_sq="$(
    sed -nE 's/^K_SQ:[^=]*=([0-9]+)$/\1/p' "$cache" | head -n 1
  )"
  if [[ -z "$configured_k_sq" ]]; then
    echo "CMakeCache.txt exists but does not record K_SQ: $cache" >&2
    exit 2
  fi
  if [[ "$configured_k_sq" != "26" ]]; then
    echo "K26 full-run bundle requires build configured with -DK_SQ=26; found K_SQ=$configured_k_sq in $cache" >&2
    exit 2
  fi
}

require_k26_cmake_cache

mkdir -p "$out_dir"
run_start_seconds="$SECONDS"

status_file="$out_dir/status.txt"
artifact_manifest="$out_dir/k26-full-run-artifacts.sha256"
source_dead_gap="$out_dir/k26-source-dead-gap.json"
chunk_ledger="$out_dir/k26-continuation-chunks.jsonl"

record_runtime_budget_diagnostic() {
  local label="$1"
  local progress="$2"
  if [[ ! -x "$runtime_budget_checker" || ! -f "$progress" ]]; then
    return
  fi

  local args=(
    "$runtime_budget_checker"
    --progress "$progress"
    --schedule-segment-count 123
  )
  if [[ "$max_runtime_seconds" != "0" ]]; then
    args+=(--max-runtime-seconds "$max_runtime_seconds")
  fi
  if [[ -f "$chunk_ledger" ]]; then
    args+=(--chunk-ledger "$chunk_ledger")
  fi

  set +e
  "${args[@]}" \
    > "$out_dir/k26-runtime-budget-check.log" \
    2> "$out_dir/k26-runtime-budget-check.err"
  local checker_status=$?
  set -e
  {
    echo "label=$label"
    echo "progress=$(basename "$progress")"
    echo "exit_code=$checker_status"
  } > "$out_dir/k26-runtime-budget-check.meta"
}

record_runtime_budget_for_run() {
  local label="$1"
  local output="$2"
  case "$label" in
    K26_CONTINUATION)
      record_runtime_budget_diagnostic \
        "$label" "$out_dir/k26-continuation-progress.jsonl"
      ;;
    K26_CONTINUATION_CHUNK_*)
      record_runtime_budget_diagnostic "$label" "${output%.json}.progress.jsonl"
      ;;
  esac
}

runtime_budget_status() {
  if [[ ! -f "$out_dir/k26-runtime-budget-check.log" ]]; then
    return
  fi
  sed -nE 's/.*"status":"([^"]+)".*/\1/p' \
    "$out_dir/k26-runtime-budget-check.log" | head -n 1
}

enforce_chunk_runtime_budget() {
  local chunk_id="$1"
  if [[ "$max_runtime_seconds" == "0" ]]; then
    return
  fi
  record_runtime_budget_diagnostic \
    "K26_CONTINUATION_CHUNK_${chunk_id}_CUMULATIVE" \
    "$out_dir/k26-continuation-progress.jsonl"
  case "$(runtime_budget_status)" in
    K26_RUNTIME_BUDGET_REJECT)
      write_status \
        "K26_FULL_RUN_BUNDLE_BLOCKED_K26_CONTINUATION_CHUNK_${chunk_id}_RUNTIME_BUDGET_REJECT"
      echo "K26 continuation chunk ${chunk_id} completed, but cumulative runtime-budget projection now rejects the paid-run cap" >&2
      exit 3
      ;;
  esac
}

write_status() {
  local status="$1"
  {
    echo "$status"
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "build_dir=$build_dir"
    echo "out_dir=$out_dir"
    echo "max_atoms=$max_atoms"
    echo "timeout_seconds=$timeout_seconds"
    echo "max_runtime_seconds=$max_runtime_seconds"
    echo "tileop_threads=$tileop_threads"
    echo "elapsed_seconds=$((SECONDS - run_start_seconds))"
    echo "continuation_chunk_bands=$continuation_chunk_bands"
    echo "resume_existing=$resume_existing"
    echo "non_claim=this is an executed bundle harness, not a source-dead acceptance"
    if [[ -f "$out_dir/k26-source-dead-gap-check.log" ]]; then
      sed -nE 's/.*"status":"([^"]+)".*/source_dead_gap_check_status=\1/p' \
        "$out_dir/k26-source-dead-gap-check.log" | head -n 1
      sed -nE 's/.*"bridge_safety":"([^"]+)".*/source_dead_gap_bridge_safety=\1/p' \
        "$out_dir/k26-source-dead-gap-check.log" | head -n 1
      sed -nE 's/.*"coordinate_path_obligation":"([^"]+)".*/source_dead_gap_coordinate_path_obligation=\1/p' \
        "$out_dir/k26-source-dead-gap-check.log" | head -n 1
      sed -nE 's/.*"bz_schedule_obligation":"([^"]+)".*/source_dead_gap_bz_schedule_obligation=\1/p' \
        "$out_dir/k26-source-dead-gap-check.log" | head -n 1
      sed -nE 's/.*"terminal_inventory_obligation":"([^"]+)".*/source_dead_gap_terminal_inventory_obligation=\1/p' \
        "$out_dir/k26-source-dead-gap-check.log" | head -n 1
      sed -nE 's/.*"claim_grade":(true|false).*/source_dead_gap_claim_grade=\1/p' \
        "$out_dir/k26-source-dead-gap-check.log" | head -n 1
    fi
    if [[ -f "$out_dir/k26-runtime-budget-check.log" ]]; then
      sed -nE 's/.*"status":"([^"]+)".*/runtime_budget_check_status=\1/p' \
        "$out_dir/k26-runtime-budget-check.log" | head -n 1
      sed -nE 's/.*"completed_band_count":([0-9]+).*/runtime_budget_completed_band_count=\1/p' \
        "$out_dir/k26-runtime-budget-check.log" | head -n 1
      sed -nE 's/.*"projected_total_seconds":([0-9]+).*/runtime_budget_projected_total_seconds=\1/p' \
        "$out_dir/k26-runtime-budget-check.log" | head -n 1
      sed -nE 's/.*"cumulative_projected_total_seconds":([0-9]+).*/runtime_budget_cumulative_projected_total_seconds=\1/p' \
        "$out_dir/k26-runtime-budget-check.log" | head -n 1
      sed -nE 's/.*"tail_projected_total_seconds":([0-9]+).*/runtime_budget_tail_projected_total_seconds=\1/p' \
        "$out_dir/k26-runtime-budget-check.log" | head -n 1
      sed -nE 's/.*"tail_window_band_count":([0-9]+).*/runtime_budget_tail_window_band_count=\1/p' \
        "$out_dir/k26-runtime-budget-check.log" | head -n 1
      sed -nE 's/.*"budget_margin_seconds":(-?[0-9]+).*/runtime_budget_margin_seconds=\1/p' \
        "$out_dir/k26-runtime-budget-check.log" | head -n 1
      sed -nE 's/.*"last_completed_r_outer":([0-9]+).*/runtime_budget_last_completed_r_outer=\1/p' \
        "$out_dir/k26-runtime-budget-check.log" | head -n 1
    fi
    if [[ -f "$out_dir/k26-runtime-budget-check.meta" ]]; then
      sed -nE 's/^label=(.*)$/runtime_budget_label=\1/p' \
        "$out_dir/k26-runtime-budget-check.meta" | head -n 1
      sed -nE 's/^progress=(.*)$/runtime_budget_progress=\1/p' \
        "$out_dir/k26-runtime-budget-check.meta" | head -n 1
      sed -nE 's/^exit_code=([0-9]+)$/runtime_budget_exit_code=\1/p' \
        "$out_dir/k26-runtime-budget-check.meta" | head -n 1
    fi
  } > "$status_file"
}

check_source_dead_gap() {
  if [[ -z "$source_dead_gap_checker" ]]; then
    return
  fi
  if [[ ! -x "$source_dead_gap_checker" ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_GAP_CHECKER_MISSING"
    echo "source-dead gap checker is not executable: $source_dead_gap_checker" >&2
    exit 2
  fi
  if ! "$source_dead_gap_checker" "$source_dead_gap" \
      > "$out_dir/k26-source-dead-gap-check.log" \
      2> "$out_dir/k26-source-dead-gap-check.err"; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_GAP_REJECTED"
    cat "$out_dir/k26-source-dead-gap-check.err" >&2
    exit 1
  fi
}

write_artifact_manifest() {
  local names=(
    k26_source_run_commands.json
    k26_bz_schedule_check.json
    k26_source_run_profile.json
    k26-prefix-result.json
    k26-prefix-progress.jsonl
    k26-continuation-result.json
    k26-continuation-progress.jsonl
    k26-continuation-chunks.jsonl
    k26-prefix-manifest.txt
    k26-prefix-witness.txt
    k26-source-dead-gap.json
    k26-source-dead-cert.json
  )
  : > "$artifact_manifest"
  local name path digest
  for name in "${names[@]}"; do
    path="$out_dir/$name"
    if [[ ! -f "$path" ]]; then
      continue
    fi
    digest="$(shasum -a 256 "$path" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
    if [[ -z "$digest" ]]; then
      write_status "K26_FULL_RUN_BUNDLE_BLOCKED_HASH_FAILED"
      echo "could not hash artifact: $path" >&2
      exit 1
    fi
    printf '%s  %s\n' "$digest" "$name" >> "$artifact_manifest"
  done
  while IFS= read -r path; do
    [[ -z "$path" ]] && continue
    name="${path#$out_dir/}"
    digest="$(shasum -a 256 "$path" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
    if [[ -z "$digest" ]]; then
      write_status "K26_FULL_RUN_BUNDLE_BLOCKED_HASH_FAILED"
      echo "could not hash artifact: $path" >&2
      exit 1
    fi
    printf '%s  %s\n' "$digest" "$name" >> "$artifact_manifest"
  done < <(find "$out_dir" -maxdepth 1 -type f \
    \( -name 'k26-continuation-chunk-*.json' \
       -o -name 'k26-continuation-chunk-*.manifest.txt' \
       -o -name 'k26-continuation-chunk-*.progress.jsonl' \) \
    -print | LC_ALL=C sort)
}

run_json() {
  local label="$1"
  local output="$2"
  shift 2
  local stderr_file="$output.stderr"
  local status effective_timeout timeout_kind remaining_seconds
  echo "RUN $label: $*" >> "$out_dir/run.log"
  effective_timeout="0"
  timeout_kind="none"
  if [[ "$max_runtime_seconds" != "0" ]]; then
    remaining_seconds="$((run_start_seconds + max_runtime_seconds - SECONDS))"
    if (( remaining_seconds <= 0 )); then
      write_status "K26_FULL_RUN_BUNDLE_BLOCKED_${label}_RUNTIME_LIMIT"
      echo "$label not started because max runtime ${max_runtime_seconds}s is exhausted" >&2
      exit 124
    fi
    effective_timeout="$remaining_seconds"
    timeout_kind="runtime"
  fi
  if [[ "$timeout_seconds" != "0" &&
        ( "$effective_timeout" == "0" ||
          "$timeout_seconds" -lt "$effective_timeout" ) ]]; then
    effective_timeout="$timeout_seconds"
    timeout_kind="timeout"
  fi
  set +e
  if [[ "$effective_timeout" == "0" ]]; then
    "$@" > "$output" 2> "$stderr_file"
    status="$?"
  else
    python3 - "$effective_timeout" "$output" "$stderr_file" "$@" <<'PY'
import subprocess
import sys

timeout = int(sys.argv[1])
stdout_path = sys.argv[2]
stderr_path = sys.argv[3]
cmd = sys.argv[4:]

try:
    with open(stdout_path, "wb") as stdout, open(stderr_path, "wb") as stderr:
        completed = subprocess.run(
            cmd, stdout=stdout, stderr=stderr, timeout=timeout
        )
except subprocess.TimeoutExpired:
    raise SystemExit(124)

raise SystemExit(completed.returncode)
PY
    status="$?"
  fi
  set -e
  if [[ "$status" == "124" ]]; then
    record_runtime_budget_for_run "$label" "$output"
    if [[ "$timeout_kind" == "runtime" ]]; then
      write_status "K26_FULL_RUN_BUNDLE_BLOCKED_${label}_RUNTIME_LIMIT"
      echo "$label exceeded K26 bundle max runtime ${max_runtime_seconds}s" >&2
    else
      write_status "K26_FULL_RUN_BUNDLE_BLOCKED_${label}_TIMEOUT"
      echo "$label timed out after ${timeout_seconds}s" >&2
    fi
    exit 124
  fi
  if [[ "$status" != "0" ]]; then
    record_runtime_budget_for_run "$label" "$output"
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_${label}_FAILED"
    cat "$stderr_file" >&2
    exit 1
  fi
}

json_string_value() {
  local path="$1"
  local field="$2"
  sed -nE "s/.*\"${field}\":\"([^\"]+)\".*/\\1/p" "$path" | head -n 1
}

json_number_value() {
  local path="$1"
  local field="$2"
  sed -nE "s/.*\"${field}\":([0-9]+).*/\\1/p" "$path" | head -n 1
}

json_bool_value() {
  local path="$1"
  local field="$2"
  sed -nE "s/.*\"${field}\":(true|false).*/\\1/p" "$path" | head -n 1
}

json_bool_or_null() {
  local value="$1"
  if [[ "$value" == "true" || "$value" == "false" ]]; then
    printf '%s\n' "$value"
  else
    printf 'null\n'
  fi
}

json_number_or_null() {
  local value="$1"
  if [[ "$value" =~ ^[0-9]+$ ]]; then
    printf '%s\n' "$value"
  else
    printf 'null\n'
  fi
}

json_array_value() {
  local path="$1"
  local field="$2"
  sed -nE "s/.*\"${field}\":(\[[0-9, -]*\]).*/\\1/p" "$path" | head -n 1
}

join_csv_range() {
  local begin="$1"
  local end="$2"
  local out="" i
  for ((i = begin; i <= end; ++i)); do
    if [[ -n "$out" ]]; then
      out+=","
    fi
    out+="${radii[$i]}"
  done
  printf '%s\n' "$out"
}

append_file_if_exists() {
  local src="$1"
  local dst="$2"
  if [[ -f "$src" ]]; then
    cat "$src" >> "$dst"
  fi
}

artifact_basename_or_empty() {
  local path="$1"
  if [[ -n "$path" ]]; then
    basename "$path"
  fi
}

append_chunk_ledger() {
  local chunk_id="$1"
  local chunk_index="$2"
  local action="$3"
  local chunk_start="$4"
  local chunk_end="$5"
  local chunk_r_start="$6"
  local chunk_r_final="$7"
  local chunk_csv="$8"
  local final_chunk="$9"
  local input_manifest="${10}"
  local output_manifest="${11}"
  local chunk_json="${12}"
  local chunk_progress="${13}"
  local terminal_dead="${14}"
  local has_live_source="${15}"
  local source_carry_atoms="${16}"
  local input_name output_name result_name progress_name final_bool
  input_name="$(artifact_basename_or_empty "$input_manifest")"
  output_name="$(artifact_basename_or_empty "$output_manifest")"
  result_name="$(artifact_basename_or_empty "$chunk_json")"
  progress_name="$(artifact_basename_or_empty "$chunk_progress")"
  if [[ "$final_chunk" == "1" ]]; then
    final_bool="true"
  else
    final_bool="false"
  fi
  terminal_dead="$(json_bool_or_null "$terminal_dead")"
  has_live_source="$(json_bool_or_null "$has_live_source")"
  source_carry_atoms="$(json_number_or_null "$source_carry_atoms")"
  printf '{"schema":"lb_source_k26_continuation_chunk_v1","chunk_index":%s,"chunk_id":"%s","action":"%s","schedule_segment_start":%s,"schedule_segment_end":%s,"schedule_segment_count":%s,"r_start":%s,"r_final":%s,"schedule_radii_csv":"%s","input_manifest":"%s","output_manifest":"%s","result":"%s","progress":"%s","final_chunk":%s,"terminal_source_dead":%s,"has_source_carry":%s,"source_carry_atoms":%s}\n' \
    "$chunk_index" "$chunk_id" "$action" "$chunk_start" "$chunk_end" \
    "$((chunk_end - chunk_start))" "$chunk_r_start" "$chunk_r_final" \
    "$chunk_csv" "$input_name" "$output_name" "$result_name" \
    "$progress_name" "$final_bool" "$terminal_dead" "$has_live_source" \
    "$source_carry_atoms" >> "$chunk_ledger"
}

prefix_resume_ready() {
  [[ "$resume_existing" == "1" &&
     -f "$out_dir/k26-prefix-result.json" &&
     -f "$out_dir/k26-prefix-progress.jsonl" &&
     -f "$out_dir/k26-prefix-manifest.txt" &&
     -f "$out_dir/k26-prefix-witness.txt" ]]
}

chunk_resume_ready() {
  local chunk_json="$1"
  local chunk_progress="$2"
  local chunk_manifest="$3"
  local final_chunk="$4"
  if [[ "$resume_existing" != "1" ||
        ! -f "$chunk_json" ||
        ! -f "$chunk_progress" ]]; then
    return 1
  fi
  if [[ "$final_chunk" == "1" ]]; then
    return 0
  fi
  if [[ ! -f "$chunk_manifest" ]]; then
    return 1
  fi
  local terminal_dead has_live_source
  terminal_dead="$(json_bool_value "$chunk_json" terminal_source_dead)"
  has_live_source="$(json_bool_value "$chunk_json" has_source_carry)"
  [[ "$terminal_dead" == "false" && "$has_live_source" == "true" ]]
}

run_k26_continuation() {
  if [[ "$continuation_chunk_bands" == "0" ]]; then
    run_json K26_CONTINUATION "$out_dir/k26-continuation-result.json" \
      "$build_dir/source_tileop_port_runner" \
        --r-start 8192 \
        --r-final 1015645 \
        --band-width 8192 \
        --schedule-radii "$schedule_csv" \
        --max-atoms "$max_atoms" \
        --tileop-threads "$tileop_threads" \
        --target-a 376039 \
        --target-b 943460 \
        --manifest-in "$out_dir/k26-prefix-manifest.txt" \
        --prefix-witness-in "$out_dir/k26-prefix-witness.txt" \
        --progress-out "$out_dir/k26-continuation-progress.jsonl"
    return
  fi

  local radii=()
  IFS=',' read -r -a radii <<< "$schedule_csv"
  local boundary_count="${#radii[@]}"
  if [[ "$boundary_count" -lt 2 ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_CHUNK_SCHEDULE_MALFORMED"
    echo "chunked continuation requires at least two schedule radii" >&2
    exit 1
  fi

  local segment_count=$((boundary_count - 1))
  local chunk_size="$continuation_chunk_bands"
  if (( chunk_size < 1 )); then
    echo "--continuation-chunk-bands must be positive when chunking is enabled" >&2
    exit 2
  fi

  : > "$out_dir/k26-continuation-progress.jsonl"
  : > "$chunk_ledger"
  local chunk_start=0
  local chunk_index=0
  local manifest_in="$out_dir/k26-prefix-manifest.txt"
  local chunk_id chunk_end chunk_r_start chunk_r_final chunk_csv
  local chunk_json chunk_progress chunk_manifest
  local terminal_dead has_live_source source_carry_atoms
  while (( chunk_start < segment_count )); do
    chunk_end=$((chunk_start + chunk_size))
    if (( chunk_end > segment_count )); then
      chunk_end="$segment_count"
    fi
    chunk_r_start="${radii[$chunk_start]}"
    chunk_r_final="${radii[$chunk_end]}"
    chunk_csv="$(join_csv_range "$chunk_start" "$chunk_end")"
    chunk_id="$(printf '%03d' "$chunk_index")"
    chunk_json="$out_dir/k26-continuation-chunk-${chunk_id}.json"
    chunk_progress="$out_dir/k26-continuation-chunk-${chunk_id}.progress.jsonl"
    chunk_manifest="$out_dir/k26-continuation-chunk-${chunk_id}.manifest.txt"
    local final_chunk=0
    if (( chunk_end >= segment_count )); then
      final_chunk=1
    fi

    local action chunk_input_manifest chunk_output_manifest
    action="executed"
    chunk_input_manifest="$manifest_in"
    chunk_output_manifest=""
    if (( chunk_end < segment_count )); then
      chunk_output_manifest="$chunk_manifest"
    fi

    if chunk_resume_ready "$chunk_json" "$chunk_progress" "$chunk_manifest" \
        "$final_chunk"; then
      echo "SKIP K26_CONTINUATION_CHUNK_${chunk_id}: existing complete chunk" \
        >> "$out_dir/run.log"
      action="reused"
    else
      local args=(
        "$build_dir/source_tileop_port_runner"
        --r-start "$chunk_r_start"
        --r-final "$chunk_r_final"
        --band-width 8192
        --schedule-radii "$chunk_csv"
        --max-atoms "$max_atoms"
        --tileop-threads "$tileop_threads"
        --target-a 376039
        --target-b 943460
        --manifest-in "$manifest_in"
        --progress-out "$chunk_progress"
      )
      if (( chunk_index == 0 )); then
        args+=(--prefix-witness-in "$out_dir/k26-prefix-witness.txt")
      fi
      if (( chunk_end < segment_count )); then
        args+=(--manifest-out "$chunk_manifest")
      fi

      run_json "K26_CONTINUATION_CHUNK_${chunk_id}" "$chunk_json" "${args[@]}"
    fi

    append_file_if_exists "$chunk_progress" "$out_dir/k26-continuation-progress.jsonl"

    terminal_dead="$(json_bool_value "$chunk_json" terminal_source_dead)"
    require_extracted "$terminal_dead" "CHUNK_${chunk_id}_TERMINAL_SOURCE_DEAD"
    has_live_source="$(json_bool_value "$chunk_json" has_source_carry)"
    source_carry_atoms="$(json_number_value "$chunk_json" source_carry_atoms)"
    if (( chunk_end < segment_count )); then
      require_extracted "$has_live_source" "CHUNK_${chunk_id}_HAS_SOURCE_CARRY"
      if [[ "$terminal_dead" == "true" || "$has_live_source" != "true" ]]; then
        write_status "K26_FULL_RUN_BUNDLE_BLOCKED_CHUNK_${chunk_id}_NOT_RESUMABLE"
        echo "K26 continuation chunk ${chunk_id} did not leave live source carry for resume" >&2
        exit 1
      fi
      if [[ ! -f "$chunk_manifest" ]]; then
        write_status "K26_FULL_RUN_BUNDLE_BLOCKED_CHUNK_${chunk_id}_MANIFEST_MISSING"
        echo "K26 continuation chunk ${chunk_id} did not write resume manifest" >&2
        exit 1
      fi
      manifest_in="$chunk_manifest"
    else
      cp "$chunk_json" "$out_dir/k26-continuation-result.json"
    fi

    append_chunk_ledger "$chunk_id" "$chunk_index" "$action" \
      "$chunk_start" "$chunk_end" "$chunk_r_start" "$chunk_r_final" \
      "$chunk_csv" "$final_chunk" "$chunk_input_manifest" \
      "$chunk_output_manifest" "$chunk_json" "$chunk_progress" \
      "$terminal_dead" "$has_live_source" "$source_carry_atoms"
    enforce_chunk_runtime_budget "$chunk_id"

    chunk_start="$chunk_end"
    chunk_index=$((chunk_index + 1))
  done
}

atom_path_kind_counts() {
  local atom_path="$1"
  local body token coordinate_count=0 port_count=0
  body="${atom_path#[}"
  body="${body%]}"
  if [[ -z "$body" ]]; then
    printf '0 0\n'
    return
  fi
  IFS=',' read -r -a tokens <<< "$body"
  for token in "${tokens[@]}"; do
    token="${token//[[:space:]]/}"
    [[ -z "$token" ]] && continue
    if [[ "$token" == -* ]]; then
      port_count=$((port_count + 1))
    else
      coordinate_count=$((coordinate_count + 1))
    fi
  done
  printf '%s %s\n' "$coordinate_count" "$port_count"
}

first_coordinate_atom_id() {
  local atom_path="$1"
  local body token
  body="${atom_path#[}"
  body="${body%]}"
  if [[ -z "$body" ]]; then
    return
  fi
  IFS=',' read -r -a tokens <<< "$body"
  for token in "${tokens[@]}"; do
    token="${token//[[:space:]]/}"
    [[ -z "$token" ]] && continue
    if [[ "$token" != -* ]]; then
      printf '%s\n' "$token"
      return
    fi
  done
}

require_extracted() {
  local value="$1"
  local label="$2"
  if [[ -z "$value" ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_GAP_${label}_MISSING"
    echo "could not extract ${label} from k26-continuation-result.json" >&2
    exit 1
  fi
}

write_source_dead_gap() {
  local continuation="$out_dir/k26-continuation-result.json"
  local bridge_source="$continuation"
  if [[ -f "$chunk_ledger" ]]; then
    bridge_source="$out_dir/k26-continuation-chunk-000.json"
  fi
  local continuation_digest
  continuation_digest="$(shasum -a 256 "$continuation" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  require_extracted "$continuation_digest" "CONTINUATION_HASH"
  if [[ ! -f "$bridge_source" ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_GAP_BRIDGE_SOURCE_MISSING"
    echo "could not find bridge-source continuation artifact: $bridge_source" >&2
    exit 1
  fi

  local prefix_manifest="$out_dir/k26-prefix-manifest.txt"
  local prefix_witness="$out_dir/k26-prefix-witness.txt"
  if [[ ! -f "$prefix_manifest" ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_GAP_PREFIX_MANIFEST_MISSING"
    echo "could not find prefix manifest artifact: $prefix_manifest" >&2
    exit 1
  fi
  if [[ ! -f "$prefix_witness" ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_GAP_PREFIX_WITNESS_MISSING"
    echo "could not find prefix witness artifact: $prefix_witness" >&2
    exit 1
  fi

  local prefix_manifest_digest prefix_witness_digest
  prefix_manifest_digest="$(shasum -a 256 "$prefix_manifest" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  prefix_witness_digest="$(shasum -a 256 "$prefix_witness" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  require_extracted "$prefix_manifest_digest" "PREFIX_MANIFEST_HASH"
  require_extracted "$prefix_witness_digest" "PREFIX_WITNESS_HASH"

  local path_provenance atom_path atom_path_length inventory_count
  local inventory_digest max_norm max_ties bz_status bz_digest_algorithm
  local bz_digest_hex
  local chunk_ledger_artifact_json=""
  local bridge_artifact_json=""
  local seam_bridge_policy source_with_next_candidates source_bridged
  local source_unbridged source_unbridged_without source_unbridged_with
  local source_dead_end source_unsafe source_bridge_rejected
  local atom_path_counts coordinate_atom_count port_atom_count
  local prefix_atom_id prefix_accepted_json
  local target_seen_json target_source_reached_json
  local target_port_atoms target_bridge_edges target_values
  local target_prefix_path_available_json target_prefix_path_atom_id_json
  local target_prefix_path_points target_prefix_path_seed_norm_sq
  local target_prefix_path_target_norm_sq
  local target_port_expansion_required_edges target_port_expansion_available_edges
  local target_port_expansion_path_points_total
  path_provenance="$(json_string_value "$continuation" path_provenance)"
  atom_path="$(json_array_value "$continuation" atom_path)"
  atom_path_length="$(json_number_value "$continuation" atom_path_length)"
  inventory_count="$(json_number_value "$continuation" source_inventory_count)"
  inventory_digest="$(json_string_value "$continuation" source_inventory_digest_hex)"
  max_norm="$(json_number_value "$continuation" max_source_norm_sq)"
  max_ties="$(json_array_value "$continuation" max_source_norm_atom_ids)"
  seam_bridge_policy="$(json_string_value "$bridge_source" seam_bridge_policy)"
  source_with_next_candidates="$(json_number_value "$bridge_source" source_coordinate_carry_atoms_with_next_band_candidates)"
  source_bridged="$(json_number_value "$bridge_source" source_bridged_coordinate_carry_atoms)"
  source_unbridged="$(json_number_value "$bridge_source" source_unbridged_coordinate_carry_atoms)"
  source_unbridged_without="$(json_number_value "$bridge_source" source_unbridged_without_next_band_candidates)"
  source_unbridged_with="$(json_number_value "$bridge_source" source_unbridged_with_next_band_candidates)"
  source_dead_end="$(json_number_value "$bridge_source" source_unbridged_dead_end_candidate_atoms)"
  source_unsafe="$(json_number_value "$bridge_source" source_unbridged_unsafe_candidate_atoms)"
  source_bridge_rejected="$(json_number_value "$bridge_source" source_bridge_rejected_candidate_atoms)"
  bz_status="$(json_string_value "$out_dir/k26_bz_schedule_check.json" proof_status)"
  bz_digest_algorithm="$(json_string_value "$out_dir/k26_bz_schedule_check.json" schedule_digest_algorithm)"
  bz_digest_hex="$(json_string_value "$out_dir/k26_bz_schedule_check.json" schedule_digest_hex)"
  target_values="$(
    python3 - "$continuation" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    target = json.load(fh).get("target", {})
prefix_path = target.get("prefix_witness_path", {})
if not isinstance(prefix_path, dict):
    prefix_path = {}
port_expansions = target.get("coordinate_port_expansions", {})
if not isinstance(port_expansions, dict):
    port_expansions = {}

def b(value):
    return "true" if value is True else "false"

def n(value):
    return "null" if value is None else str(int(value))

print(
    b(target.get("seen")),
    b(target.get("source_reached")),
    int(target.get("port_atoms", 0)),
    int(target.get("bridge_edges", 0)),
    b(prefix_path.get("available")),
    n(prefix_path.get("target_atom_id")),
    int(prefix_path.get("path_points", 0)),
    int(prefix_path.get("seed_norm_sq", 0)),
    int(prefix_path.get("target_norm_sq", 0)),
    int(port_expansions.get("required_edges", 0)),
    int(port_expansions.get("available_edges", 0)),
    int(port_expansions.get("path_points_total", 0)),
)
PY
  )"
  read -r target_seen_json target_source_reached_json \
    target_port_atoms target_bridge_edges target_prefix_path_available_json \
    target_prefix_path_atom_id_json target_prefix_path_points \
    target_prefix_path_seed_norm_sq target_prefix_path_target_norm_sq \
    target_port_expansion_required_edges target_port_expansion_available_edges \
    target_port_expansion_path_points_total \
    <<< "$target_values"
  require_extracted "$path_provenance" "PATH_PROVENANCE"
  require_extracted "$atom_path" "ATOM_PATH"
  require_extracted "$atom_path_length" "ATOM_PATH_LENGTH"
  atom_path_counts="$(atom_path_kind_counts "$atom_path")"
  coordinate_atom_count="${atom_path_counts%% *}"
  port_atom_count="${atom_path_counts##* }"
  prefix_atom_id="$(first_coordinate_atom_id "$atom_path")"
  prefix_accepted_json="false"
  if [[ -n "$prefix_atom_id" ]]; then
    if ! grep -Fq "witness ${prefix_atom_id} " "$prefix_witness"; then
      write_status "K26_FULL_RUN_BUNDLE_BLOCKED_GAP_PREFIX_WITNESS_TARGET_MISSING"
      echo "prefix witness does not contain mixed-path source atom: $prefix_atom_id" >&2
      exit 1
    fi
    prefix_accepted_json="true"
  fi
  require_extracted "$coordinate_atom_count" "COORDINATE_ATOM_COUNT"
  require_extracted "$port_atom_count" "PORT_ATOM_COUNT"
  require_extracted "$inventory_count" "INVENTORY_COUNT"
  require_extracted "$inventory_digest" "INVENTORY_DIGEST"
  require_extracted "$max_norm" "MAX_SOURCE_NORM"
  require_extracted "$max_ties" "MAX_SOURCE_NORM_TIES"

  local terminal_accumulator_json accumulator_check_err accumulator_check_status
  accumulator_check_err="$out_dir/k26-terminal-accumulator-check.err"
  set +e
  terminal_accumulator_json="$(
    python3 - "$continuation" "$inventory_count" "$inventory_digest" "$max_norm" "$max_ties" \
      2> "$accumulator_check_err" <<'PY'
import json
import sys

path, expected_count, expected_digest, expected_max_norm, expected_ties = sys.argv[1:]
with open(path, "r", encoding="utf-8") as fh:
    doc = json.load(fh)

accumulator = doc.get("terminal_source_inventory_accumulator")
if not isinstance(accumulator, dict):
    print("terminal continuation missing terminal_source_inventory_accumulator", file=sys.stderr)
    raise SystemExit(10)

try:
    expected_ties_value = json.loads(expected_ties)
except json.JSONDecodeError as exc:
    print(f"invalid expected max_norm_atom_ids JSON: {exc}", file=sys.stderr)
    raise SystemExit(11)

expected = {
    "mode": "summary_digest_only_non_claim",
    "provenance": "terminal_component_inventory_accumulator",
    "listed_inventory_present": False,
    "claim_grade_inventory_accepted": False,
    "count": int(expected_count),
    "digest_algorithm": "sha256:lb_source_inventory_v1",
    "digest_hex": expected_digest,
    "max_norm_sq": int(expected_max_norm),
    "max_norm_atom_ids": expected_ties_value,
}

for key, value in expected.items():
    if accumulator.get(key) != value:
        print(
            "terminal_source_inventory_accumulator "
            f"{key} mismatch: expected {value!r}, got {accumulator.get(key)!r}",
            file=sys.stderr,
        )
        raise SystemExit(11)

print(json.dumps(accumulator, separators=(",", ":")))
PY
  )"
  accumulator_check_status=$?
  set -e
  if [[ "$accumulator_check_status" -eq 10 ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_TERMINAL_ACCUMULATOR_MISSING"
    cat "$accumulator_check_err" >&2
    exit 1
  fi
  if [[ "$accumulator_check_status" -ne 0 ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_TERMINAL_ACCUMULATOR_MISMATCH"
    cat "$accumulator_check_err" >&2
    exit 1
  fi
  require_extracted "$terminal_accumulator_json" "TERMINAL_ACCUMULATOR"

  require_extracted "$seam_bridge_policy" "SEAM_BRIDGE_POLICY"
  require_extracted "$source_with_next_candidates" "SOURCE_CARRY_WITH_NEXT_BAND_CANDIDATES"
  require_extracted "$source_bridged" "SOURCE_BRIDGED_CARRY"
  require_extracted "$source_unbridged" "SOURCE_UNBRIDGED_CARRY"
  require_extracted "$source_unbridged_without" "SOURCE_UNBRIDGED_WITHOUT_CANDIDATES"
  require_extracted "$source_unbridged_with" "SOURCE_UNBRIDGED_WITH_CANDIDATES"
  require_extracted "$source_dead_end" "SOURCE_UNBRIDGED_DEAD_END"
  require_extracted "$source_unsafe" "SOURCE_UNBRIDGED_UNSAFE"
  require_extracted "$source_bridge_rejected" "SOURCE_BRIDGE_REJECTED"
  require_extracted "$bz_status" "BZ_STATUS"
  require_extracted "$bz_digest_algorithm" "BZ_DIGEST_ALGORITHM"
  require_extracted "$bz_digest_hex" "BZ_DIGEST_HEX"
  require_extracted "$target_seen_json" "TARGET_SEEN"
  require_extracted "$target_source_reached_json" "TARGET_SOURCE_REACHED"
  require_extracted "$target_port_atoms" "TARGET_PORT_ATOMS"
  require_extracted "$target_bridge_edges" "TARGET_BRIDGE_EDGES"
  require_extracted "$target_prefix_path_available_json" "TARGET_PREFIX_PATH_AVAILABLE"
  require_extracted "$target_prefix_path_atom_id_json" "TARGET_PREFIX_PATH_ATOM_ID"
  require_extracted "$target_prefix_path_points" "TARGET_PREFIX_PATH_POINTS"
  require_extracted "$target_prefix_path_seed_norm_sq" "TARGET_PREFIX_PATH_SEED_NORM"
  require_extracted "$target_prefix_path_target_norm_sq" "TARGET_PREFIX_PATH_TARGET_NORM"
  require_extracted "$target_port_expansion_required_edges" "TARGET_PORT_EXPANSION_REQUIRED_EDGES"
  require_extracted "$target_port_expansion_available_edges" "TARGET_PORT_EXPANSION_AVAILABLE_EDGES"
  require_extracted "$target_port_expansion_path_points_total" "TARGET_PORT_EXPANSION_PATH_POINTS_TOTAL"
  if [[ -f "$chunk_ledger" ]]; then
    local chunk_ledger_digest
    local bridge_source_digest
    chunk_ledger_digest="$(shasum -a 256 "$chunk_ledger" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
    bridge_source_digest="$(shasum -a 256 "$bridge_source" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
    require_extracted "$chunk_ledger_digest" "CHUNK_LEDGER_HASH"
    require_extracted "$bridge_source_digest" "BRIDGE_SOURCE_HASH"
    chunk_ledger_artifact_json=",\"chunk_ledger_artifact\":{\"name\":\"k26-continuation-chunks.jsonl\",\"sha256\":\"$chunk_ledger_digest\"}"
    bridge_artifact_json=",\"bridge_source_artifact\":{\"name\":\"k26-continuation-chunk-000.json\",\"sha256\":\"$bridge_source_digest\"}"
  fi

  local blocker missing_for_cert per_port_expansion_status
  blocker="SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING"
  per_port_expansion_status="available_summary_non_claim"
  missing_for_cert='"coordinate Gaussian-prime source_path verifier acceptance from origin prefix to canonical endpoint","claim-grade terminal inventory listing or independently checkable accumulator","claim-grade BZ schedule acceptance","claim-grade verifier binding the coordinate path to terminal inventory and BZ schedule"'
  if [[ "$path_provenance" == "component_reachability_only" &&
        "$atom_path_length" == "0" ]]; then
    blocker="SOURCE_DEAD_CERT_TARGET_NOT_REACHED"
    per_port_expansion_status="not_applicable"
    missing_for_cert='"positive target reachability to canonical Tsuchimura endpoint","coordinate Gaussian-prime source_path from origin prefix to canonical endpoint","claim-grade verifier binding the coordinate path to terminal inventory and BZ schedule"'
  fi

  cat > "$source_dead_gap" <<JSON
{"schema":"lb_source_k26_source_dead_gap_v1","claim_label":"SOURCE_ORIGIN_K26","proof_status":"DIAGNOSTIC_NON_CLAIM","blocker":"$blocker","non_claim":"executed prefix and continuation evidence only; not a SOURCE_DEAD_CERT","k_sq":26,"terminal_radius":1015645,"target":{"tsuchimura_endpoint":{"a":943460,"b":376039,"norm_sq":1031522101121},"canonical_octant_endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121}},"prefix_manifest_artifact":{"name":"k26-prefix-manifest.txt","sha256":"$prefix_manifest_digest"},"prefix_witness_artifact":{"name":"k26-prefix-witness.txt","sha256":"$prefix_witness_digest"},"continuation_artifact":{"name":"k26-continuation-result.json","sha256":"$continuation_digest"}$chunk_ledger_artifact_json$bridge_artifact_json,"bz_evidence":{"status":"$bz_status","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"$bz_digest_algorithm","schedule_digest_hex":"$bz_digest_hex"},"bz_schedule_obligation":{"required_status":"claim_grade_bz_schedule","observed_status":"$bz_status","observed_schedule_digest_algorithm":"$bz_digest_algorithm","observed_schedule_digest_hex":"$bz_digest_hex","accepted_for_schedule":true,"accepted_for_claim":false,"claim_grade_bz_accepted":false},"bridge_safety":{"seam_bridge_policy":"$seam_bridge_policy","source_coordinate_carry_atoms_with_next_band_candidates":$source_with_next_candidates,"source_bridged_coordinate_carry_atoms":$source_bridged,"source_unbridged_coordinate_carry_atoms":$source_unbridged,"source_unbridged_without_next_band_candidates":$source_unbridged_without,"source_unbridged_with_next_band_candidates":$source_unbridged_with,"source_unbridged_dead_end_candidate_atoms":$source_dead_end,"source_unbridged_unsafe_candidate_atoms":$source_unsafe,"source_bridge_rejected_candidate_atoms":$source_bridge_rejected},"target_path_provenance":"$path_provenance","target_atom_path_length":$atom_path_length,"target_atom_path":$atom_path,"coordinate_path_obligation":{"required_provenance":"coordinate_gaussian_prime_path","observed_provenance":"$path_provenance","observed_coordinate_atom_count":$coordinate_atom_count,"observed_port_atom_count":$port_atom_count,"origin_prefix_witness_artifact":"k26-prefix-witness.txt","origin_prefix_witness_target_atom_id":${prefix_atom_id:-null},"origin_prefix_witness_accepted":$prefix_accepted_json,"origin_prefix_witness_path_available":$target_prefix_path_available_json,"origin_prefix_witness_path_target_atom_id":$target_prefix_path_atom_id_json,"origin_prefix_witness_path_points":$target_prefix_path_points,"origin_prefix_witness_seed_norm_sq":$target_prefix_path_seed_norm_sq,"origin_prefix_witness_path_target_norm_sq":$target_prefix_path_target_norm_sq,"per_port_coordinate_expansion":"$per_port_expansion_status","per_port_coordinate_expansion_required_edges":$target_port_expansion_required_edges,"per_port_coordinate_expansion_available_edges":$target_port_expansion_available_edges,"per_port_coordinate_expansion_path_points_total":$target_port_expansion_path_points_total,"claim_grade_path_accepted":false},"target_bridge_obligation":{"endpoint_atom_id":1615075207964004,"observed_target_seen":$target_seen_json,"observed_target_source_reached":$target_source_reached_json,"observed_target_port_atoms":$target_port_atoms,"observed_target_bridge_edges":$target_bridge_edges,"endpoint_bridge_accepted":$target_source_reached_json},"terminal_source_inventory_summary":{"count":$inventory_count,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"$inventory_digest","max_norm_sq":$max_norm,"max_norm_atom_ids":$max_ties},"terminal_source_inventory_accumulator":$terminal_accumulator_json,"terminal_inventory_obligation":{"required_mode":"claim_grade_terminal_inventory","observed_mode":"summary_digest_only_non_claim","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"observed_count":$inventory_count,"observed_digest_algorithm":"sha256:lb_source_inventory_v1","observed_digest_hex":"$inventory_digest","observed_max_norm_sq":$max_norm},"missing_for_source_dead_cert":[$missing_for_cert]}
JSON
}

write_summary_source_dead_cert() {
  local cert_tmp="$out_dir/k26-source-dead-cert.json.tmp"
  local cert_err="$out_dir/k26-source-dead-cert-generation.err"
  set +e
  python3 - \
    "$out_dir/k26-continuation-result.json" \
    "$out_dir/k26-prefix-witness.txt" \
    "$out_dir/k26_source_run_profile.json" \
    "$out_dir/k26_bz_schedule_check.json" \
    > "$cert_tmp" 2> "$cert_err" <<'PY'
import json
import sys

continuation_path, witness_path, profile_path, bz_path = sys.argv[1:]

with open(continuation_path, "r", encoding="utf-8") as fh:
    continuation = json.load(fh)
with open(profile_path, "r", encoding="utf-8") as fh:
    profile = json.load(fh)
with open(bz_path, "r", encoding="utf-8") as fh:
    bz = json.load(fh)

target = continuation.get("target")
if not isinstance(target, dict):
    raise SystemExit("continuation target object is missing")
if target.get("source_reached") is not True:
    raise SystemExit("target is not source-reached")
atom_path = target.get("atom_path")
if not isinstance(atom_path, list) or not atom_path:
    raise SystemExit("target atom_path is missing")

first_coordinate_atom = next((item for item in atom_path if isinstance(item, int) and item >= 0), None)
if first_coordinate_atom is None:
    raise SystemExit("target atom_path has no coordinate atom")

def parse_prefix_witness(path, wanted_atom):
    current = None
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            parts = raw.strip().split()
            if not parts:
                continue
            if parts[0] == "witness":
                if current is not None:
                    break
                if len(parts) != 6:
                    raise SystemExit("malformed prefix witness row")
                atom_id = int(parts[1])
                if atom_id == wanted_atom:
                    current = {
                        "atom_id": atom_id,
                        "a": int(parts[2]),
                        "b": int(parts[3]),
                        "norm_sq": int(parts[4]),
                        "expected_points": int(parts[5]),
                        "points": [],
                    }
            elif parts[0] == "point" and current is not None:
                if len(parts) != 4:
                    raise SystemExit("malformed prefix witness point")
                current["points"].append(
                    {"a": int(parts[1]), "b": int(parts[2]), "norm_sq": int(parts[3])}
                )
            elif parts[0] == "END" and current is not None:
                break
    if current is None:
        raise SystemExit(f"prefix witness omits target atom {wanted_atom}")
    if len(current["points"]) != current["expected_points"]:
        raise SystemExit("prefix witness point count mismatch")
    if not current["points"]:
        raise SystemExit("prefix witness path is empty")
    last = current["points"][-1]
    if (last["a"], last["b"], last["norm_sq"]) != (
        current["a"],
        current["b"],
        current["norm_sq"],
    ):
        raise SystemExit("prefix witness path does not end at witness atom")
    return current["points"]

source_path = parse_prefix_witness(witness_path, first_coordinate_atom)

port_expansions = target.get("coordinate_port_expansions")
if not isinstance(port_expansions, dict):
    raise SystemExit("coordinate_port_expansions object is missing")
expansion_rows = port_expansions.get("expansions")
if not isinstance(expansion_rows, list):
    raise SystemExit("coordinate_port_expansions.expansions is missing")

expansions = {}
for row in expansion_rows:
    if not isinstance(row, dict):
        raise SystemExit("coordinate-port expansion row is not object")
    coordinate_atom_id = row.get("coordinate_atom_id")
    port_atom_id = row.get("port_atom_id")
    path = row.get("path")
    if not isinstance(coordinate_atom_id, int) or coordinate_atom_id < 0:
        raise SystemExit("coordinate-port expansion has invalid coordinate atom")
    if not isinstance(port_atom_id, int) or port_atom_id >= 0:
        raise SystemExit("coordinate-port expansion has invalid port atom")
    if not isinstance(path, list) or not path:
        raise SystemExit(
            f"coordinate-port expansion {coordinate_atom_id}->{port_atom_id} has no path"
        )
    if row.get("path_points") != len(path):
        raise SystemExit(
            f"coordinate-port expansion {coordinate_atom_id}->{port_atom_id} path_points mismatch"
        )
    normalized = []
    for point in path:
        if not isinstance(point, dict):
            raise SystemExit("coordinate-port expansion point is not object")
        normalized.append(
            {
                "a": int(point["a"]),
                "b": int(point["b"]),
                "norm_sq": int(point["norm_sq"]),
            }
        )
    expansions[(coordinate_atom_id, port_atom_id)] = normalized

def append_points(points):
    for point in points:
        if source_path and source_path[-1] == point:
            continue
        source_path.append(point)

required_edges = 0
for previous, current in zip(atom_path, atom_path[1:]):
    if not isinstance(previous, int) or not isinstance(current, int):
        raise SystemExit("target atom_path contains non-integer atom")
    if previous >= 0 and current < 0:
        required_edges += 1
        key = (previous, current)
        if key not in expansions:
            raise SystemExit(f"missing coordinate-port path for {previous}->{current}")
        append_points(expansions[key])
    elif previous < 0 and current >= 0:
        required_edges += 1
        key = (current, previous)
        if key not in expansions:
            raise SystemExit(f"missing coordinate-port path for {previous}->{current}")
        append_points(reversed(expansions[key]))

if required_edges == 0:
    raise SystemExit("target atom_path has no coordinate-port edges")
if int(port_expansions.get("required_edges", -1)) != required_edges:
    raise SystemExit("coordinate-port required edge count disagrees with atom_path")
if int(port_expansions.get("available_edges", -1)) != required_edges:
    raise SystemExit("coordinate-port expansion evidence is incomplete")

endpoint = {"a": 376039, "b": 943460, "norm_sq": 1031522101121}
if source_path[-1] != endpoint:
    raise SystemExit("assembled source_path does not terminate at canonical endpoint")

inventory_summary = {
    "count": int(continuation["source_inventory_count"]),
    "digest_algorithm": "sha256:lb_source_inventory_v1",
    "digest_hex": continuation["source_inventory_digest_hex"],
    "max_norm_sq": int(continuation["max_source_norm_sq"]),
    "max_norm_atom_ids": continuation["max_source_norm_atom_ids"],
}
accumulator = continuation.get("terminal_source_inventory_accumulator")
if not isinstance(accumulator, dict):
    raise SystemExit("terminal_source_inventory_accumulator is missing")
for key, value in inventory_summary.items():
    if accumulator.get(key) != value:
        raise SystemExit(f"terminal accumulator {key} disagrees with summary")
if accumulator.get("mode") != "summary_digest_only_non_claim":
    raise SystemExit("terminal accumulator mode is not summary_digest_only_non_claim")
if accumulator.get("claim_grade_inventory_accepted") is not False:
    raise SystemExit("terminal accumulator unexpectedly claims grade")

continuation_digest = continuation.get("_sha256")
if continuation_digest is None:
    import hashlib

    with open(continuation_path, "rb") as fh:
        continuation_digest = hashlib.sha256(fh.read()).hexdigest()

cert = {
    "schema": "lb_source_dead_cert_draft_v1",
    "certificate_id": "k26-source-dead-cert-summary-nonclaim",
    "profile_id": profile.get("profile_id", "k26-source-run-profile"),
    "proof_status": "SUMMARY_ONLY_NON_CLAIM",
    "non_claim": "summary-only diagnostic inventory, not a SOURCE_DEAD_CERT",
    "terminal_source_inventory_mode": "summary_only_non_claim",
    "metadata": {
        "source_mode": "ORIGIN_SOURCE",
        "source_id": "omega",
        "geometry_id": "SOURCE_ORIGIN_K26",
        "commit_id": "lb-source-propagation-bundle",
        "build_id": "k26-source-bundle",
        "bz_status": bz.get("proof_status", "BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE"),
        "bz_schedule_digest_algorithm": bz.get(
            "schedule_digest_algorithm",
            "sha256:lb_source_k26_repaired_bz_schedule_v1",
        ),
        "bz_schedule_digest_hex": bz.get("schedule_digest_hex"),
        "artifact_hash": f"sha256:{continuation_digest}",
    },
    "k_sq": 26,
    "terminal_radius": 1015645,
    "negative_guard_pass": True,
    "endpoint": endpoint,
    "endpoint_atom_id": 1615075207964004,
    "source_path_provenance": "coordinate_gaussian_prime_path",
    "source_path": source_path,
    "terminal_source_inventory_summary": inventory_summary,
    "terminal_source_inventory_accumulator": accumulator,
}

json.dump(cert, sys.stdout, separators=(",", ":"))
sys.stdout.write("\n")
PY
  local cert_status=$?
  set -e
  if [[ "$cert_status" -ne 0 ]]; then
    rm -f "$cert_tmp"
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_GENERATION"
    cat "$cert_err" >&2
    exit 1
  fi
  mv "$cert_tmp" "$out_dir/k26-source-dead-cert.json"
}

run_json K26_COMMANDS "$out_dir/k26_source_run_commands.json" \
  "$build_dir/k26_source_run_commands"
run_json K26_BZ_SCHEDULE "$out_dir/k26_bz_schedule_check.json" \
  "$build_dir/k26_bz_schedule_check"
run_json K26_RUN_PROFILE "$out_dir/k26_source_run_profile.json" \
  "$build_dir/k26_source_run_profile"

schedule_csv="$(
  sed -n 's/.*"schedule_radii_csv":"\([^"]*\)".*/\1/p' \
    "$out_dir/k26_source_run_commands.json"
)"
if [[ -z "$schedule_csv" ]]; then
  write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SCHEDULE_CSV_MISSING"
  echo "could not extract schedule_radii_csv from k26_source_run_commands.json" >&2
  exit 1
fi
if [[ "$schedule_csv" != 8192,* || "$schedule_csv" != *,1015645 ]]; then
  write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SCHEDULE_ENDPOINT_MISMATCH"
  echo "K26 schedule does not start at 8192 and end at 1015645" >&2
  exit 1
fi

if prefix_resume_ready; then
  echo "SKIP K26_PREFIX: existing prefix artifacts" >> "$out_dir/run.log"
else
  run_json K26_PREFIX "$out_dir/k26-prefix-result.json" \
    "$build_dir/source_origin_cpu_runner" \
      --k-sq 26 \
      --r-final 8192 \
      --band-width 8192 \
      --endpoint-a 376039 \
      --endpoint-b 943460 \
      --max-atoms "$max_atoms" \
      --manifest-out "$out_dir/k26-prefix-manifest.txt" \
      --prefix-witness-out "$out_dir/k26-prefix-witness.txt" \
      --progress-out "$out_dir/k26-prefix-progress.jsonl"
fi

run_k26_continuation
record_runtime_budget_diagnostic \
  K26_CONTINUATION_COMPLETE "$out_dir/k26-continuation-progress.jsonl"

continuation_terminal_dead="$(
  json_bool_value "$out_dir/k26-continuation-result.json" terminal_source_dead
)"
require_extracted "$continuation_terminal_dead" "TERMINAL_SOURCE_DEAD"
if [[ "$continuation_terminal_dead" != "true" ]]; then
  write_artifact_manifest
  write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_STILL_LIVE"
  echo "K26 full-run prefix and continuation artifacts were produced, but source carry still survives at R_final." >&2
  echo "This is not a SOURCE_DEAD_CERT state; extend the terminal radius or inspect the live-source continuation." >&2
  exit 3
fi

write_source_dead_gap
check_source_dead_gap
write_artifact_manifest

gap_blocker="$(json_string_value "$source_dead_gap" blocker)"
require_extracted "$gap_blocker" "SOURCE_DEAD_GAP_BLOCKER"
if [[ "$gap_blocker" == "SOURCE_DEAD_CERT_TARGET_NOT_REACHED" ]]; then
  write_status "K26_FULL_RUN_BUNDLE_BLOCKED_TARGET_NOT_REACHED"
  echo "K26 full-run prefix and continuation artifacts were produced, and source death was reached, but the canonical Tsuchimura endpoint was not source-reached." >&2
  echo "This is not a SOURCE_DEAD_CERT state; inspect k26-source-dead-gap.json for target reachability evidence." >&2
  exit 3
fi

if [[ -z "$cert_in" && -n "$source_dead_checker" &&
      -n "$source_dead_gap_checker" ]]; then
  write_summary_source_dead_cert
  write_artifact_manifest
fi

if [[ -n "$cert_in" ]]; then
  if [[ ! -f "$cert_in" ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_CERT_IN_MISSING"
    echo "--cert-in file not found: $cert_in" >&2
    exit 2
  fi
  cp "$cert_in" "$out_dir/k26-source-dead-cert.json"
  write_artifact_manifest
fi

if [[ ! -f "$out_dir/k26-source-dead-cert.json" ]]; then
  write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING"
  echo "K26 full-run prefix and continuation artifacts were produced, but no k26-source-dead-cert.json exists." >&2
  echo "This is the expected blocker until SOURCE_DEAD_CERT emission/provenance is implemented." >&2
  exit 3
fi

if [[ -z "$source_dead_checker" ]]; then
  write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CHECKER_MISSING"
  echo "k26-source-dead-cert.json exists, but --source-dead-checker was not supplied" >&2
  exit 2
fi
if [[ -z "$source_dead_gap_checker" ]]; then
  write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_GAP_CHECKER_MISSING"
  echo "k26-source-dead-cert.json exists, but --source-dead-gap-checker was not supplied" >&2
  exit 2
fi

bundle_checker_args=(
  "$out_dir"
  --source-dead-checker "$source_dead_checker"
  --source-dead-gap-checker "$source_dead_gap_checker"
)
set +e
"$bundle_checker" "${bundle_checker_args[@]}" \
  > "$out_dir/k26-full-run-bundle-check.log" 2>&1
bundle_checker_status=$?
set -e
cat "$out_dir/k26-full-run-bundle-check.log"
if [[ "$bundle_checker_status" -ne 0 ]]; then
  if grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM' \
      "$out_dir/k26-full-run-bundle-check.log"; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM"
    exit 3
  fi
  write_status "K26_FULL_RUN_BUNDLE_BLOCKED_BUNDLE_CHECK_REJECTED"
  exit "$bundle_checker_status"
fi
write_status "K26_FULL_RUN_BUNDLE_CHECKED"
