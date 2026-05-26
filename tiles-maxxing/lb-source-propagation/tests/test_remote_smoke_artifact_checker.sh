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

write_ctest_log "$tmp/ctest.log" 35
write_ctest_log "$tmp/verification-ctest.log" 78

cat > "$tmp/status.txt" <<'STATUS'
REMOTE_SIDECAR_SMOKE_PASS
Scope: sidecar build/test, independent verification CTest, CPU TileOp producer smoke, small coordinate source runner, CPU TileOp-fed source runner, diagnostic TileOp-port stream unit smoke/equivalence, K26 non-claim run contract, K26 non-claim execution plan, K26 BZ schedule evidence, K26 run profile draft, and K26 run command contract only.
Non-claim: this is not a sqrt(26) source/origin run and not a moat result.
STATUS

cat > "$tmp/environment.txt" <<'ENV'
timestamp_utc=2026-05-22T00:00:00Z
hostname=fixture
repo=/workspace/gaussian-moat-cuda
commit=fixture
branch=ttc/lb-source-propagation
k_sq=26
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

cat > "$tmp/source_tileop_port_stream_runner_smoke.json" <<'JSON'
{"schema":"lb_source_tileop_port_stream_runner_v1","runner_id":"source_tileop_port_stream_runner_v1","claim_label":"SOURCE_TILEOP_PORT_STREAM_DIAGNOSTIC","proof_status":"DIAGNOSTIC_NON_CLAIM","resumable_mode":"resumable-band","source_mode":"GEO_I_PORT_DIAGNOSTIC","k_sq":26,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"checkpoint_written":true,"resumable_checkpoint_written":true,"max_resident_microband_tiles":4,"max_checkpoint_bytes":1693}
JSON

cat > "$tmp/source_tileop_port_stream_live_handoff.txt" <<'TXT'
LB_SOURCE_LIVE_HANDOFF_V1
TXT

cat > "$tmp/source_tileop_port_stream_checkpoint.txt" <<'TXT'
LB_SOURCE_STREAM_CHECKPOINT_V1
TXT

cat > "$tmp/source_tileop_port_stream_resumable_checkpoint.txt" <<'TXT'
LB_RESUMABLE_BAND_CHECKPOINT_V1
mode resumable-band
proof_status DIAGNOSTIC_NON_CLAIM
TXT

cat > "$tmp/source_tileop_port_stream_progress.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_stream_progress_v1","max_resident_microband_tiles":4}
JSONL

cat > "$tmp/source_tileop_port_stream_equivalence.log" <<'LOG'
MATERIALIZED_STREAM_HANDOFF_EQUIVALENCE_PASS k_sq=26 cut_radius=512 carry_atoms=21 components=1
TILEOP_PORT_STREAM_EQUIVALENCE_PASS out_dir=/tmp/stream-equivalence
LOG

cat > "$tmp/k26_tsuchimura_preflight.json" <<'JSON'
{"schema":"k26_tsuchimura_preflight_v1","claim_label":"SOURCE_ORIGIN_K26","conservative_guard_min_r_final":1015645,"non_claim":"preflight only; no source/origin run executed"}
JSON

cat > "$tmp/k26_source_run_contract.json" <<'JSON'
{"schema":"lb_source_k26_run_contract_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":false,"source_dead_cert_claim_gate":{"terminal_source_inventory_mode":"claim_grade_accumulator","terminal_source_inventory_accumulator_mode":"claim_grade_digest_accumulator","required_true_flags":["complete_stream_observed","canonical_order","duplicate_free","retired_component_finalized","overflow_checked"]},"band_schedule_hint":{"band_count":124},"blocking_gaps":["source_tileop_port_runner supports explicit variable boundaries and the harness wires rows 1..123 with chunk/resume support, but the repaired schedule has not produced accepted full-run artifacts"],"non_claim":"execution contract only; no source/origin run executed"}
JSON

cat > "$tmp/k26_execution_plan.json" <<'JSON'
{"schema":"lb_source_k26_execution_plan_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":false,"budget_caps":{"max_dph_usd":0.37,"max_total_usd":1.5},"schedule":{"bz_schedule":"repaired","bz_evidence":{"status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95"},"repaired_boundary_count":3,"max_abs_boundary_shift":1,"band_count":124,"last_band_width":8029,"rows":[{"index":123,"r_outer":1015645}]},"pre_run_gates":["local sidecar CTest passes","local independent verification CTest passes"],"non_claim":"execution plan only; no source/origin run executed"}
JSON

cat > "$tmp/k26_bz_schedule_check.json" <<'JSON'
{"schema":"lb_source_k26_bz_schedule_check_v1","claim_label":"SOURCE_ORIGIN_K26","proof_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","band_count":124,"repair":{"repaired_boundary_count":3,"max_abs_boundary_shift":1},"nominal_summary":{"rows_checked":124,"rows_with_bad_norms":3,"bad_norm_count":3,"bz_clean":false},"repaired_summary":{"rows_checked":124,"rows_with_bad_norms":0,"bad_norm_count":0,"bz_clean":true},"non_claim":"exact K26 bad-zone schedule evidence only; repaired rows are BZ-clean but no source/origin run was executed"}
JSON

cat > "$tmp/k26_source_run_profile.json" <<'JSON'
{"schema":"lb_source_k26_run_profile_v1","claim_label":"SOURCE_ORIGIN_K26","profile_status":"RUN_PROFILE_DRAFT_NON_CLAIM","executable_now":false,"build":{"required_k_sq":26},"schedule":{"bz_schedule":"repaired","bz_evidence":{"status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95"},"band_count":124,"prefix_row_index":0,"tileop_port_first_row_index":1},"execution_protocol":{"bundle_harness":"run_k26_full_source_bundle.sh","recommended_continuation_chunk_bands":8,"resume_existing_supported":true,"resume_existing_flag":"--resume-existing","chunk_ledger":"k26-continuation-chunks.jsonl","chunk_ledger_required_for_checked_bundle":true,"source_dead_gap_checker":"source_dead_gap_check","source_dead_checker":"source_dead_cert_check","auto_summary_nonclaim_cert":true,"auto_summary_nonclaim_cert_artifact":"k26-source-dead-cert.json","recommended_max_runtime_seconds":14000},"source_dead_cert_claim_gate":{"required_terminal_source_inventory_mode":"claim_grade_accumulator","required_accumulator_mode":"claim_grade_digest_accumulator","required_true_flags":["complete_stream_observed","canonical_order","duplicate_free","retired_component_finalized","overflow_checked"]},"missing_runner_features":["full-run K26 bundle harness supports chunk/resume execution but has not completed accepted artifacts for the repaired variable-boundary schedule under the active budget"],"non_claim":"run profile only; no sqrt(26) source/origin run executed"}
JSON

cat > "$tmp/k26_source_run_commands.json" <<'JSON'
{"schema":"lb_source_k26_run_commands_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":false,"build":{"required_k_sq":26},"target":{"tsuchimura_endpoint":{"a":943460,"b":376039},"canonical_octant_endpoint":{"a":376039,"b":943460}},"prefix":{"r_final":8192,"command":"source_origin_cpu_runner --endpoint-a 376039 --endpoint-b 943460"},"continuation":{"r_start":8192,"r_final":1015645,"schedule_boundary_count":124,"schedule_segment_count":123,"schedule_min_width":8029,"schedule_max_width":8193,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","seam_bridge_policy":"diagnostic_allow_unbridged","blocked_if_unbridged_coordinate_carry_atoms":false,"claim_grade_requires_source_unbridged_unsafe_candidate_atoms":0,"schedule_radii_csv":"8192,122879,475135,622591,1015645","recommended_chunk_bands":8,"resume_existing_supported":true,"resume_existing_flag":"--resume-existing","command":"source_tileop_port_runner --target-a 376039 --target-b 943460"},"bundle_harness":{"runner":"run_k26_full_source_bundle.sh","recommended_continuation_chunk_bands":8,"resume_existing_supported":true,"chunk_ledger":"k26-continuation-chunks.jsonl","chunk_ledger_required_for_checked_bundle":true,"source_dead_gap_checker":"source_dead_gap_check","source_dead_checker":"source_dead_cert_check","auto_summary_nonclaim_cert":true,"auto_summary_nonclaim_cert_artifact":"k26-source-dead-cert.json","auto_summary_nonclaim_blocker":"K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM","claim_grade_terminal_source_inventory_mode":"claim_grade_accumulator","claim_grade_accumulator_mode":"claim_grade_digest_accumulator","claim_grade_accumulator_required_true_flags":["complete_stream_observed","canonical_order","duplicate_free","retired_component_finalized","overflow_checked"],"timeout_seconds":1200,"max_runtime_seconds":14000,"command":"run_k26_full_source_bundle.sh --continuation-chunk-bands 8 --resume-existing --timeout-seconds 1200 --max-runtime-seconds 14000 --source-dead-gap-checker source_dead_gap_check","checked_bundle_command":"run_k26_full_source_bundle.sh --continuation-chunk-bands 8 --resume-existing --timeout-seconds 1200 --max-runtime-seconds 14000 --source-dead-gap-checker source_dead_gap_check --source-dead-checker source_dead_cert_check","supplied_cert_bundle_command":"run_k26_full_source_bundle.sh --continuation-chunk-bands 8 --resume-existing --timeout-seconds 1200 --max-runtime-seconds 14000 --source-dead-gap-checker source_dead_gap_check --cert-in k26-source-dead-cert.json --source-dead-checker source_dead_cert_check"},"non_claim":"command contract only; no sqrt(26) source/origin run executed"}
JSON

"$checker" "$tmp" > "$tmp/pass.log"
grep -q "REMOTE_SIDECAR_SMOKE_ARTIFACTS_PASS" "$tmp/pass.log"

cat > "$tmp/deployed_source.txt" <<'SRC'
deployed_local_head=abc1234
deployed_local_branch=ttc/lb-source-propagation
SRC
"$checker" "$tmp" --expect-head abc1234 --expect-branch ttc/lb-source-propagation \
  --expect-k-sq 26 \
  > "$tmp/provenance-pass.log"
grep -q "REMOTE_SIDECAR_SMOKE_ARTIFACTS_PASS" "$tmp/provenance-pass.log"

bad_ctest="$tmp/bad-ctest"
mkdir "$bad_ctest"
cp "$tmp"/* "$bad_ctest"/ 2>/dev/null || true
perl -0pi -e 's/100% tests passed, 0 tests failed out of 78/100% tests passed, 0 tests failed out of 77/' \
  "$bad_ctest/verification-ctest.log"
if "$checker" "$bad_ctest" > "$tmp/bad-ctest.log" 2>&1; then
  echo "checker accepted verification CTest log with mismatched row count" >&2
  exit 1
fi
grep -q 'artifact check failed' "$tmp/bad-ctest.log"

short_ctest="$tmp/short-ctest"
mkdir "$short_ctest"
cp "$tmp"/* "$short_ctest"/ 2>/dev/null || true
write_ctest_log "$short_ctest/verification-ctest.log" 75
if "$checker" "$short_ctest" > "$tmp/short-ctest.log" 2>&1; then
  echo "checker accepted verification CTest below Phase 1 baseline" >&2
  exit 1
fi
grep -q 'verification CTest summary is below Phase 1 baseline' \
  "$tmp/short-ctest.log"

missing_stream="$tmp/missing-stream"
mkdir "$missing_stream"
cp "$tmp"/* "$missing_stream"/ 2>/dev/null || true
rm "$missing_stream/source_tileop_port_stream_runner_smoke.json"
if "$checker" "$missing_stream" > "$tmp/missing-stream.log" 2>&1; then
  echo "checker accepted remote smoke artifacts without stream runner smoke JSON" >&2
  exit 1
fi
grep -q 'missing required artifact' "$tmp/missing-stream.log"

bad_stream="$tmp/bad-stream"
mkdir "$bad_stream"
cp "$tmp"/* "$bad_stream"/ 2>/dev/null || true
perl -0pi -e 's/"proof_status":"DIAGNOSTIC_NON_CLAIM"/"proof_status":"SOURCE_DEAD_CERT_PASS"/' \
  "$bad_stream/source_tileop_port_stream_runner_smoke.json"
if "$checker" "$bad_stream" > "$tmp/bad-stream.log" 2>&1; then
  echo "checker accepted claim-like stream runner smoke JSON" >&2
  exit 1
fi
grep -q 'TileOp-port stream runner non-claim status' "$tmp/bad-stream.log"

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

if "$checker" "$tmp" --expect-k-sq 36 > "$tmp/bad-ksq.log" 2>&1; then
  echo "checker accepted a mismatched remote K_SQ" >&2
  exit 1
fi

bad_runtime="$tmp/bad-runtime"
mkdir "$bad_runtime"
cp "$tmp"/* "$bad_runtime"/ 2>/dev/null || true
perl -0pi -e 's/,"recommended_max_runtime_seconds":14000//' \
  "$bad_runtime/k26_source_run_profile.json"
perl -0pi -e 's/,"max_runtime_seconds":14000//; s/ --max-runtime-seconds 14000//g' \
  "$bad_runtime/k26_source_run_commands.json"
if "$checker" "$bad_runtime" > "$tmp/bad-runtime.log" 2>&1; then
  echo "checker accepted K26 run artifacts without runtime budget guard" >&2
  exit 1
fi
grep -q 'runtime budget guard' "$tmp/bad-runtime.log"

echo "remote smoke artifact checker self-test PASS"
