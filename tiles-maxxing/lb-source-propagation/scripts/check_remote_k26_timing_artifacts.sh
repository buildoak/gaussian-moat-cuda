#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_remote_k26_timing_artifacts.sh OUT_DIR [--expect-head HEAD]
                                             [--expect-branch BRANCH]

Validate pulled remote sqrt(26) timing-probe artifacts. This is a non-claim
gate: it checks deployed-source provenance, remote probe status, source-dead
checker wiring, runtime-budget diagnostics, and the K26 bundle status shape.
USAGE
}

if [[ $# -lt 1 ]]; then
  usage >&2
  exit 2
fi

out_dir="$1"
shift
expect_head=""
expect_branch=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --expect-head)
      expect_head="$2"
      shift 2
      ;;
    --expect-branch)
      expect_branch="$2"
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

require_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "missing required artifact: $path" >&2
    exit 1
  fi
}

require_grep() {
  local pattern="$1"
  local path="$2"
  local label="$3"
  if ! grep -Eq -- "$pattern" "$path"; then
    echo "artifact check failed: $label ($path)" >&2
    exit 1
  fi
}

require_no_grep() {
  local pattern="$1"
  local path="$2"
  local label="$3"
  if grep -Eq -- "$pattern" "$path"; then
    echo "artifact check failed: $label ($path)" >&2
    exit 1
  fi
}

json_string_value() {
  local path="$1"
  local field="$2"
  sed -nE "s/.*\"${field}\":\"([^\"]+)\".*/\\1/p" "$path" | head -n 1
}

require_json_string_value() {
  local path="$1"
  local field="$2"
  local value
  value="$(json_string_value "$path" "$field")"
  if [[ -z "$value" ]]; then
    echo "artifact check failed: missing JSON string field ${field} ($path)" >&2
    exit 1
  fi
  printf '%s\n' "$value"
}

require_equal() {
  local expected="$1"
  local actual="$2"
  local label="$3"
  if [[ "$expected" != "$actual" ]]; then
    echo "artifact check failed: ${label}: expected ${expected}, got ${actual}" >&2
    exit 1
  fi
}

if [[ ! -d "$out_dir" ]]; then
  echo "artifact directory not found: $out_dir" >&2
  exit 1
fi

for artifact in \
  environment.txt \
  cmake-configure.log \
  cmake-build.log \
  verification-cmake-configure.log \
  verification-cmake-build.log \
  k26-bundle-harness.log \
  k26-bundle-harness.err \
  harness-status-summary.txt \
  status.txt \
  remote-k26-timing-probe-status.txt; do
  require_file "$out_dir/$artifact"
done

require_grep "^REMOTE_K26_TIMING_PROBE_PASS$" \
  "$out_dir/remote-k26-timing-probe-status.txt" \
  "remote K26 timing probe pass status"
require_grep "Non-claim: this is not SOURCE_DEAD_CERT and not a moat result." \
  "$out_dir/remote-k26-timing-probe-status.txt" \
  "remote K26 timing probe non-claim status"
require_grep "^k_sq=26$" "$out_dir/environment.txt" "remote K26 K_SQ"
require_grep "^sidecar_build_dir=" "$out_dir/environment.txt" \
  "sidecar build dir provenance"
require_grep "^verification_build_dir=" "$out_dir/environment.txt" \
  "verification build dir provenance"

require_grep "source_dead_gap_check" "$out_dir/verification-cmake-build.log" \
  "source-dead gap checker build"
require_grep "source_dead_cert_check" "$out_dir/verification-cmake-build.log" \
  "source-dead cert checker build"

if [[ -f "$out_dir/k26-full-run-args.txt" ]]; then
  require_grep "--source-dead-gap-checker [^[:space:]]*source_dead_gap_check" \
    "$out_dir/k26-full-run-args.txt" "bundle gap checker argument"
  require_grep "--source-dead-checker [^[:space:]]*source_dead_cert_check" \
    "$out_dir/k26-full-run-args.txt" "bundle cert checker argument"
fi

require_grep "^harness_exit_code=[0-9]+$" \
  "$out_dir/harness-status-summary.txt" "harness exit code summary"
require_grep "^K26_FULL_RUN_BUNDLE_(BLOCKED_|PASS)" "$out_dir/status.txt" \
  "K26 bundle status"
require_grep "^K26_FULL_RUN_BUNDLE_(BLOCKED_|PASS)" \
  "$out_dir/harness-status-summary.txt" "K26 bundle summary status"

if [[ -f "$out_dir/k26-continuation-progress.jsonl" ]]; then
  require_file "$out_dir/k26-continuation-chunks.jsonl"
  require_file "$out_dir/k26-runtime-budget-check.manual.log"
  require_file "$out_dir/k26-runtime-budget-check.manual.meta"
  require_grep '"status":"K26_RUNTIME_BUDGET_(PASS|REJECT|INSUFFICIENT_PROGRESS)"' \
    "$out_dir/k26-runtime-budget-check.manual.log" \
    "manual runtime-budget status"
  require_grep '"proof_status":"RUNTIME_BUDGET_DIAGNOSTIC_NON_CLAIM"' \
    "$out_dir/k26-runtime-budget-check.manual.log" \
    "manual runtime-budget non-claim status"
  require_grep "^manual_runtime_budget_exit_code=[0-9]+$" \
    "$out_dir/k26-runtime-budget-check.manual.meta" \
    "manual runtime-budget exit code"
fi

if [[ -f "$out_dir/k26-source-dead-gap.json" ]]; then
  require_grep '"schema":"lb_source_k26_source_dead_gap_v1"' \
    "$out_dir/k26-source-dead-gap.json" "source-dead gap schema"
  require_grep '"claim_label":"SOURCE_ORIGIN_K26"' \
    "$out_dir/k26-source-dead-gap.json" "source-dead gap claim label"
  require_grep '"proof_status":"DIAGNOSTIC_NON_CLAIM"' \
    "$out_dir/k26-source-dead-gap.json" "source-dead gap non-claim status"
fi

if [[ -f "$out_dir/k26-source-dead-cert.json" ]]; then
  require_grep '"schema":"lb_source_dead_cert_draft_v1"' \
    "$out_dir/k26-source-dead-cert.json" "source-dead cert schema"
  require_grep '"proof_status":"SUMMARY_ONLY_NON_CLAIM"' \
    "$out_dir/k26-source-dead-cert.json" "source-dead cert non-claim status"
fi

for artifact in "$out_dir"/*.json "$out_dir/status.txt" \
    "$out_dir/remote-k26-timing-probe-status.txt"; do
  [[ -f "$artifact" ]] || continue
  require_no_grep 'SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS' \
    "$artifact" "claim-pass token found in remote timing artifact"
done

if [[ -n "$expect_head" || -n "$expect_branch" ]]; then
  require_file "$out_dir/deployed_source.txt"
fi
if [[ -n "$expect_head" ]]; then
  require_grep "^deployed_local_head=${expect_head}$" \
    "$out_dir/deployed_source.txt" "deployed source head"
fi
if [[ -n "$expect_branch" ]]; then
  require_grep "^deployed_local_branch=${expect_branch}$" \
    "$out_dir/deployed_source.txt" "deployed source branch"
fi

echo "REMOTE_K26_TIMING_ARTIFACTS_PASS dir=$out_dir"
