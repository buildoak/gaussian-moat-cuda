#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  run_k26_full_source_bundle.sh --build-dir DIR --out-dir DIR
                                [--max-atoms N]
                                [--timeout-seconds N]
                                [--continuation-chunk-bands N]
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
terminal source death but no cert, it writes the source-dead gap and stops with
K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING.
USAGE
}

build_dir=""
out_dir=""
max_atoms="50000000"
timeout_seconds="0"
continuation_chunk_bands="0"
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
    --continuation-chunk-bands)
      continuation_chunk_bands="$2"
      shift 2
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

status_file="$out_dir/status.txt"
artifact_manifest="$out_dir/k26-full-run-artifacts.sha256"
source_dead_gap="$out_dir/k26-source-dead-gap.json"
write_status() {
  local status="$1"
  {
    echo "$status"
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "build_dir=$build_dir"
    echo "out_dir=$out_dir"
    echo "max_atoms=$max_atoms"
    echo "timeout_seconds=$timeout_seconds"
    echo "continuation_chunk_bands=$continuation_chunk_bands"
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
  local status
  echo "RUN $label: $*" >> "$out_dir/run.log"
  set +e
  if [[ "$timeout_seconds" == "0" ]]; then
    "$@" > "$output" 2> "$stderr_file"
    status="$?"
  else
    python3 - "$timeout_seconds" "$output" "$stderr_file" "$@" <<'PY'
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
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_${label}_TIMEOUT"
    echo "$label timed out after ${timeout_seconds}s" >&2
    exit 124
  fi
  if [[ "$status" != "0" ]]; then
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

run_k26_continuation() {
  if [[ "$continuation_chunk_bands" == "0" ]]; then
    run_json K26_CONTINUATION "$out_dir/k26-continuation-result.json" \
      "$build_dir/source_tileop_port_runner" \
        --r-start 8192 \
        --r-final 1015645 \
        --band-width 8192 \
        --schedule-radii "$schedule_csv" \
        --max-atoms "$max_atoms" \
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
  local chunk_start=0
  local chunk_index=0
  local manifest_in="$out_dir/k26-prefix-manifest.txt"
  local chunk_id chunk_end chunk_r_start chunk_r_final chunk_csv
  local chunk_json chunk_progress chunk_manifest
  local terminal_dead has_live_source
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

    local args=(
      "$build_dir/source_tileop_port_runner"
      --r-start "$chunk_r_start"
      --r-final "$chunk_r_final"
      --band-width 8192
      --schedule-radii "$chunk_csv"
      --max-atoms "$max_atoms"
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
    append_file_if_exists "$chunk_progress" "$out_dir/k26-continuation-progress.jsonl"

    terminal_dead="$(json_bool_value "$chunk_json" terminal_source_dead)"
    require_extracted "$terminal_dead" "CHUNK_${chunk_id}_TERMINAL_SOURCE_DEAD"
    if (( chunk_end < segment_count )); then
      has_live_source="$(json_bool_value "$chunk_json" has_source_carry)"
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
  local continuation_digest
  continuation_digest="$(shasum -a 256 "$continuation" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  require_extracted "$continuation_digest" "CONTINUATION_HASH"

  local path_provenance atom_path atom_path_length inventory_count
  local inventory_digest max_norm max_ties bz_status bz_digest_algorithm
  local bz_digest_hex
  local seam_bridge_policy source_bridged source_unbridged
  local source_unbridged_without source_unbridged_with source_dead_end
  local source_unsafe source_bridge_rejected
  local atom_path_counts coordinate_atom_count port_atom_count
  path_provenance="$(json_string_value "$continuation" path_provenance)"
  atom_path="$(json_array_value "$continuation" atom_path)"
  atom_path_length="$(json_number_value "$continuation" atom_path_length)"
  inventory_count="$(json_number_value "$continuation" source_inventory_count)"
  inventory_digest="$(json_string_value "$continuation" source_inventory_digest_hex)"
  max_norm="$(json_number_value "$continuation" max_source_norm_sq)"
  max_ties="$(json_array_value "$continuation" max_source_norm_atom_ids)"
  seam_bridge_policy="$(json_string_value "$continuation" seam_bridge_policy)"
  source_bridged="$(json_number_value "$continuation" source_bridged_coordinate_carry_atoms)"
  source_unbridged="$(json_number_value "$continuation" source_unbridged_coordinate_carry_atoms)"
  source_unbridged_without="$(json_number_value "$continuation" source_unbridged_without_next_band_candidates)"
  source_unbridged_with="$(json_number_value "$continuation" source_unbridged_with_next_band_candidates)"
  source_dead_end="$(json_number_value "$continuation" source_unbridged_dead_end_candidate_atoms)"
  source_unsafe="$(json_number_value "$continuation" source_unbridged_unsafe_candidate_atoms)"
  source_bridge_rejected="$(json_number_value "$continuation" source_bridge_rejected_candidate_atoms)"
  bz_status="$(json_string_value "$out_dir/k26_bz_schedule_check.json" proof_status)"
  bz_digest_algorithm="$(json_string_value "$out_dir/k26_bz_schedule_check.json" schedule_digest_algorithm)"
  bz_digest_hex="$(json_string_value "$out_dir/k26_bz_schedule_check.json" schedule_digest_hex)"
  require_extracted "$path_provenance" "PATH_PROVENANCE"
  require_extracted "$atom_path" "ATOM_PATH"
  require_extracted "$atom_path_length" "ATOM_PATH_LENGTH"
  atom_path_counts="$(atom_path_kind_counts "$atom_path")"
  coordinate_atom_count="${atom_path_counts%% *}"
  port_atom_count="${atom_path_counts##* }"
  require_extracted "$coordinate_atom_count" "COORDINATE_ATOM_COUNT"
  require_extracted "$port_atom_count" "PORT_ATOM_COUNT"
  require_extracted "$inventory_count" "INVENTORY_COUNT"
  require_extracted "$inventory_digest" "INVENTORY_DIGEST"
  require_extracted "$max_norm" "MAX_SOURCE_NORM"
  require_extracted "$max_ties" "MAX_SOURCE_NORM_TIES"
  require_extracted "$seam_bridge_policy" "SEAM_BRIDGE_POLICY"
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

  local blocker missing_for_cert
  blocker="SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING"
  missing_for_cert='"coordinate Gaussian-prime source_path from origin prefix to canonical endpoint","per-port representative coordinate path expansion for TileOp atom-chain edges","claim-grade verifier binding the coordinate path to terminal inventory and BZ schedule"'
  if [[ "$path_provenance" == "component_reachability_only" &&
        "$atom_path_length" == "0" ]]; then
    blocker="SOURCE_DEAD_CERT_TARGET_NOT_REACHED"
    missing_for_cert='"positive target reachability to canonical Tsuchimura endpoint","coordinate Gaussian-prime source_path from origin prefix to canonical endpoint","claim-grade verifier binding the coordinate path to terminal inventory and BZ schedule"'
  fi

  cat > "$source_dead_gap" <<JSON
{"schema":"lb_source_k26_source_dead_gap_v1","claim_label":"SOURCE_ORIGIN_K26","proof_status":"DIAGNOSTIC_NON_CLAIM","blocker":"$blocker","non_claim":"executed prefix and continuation evidence only; not a SOURCE_DEAD_CERT","k_sq":26,"terminal_radius":1015645,"target":{"tsuchimura_endpoint":{"a":943460,"b":376039,"norm_sq":1031522101121},"canonical_octant_endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121}},"continuation_artifact":{"name":"k26-continuation-result.json","sha256":"$continuation_digest"},"bz_evidence":{"status":"$bz_status","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"$bz_digest_algorithm","schedule_digest_hex":"$bz_digest_hex"},"bridge_safety":{"seam_bridge_policy":"$seam_bridge_policy","source_bridged_coordinate_carry_atoms":$source_bridged,"source_unbridged_coordinate_carry_atoms":$source_unbridged,"source_unbridged_without_next_band_candidates":$source_unbridged_without,"source_unbridged_with_next_band_candidates":$source_unbridged_with,"source_unbridged_dead_end_candidate_atoms":$source_dead_end,"source_unbridged_unsafe_candidate_atoms":$source_unsafe,"source_bridge_rejected_candidate_atoms":$source_bridge_rejected},"target_path_provenance":"$path_provenance","target_atom_path_length":$atom_path_length,"target_atom_path":$atom_path,"coordinate_path_obligation":{"required_provenance":"coordinate_gaussian_prime_path","observed_provenance":"$path_provenance","observed_coordinate_atom_count":$coordinate_atom_count,"observed_port_atom_count":$port_atom_count,"per_port_coordinate_expansion":"missing","claim_grade_path_accepted":false},"terminal_source_inventory_summary":{"count":$inventory_count,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"$inventory_digest","max_norm_sq":$max_norm,"max_norm_atom_ids":$max_ties},"terminal_inventory_obligation":{"required_mode":"claim_grade_terminal_inventory","observed_mode":"summary_digest_only_non_claim","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"observed_count":$inventory_count,"observed_digest_algorithm":"sha256:lb_source_inventory_v1","observed_digest_hex":"$inventory_digest","observed_max_norm_sq":$max_norm},"missing_for_source_dead_cert":[$missing_for_cert]}
JSON
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

run_k26_continuation

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
