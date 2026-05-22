#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  run_k26_full_source_bundle.sh --build-dir DIR --out-dir DIR
                                [--max-atoms N]
                                [--cert-in PATH]
                                [--source-dead-checker PATH]

Run the prepared sqrt(26) source/origin bundle contract using an existing
lb-source-propagation build directory. This script performs no Vast API actions
and does not claim a moat result.

Artifacts written under OUT_DIR:
  k26_source_run_commands.json
  k26_bz_schedule_check.json
  k26_source_run_profile.json
  k26-prefix-result.json
  k26-continuation-result.json
  k26-prefix-manifest.txt
  k26-prefix-witness.txt
  status.txt

If --cert-in is supplied, it is copied to k26-source-dead-cert.json and the
bundle checker is run. Without a cert, the script stops after the continuation
with K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING.
USAGE
}

build_dir=""
out_dir=""
max_atoms="50000000"
cert_in=""
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
    --cert-in)
      cert_in="$2"
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

mkdir -p "$out_dir"

status_file="$out_dir/status.txt"
write_status() {
  local status="$1"
  {
    echo "$status"
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "build_dir=$build_dir"
    echo "out_dir=$out_dir"
    echo "max_atoms=$max_atoms"
    echo "non_claim=this is an executed bundle harness, not a source-dead acceptance"
  } > "$status_file"
}

run_json() {
  local label="$1"
  local output="$2"
  shift 2
  local stderr_file="$output.stderr"
  echo "RUN $label: $*" >> "$out_dir/run.log"
  if ! "$@" > "$output" 2> "$stderr_file"; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_${label}_FAILED"
    cat "$stderr_file" >&2
    exit 1
  fi
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
    --prefix-witness-out "$out_dir/k26-prefix-witness.txt"

run_json K26_CONTINUATION "$out_dir/k26-continuation-result.json" \
  "$build_dir/source_tileop_port_runner" \
    --r-start 8192 \
    --r-final 1015645 \
    --band-width 8192 \
    --schedule-radii "$schedule_csv" \
    --max-atoms "$max_atoms" \
    --require-full-bridge \
    --manifest-in "$out_dir/k26-prefix-manifest.txt" \
    --prefix-witness-in "$out_dir/k26-prefix-witness.txt"

if [[ -n "$cert_in" ]]; then
  if [[ ! -f "$cert_in" ]]; then
    write_status "K26_FULL_RUN_BUNDLE_BLOCKED_CERT_IN_MISSING"
    echo "--cert-in file not found: $cert_in" >&2
    exit 2
  fi
  cp "$cert_in" "$out_dir/k26-source-dead-cert.json"
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

"$bundle_checker" "$out_dir" --source-dead-checker "$source_dead_checker" \
  | tee "$out_dir/k26-full-run-bundle-check.log"
write_status "K26_FULL_RUN_BUNDLE_CHECKED"
