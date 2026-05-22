#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_remote_smoke_artifacts.sh OUT_DIR [--expect-head HEAD] [--expect-branch BRANCH]

Validate pulled LB source-propagation remote smoke artifacts. This checks the
sidecar and independent verifier CTest logs, the non-claim K26 preflight,
contract, execution-plan JSON, K26 BZ schedule diagnostic, and optional
deployed source provenance.
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
  if ! grep -Eq "$pattern" "$path"; then
    echo "artifact check failed: $label ($path)" >&2
    exit 1
  fi
}

require_ctest_log() {
  local path="$1"
  local expected="$2"
  require_file "$path"
  require_grep "100% tests passed, 0 tests failed out of ${expected}" "$path" \
    "ctest ${expected}/${expected} summary"
  local count
  count="$(grep -Ec "^[[:space:]]*[0-9]+/${expected} Test" "$path")"
  if [[ "$count" != "$expected" ]]; then
    echo "artifact check failed: expected ${expected} ctest rows in $path, got $count" >&2
    exit 1
  fi
}

if [[ ! -d "$out_dir" ]]; then
  echo "artifact directory not found: $out_dir" >&2
  exit 1
fi

require_ctest_log "$out_dir/ctest.log" 17
require_ctest_log "$out_dir/verification-ctest.log" 43

for artifact in \
  environment.txt \
  status.txt \
  source_prop_cpu_tileop_smoke.log \
  source_origin_cpu_runner_smoke.json \
  source_tileop_cpu_runner_smoke.json \
  source_origin_prefix_manifest_smoke.json \
  source_tileop_cpu_runner_manifest_smoke.json \
  k26_tsuchimura_preflight.json \
  k26_source_run_contract.json \
  k26_execution_plan.json \
  k26_bz_schedule_check.json \
  k26_source_run_profile.json; do
  require_file "$out_dir/$artifact"
done

require_grep "^REMOTE_SIDECAR_SMOKE_PASS$" "$out_dir/status.txt" \
  "remote smoke pass status"
require_grep "Non-claim: this is not a sqrt\(26\) source/origin run and not a moat result\." \
  "$out_dir/status.txt" "remote smoke non-claim status"

require_grep '"schema":"k26_tsuchimura_preflight_v1"' \
  "$out_dir/k26_tsuchimura_preflight.json" "K26 preflight schema"
require_grep '"claim_label":"SOURCE_ORIGIN_K26"' \
  "$out_dir/k26_tsuchimura_preflight.json" "K26 preflight claim label"
require_grep '"conservative_guard_min_r_final":1015645' \
  "$out_dir/k26_tsuchimura_preflight.json" "K26 preflight guard"
require_grep '"non_claim":"preflight only; no source/origin run executed"' \
  "$out_dir/k26_tsuchimura_preflight.json" "K26 preflight non-claim"

require_grep '"schema":"lb_source_k26_run_contract_v1"' \
  "$out_dir/k26_source_run_contract.json" "K26 contract schema"
require_grep '"claim_label":"SOURCE_ORIGIN_K26"' \
  "$out_dir/k26_source_run_contract.json" "K26 contract claim label"
require_grep '"executable_now":false' \
  "$out_dir/k26_source_run_contract.json" "K26 contract non-executable"
require_grep '"band_count":124' \
  "$out_dir/k26_source_run_contract.json" "K26 contract band count"

require_grep '"schema":"lb_source_k26_execution_plan_v1"' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan schema"
require_grep '"claim_label":"SOURCE_ORIGIN_K26"' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan claim label"
require_grep '"executable_now":false' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan non-executable"
require_grep '"max_dph_usd":0\.37' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan dph cap"
require_grep '"bz_schedule":"repaired"' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan repaired BZ schedule"
require_grep '"repaired_boundary_count":3' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan repair count"
require_grep '"max_abs_boundary_shift":1' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan max repair shift"
require_grep '"band_count":124' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan band count"
require_grep '"last_band_width":8029' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan final width"
require_grep '"index":123' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan final row index"
require_grep '"r_outer":1015645' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan final radius"
require_grep 'local sidecar ctest 16/16' \
  "$out_dir/k26_execution_plan.json" "K26 execution plan sidecar gate"

require_grep '"schema":"lb_source_k26_bz_schedule_check_v1"' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ schedule schema"
require_grep '"claim_label":"SOURCE_ORIGIN_K26"' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ schedule claim label"
require_grep '"proof_status":"BZ_REPAIRED_SCHEDULE_DIAGNOSTIC_NON_CLAIM"' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ schedule non-claim status"
require_grep '"accepted_for_claim":false' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ schedule not accepted"
require_grep '"band_count":124' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ schedule band count"
require_grep '"repaired_boundary_count":3' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ schedule repair count"
require_grep '"max_abs_boundary_shift":1' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ schedule max repair shift"
require_grep '"rows_with_bad_norms":3' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ nominal dirty rows"
require_grep '"bad_norm_count":3' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ nominal dirty summary"
require_grep '"bz_clean":false' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ nominal dirty flag"
require_grep '"rows_with_bad_norms":0' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ repaired clean rows"
require_grep '"bad_norm_count":0' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ repaired clean summary"
require_grep '"bz_clean":true' \
  "$out_dir/k26_bz_schedule_check.json" "K26 BZ repaired clean flag"

require_grep '"schema":"lb_source_k26_run_profile_v1"' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile schema"
require_grep '"claim_label":"SOURCE_ORIGIN_K26"' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile claim label"
require_grep '"profile_status":"RUN_PROFILE_DRAFT_NON_CLAIM"' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile non-claim status"
require_grep '"executable_now":false' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile non-executable"
require_grep '"required_k_sq":26' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile required build"
require_grep '"bz_schedule":"repaired"' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile repaired schedule"
require_grep '"prefix_row_index":0' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile prefix row"
require_grep '"tileop_port_first_row_index":1' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile TileOp start row"
require_grep 'source_tileop_port_runner currently accepts --band-width, not an explicit variable boundary schedule' \
  "$out_dir/k26_source_run_profile.json" "K26 run profile runner gap"

for json in "$out_dir"/*.json "$out_dir/status.txt"; do
  if grep -Eq 'SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS' "$json"; then
    echo "artifact check failed: claim-pass token found in non-claim artifact $json" >&2
    exit 1
  fi
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

echo "REMOTE_SIDECAR_SMOKE_ARTIFACTS_PASS dir=$out_dir"
