#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_remote_smoke_artifact_checker.sh CHECKER" >&2
  exit 2
fi

checker="$1"
if [[ ! -x "$checker" ]]; then
  echo "checker is not executable: $checker" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

write_ctest_log() {
  local path="$1"
  local expected="$2"
  {
    echo "Test project /tmp/lb-source-artifact-check"
    for i in $(seq 1 "$expected"); do
      printf "%2d/%d Test #%2d: fixture_%02d ...   Passed    0.00 sec\n" \
        "$i" "$expected" "$i" "$i"
    done
    echo
    echo "100% tests passed, 0 tests failed out of $expected"
  } > "$path"
}

write_ctest_log "$tmp/ctest.log" 16
write_ctest_log "$tmp/verification-ctest.log" 43

cat > "$tmp/status.txt" <<'STATUS'
REMOTE_SIDECAR_SMOKE_PASS
Scope: sidecar build/test, independent verification CTest, CPU TileOp producer smoke, small coordinate source runner, CPU TileOp-fed source runner, K26 non-claim run contract, K26 non-claim execution plan, and K26 BZ schedule diagnostic only.
Non-claim: this is not a sqrt(26) source/origin run and not a moat result.
STATUS

cat > "$tmp/environment.txt" <<'ENV'
timestamp_utc=2026-05-22T00:00:00Z
hostname=fixture
repo=/workspace/gaussian-moat-cuda
commit=fixture
branch=ttc/lb-source-propagation
k_sq=36
nvidia_smi=unavailable
ENV

cat > "$tmp/source_prop_cpu_tileop_smoke.log" <<'LOG'
source_prop_cpu_tileop_smoke PASS primes=127 edges=414 carry_atoms=127
LOG

cat > "$tmp/source_origin_cpu_runner_smoke.json" <<'JSON'
{"schema":"lb_source_origin_cpu_runner_v1","claim_label":"SOURCE_ORIGIN_DIAGNOSTIC","proof_status":"DIAGNOSTIC_NON_CLAIM","non_claim":"small coordinate-fed sidecar runner; not a TileOp/CUDA SOURCE_DEAD_CERT"}
JSON
cat > "$tmp/source_tileop_cpu_runner_smoke.json" <<'JSON'
{"schema":"lb_source_tileop_cpu_runner_v1","claim_label":"SOURCE_ORIGIN_TILEOP_CPU_DIAGNOSTIC","proof_status":"DIAGNOSTIC_NON_CLAIM","non_claim":"CPU TileOp-fed sidecar diagnostic; not a CUDA campaign or SOURCE_DEAD_CERT"}
JSON
cat > "$tmp/source_origin_prefix_manifest_smoke.json" <<'JSON'
{"schema":"lb_source_origin_cpu_runner_v1","claim_label":"SOURCE_ORIGIN_DIAGNOSTIC","proof_status":"DIAGNOSTIC_NON_CLAIM","non_claim":"small coordinate-fed sidecar runner; not a TileOp/CUDA SOURCE_DEAD_CERT"}
JSON
cat > "$tmp/source_tileop_cpu_runner_manifest_smoke.json" <<'JSON'
{"schema":"lb_source_tileop_cpu_runner_v1","claim_label":"SOURCE_ORIGIN_TILEOP_CPU_DIAGNOSTIC","proof_status":"DIAGNOSTIC_NON_CLAIM","non_claim":"CPU TileOp-fed sidecar diagnostic; not a CUDA campaign or SOURCE_DEAD_CERT"}
JSON

cat > "$tmp/k26_tsuchimura_preflight.json" <<'JSON'
{"schema":"k26_tsuchimura_preflight_v1","claim_label":"SOURCE_ORIGIN_K26","conservative_guard_min_r_final":1015645,"non_claim":"preflight only; no source/origin run executed"}
JSON

cat > "$tmp/k26_source_run_contract.json" <<'JSON'
{"schema":"lb_source_k26_run_contract_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":false,"band_schedule_hint":{"band_count":124},"non_claim":"execution contract only; no source/origin run executed"}
JSON

cat > "$tmp/k26_execution_plan.json" <<'JSON'
{"schema":"lb_source_k26_execution_plan_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":false,"budget_caps":{"max_dph_usd":0.37,"max_total_usd":1.5},"schedule":{"band_count":124,"last_band_width":8029,"rows":[{"index":123,"r_outer":1015645}]},"pre_run_gates":["local sidecar ctest 16/16"],"non_claim":"execution plan only; no source/origin run executed"}
JSON

cat > "$tmp/k26_bz_schedule_check.json" <<'JSON'
{"schema":"lb_source_k26_bz_schedule_check_v1","claim_label":"SOURCE_ORIGIN_K26","proof_status":"BZ_SCHEDULE_REQUIRES_ROW_SHIFTS_DIAGNOSTIC","accepted_for_claim":false,"band_count":124,"summary":{"rows_checked":124,"rows_with_bad_norms":3,"bad_norm_count":3,"bz_clean":false},"non_claim":"exact K26 bad-zone schedule diagnostic only; nominal rows require BZ repair before any source/origin run"}
JSON

"$checker" "$tmp" > "$tmp/pass.log"
grep -q "REMOTE_SIDECAR_SMOKE_ARTIFACTS_PASS" "$tmp/pass.log"

cat > "$tmp/deployed_source.txt" <<'SRC'
deployed_local_head=abc1234
deployed_local_branch=ttc/lb-source-propagation
SRC
"$checker" "$tmp" --expect-head abc1234 --expect-branch ttc/lb-source-propagation \
  > "$tmp/provenance-pass.log"
grep -q "REMOTE_SIDECAR_SMOKE_ARTIFACTS_PASS" "$tmp/provenance-pass.log"

bad="$tmp/bad"
mkdir "$bad"
cp "$tmp"/* "$bad"/ 2>/dev/null || true
cat > "$bad/k26_execution_plan.json" <<'JSON'
{"schema":"lb_source_k26_execution_plan_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":true}
JSON
if "$checker" "$bad" > "$tmp/bad.log" 2>&1; then
  echo "checker accepted a corrupted executable K26 plan" >&2
  exit 1
fi

echo "remote smoke artifact checker self-test PASS"
