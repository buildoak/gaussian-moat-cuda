#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_remote_k26_timing_artifact_checker.sh CHECKER" >&2
  exit 2
fi

checker="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/environment.txt" <<'ENV'
timestamp_utc=2026-05-23T12:00:00Z
hostname=fixture
repo=/workspace/gaussian-moat-cuda
sidecar_build_dir=/tmp/gm-lbsp-remote-k26
verification_build_dir=/tmp/gm-lbsp-remote-k26-verify
commit=fixture
branch=ttc/lb-source-propagation
k_sq=26
chunk_bands=1
timeout_seconds=1200
max_runtime_seconds=14000
tileop_threads=0
nvidia_smi=unavailable
ENV

cat > "$tmp/remote-k26-timing-probe-status.txt" <<'STATUS'
REMOTE_K26_TIMING_PROBE_PASS
Scope: bounded non-claim K26 source/origin timing probe using the chunked bundle
harness. Expected bundle blockers are timing evidence only.
Non-claim: this is not SOURCE_DEAD_CERT and not a moat result.
STATUS

cat > "$tmp/status.txt" <<'STATUS'
K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM
STATUS

cat > "$tmp/harness-status-summary.txt" <<'STATUS'
harness_exit_code=3
K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM
STATUS

cat > "$tmp/cmake-configure.log" <<'LOG'
-- Configuring done
LOG
cat > "$tmp/cmake-build.log" <<'LOG'
[100%] Built target source_tileop_port_runner
LOG
cat > "$tmp/verification-cmake-configure.log" <<'LOG'
-- Configuring done
LOG
cat > "$tmp/verification-cmake-build.log" <<'LOG'
[ 50%] Built target source_dead_gap_check
[100%] Built target source_dead_cert_check
LOG
cat > "$tmp/k26-bundle-harness.log" <<'LOG'
fixture harness stdout
LOG
cat > "$tmp/k26-bundle-harness.err" <<'LOG'
fixture harness stderr
LOG

cat > "$tmp/k26-full-run-args.txt" <<'ARGS'
--build-dir /tmp/gm-lbsp-remote-k26 --out-dir /workspace/lb-source-k26-timing-probe --continuation-chunk-bands 1 --resume-existing --timeout-seconds 1200 --max-runtime-seconds 14000 --tileop-threads 0 --source-dead-gap-checker /tmp/gm-lbsp-remote-k26-verify/source_dead_gap_check --source-dead-checker /tmp/gm-lbsp-remote-k26-verify/source_dead_cert_check
ARGS

cat > "$tmp/k26-continuation-progress.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_progress_v1","accepted":true,"band_index":0,"r_start":8192,"r_outer":16384,"total_ms":1000,"has_source_carry":true,"terminal_source_dead":false}
JSONL
cat > "$tmp/k26-continuation-chunks.jsonl" <<'JSONL'
{"schema":"lb_source_k26_continuation_chunk_v1","chunk_index":0}
JSONL
cat > "$tmp/k26-runtime-budget-check.manual.log" <<'JSON'
{"status":"K26_RUNTIME_BUDGET_PASS","proof_status":"RUNTIME_BUDGET_DIAGNOSTIC_NON_CLAIM","claim_grade":false}
JSON
cat > "$tmp/k26-runtime-budget-check.manual.meta" <<'META'
manual_runtime_budget_exit_code=0
META

cat > "$tmp/k26-source-dead-gap.json" <<'JSON'
{"schema":"lb_source_k26_source_dead_gap_v1","claim_label":"SOURCE_ORIGIN_K26","proof_status":"DIAGNOSTIC_NON_CLAIM"}
JSON
cat > "$tmp/k26-source-dead-cert.json" <<'JSON'
{"schema":"lb_source_dead_cert_draft_v1","proof_status":"SUMMARY_ONLY_NON_CLAIM"}
JSON

"$checker" "$tmp" > "$tmp/pass.log"
grep -q "REMOTE_K26_TIMING_ARTIFACTS_PASS" "$tmp/pass.log"

cat > "$tmp/deployed_source.txt" <<'SRC'
deployed_local_head=abc1234
deployed_local_branch=ttc/lb-source-propagation
SRC
"$checker" "$tmp" --expect-head abc1234 \
  --expect-branch ttc/lb-source-propagation > "$tmp/provenance-pass.log"
grep -q "REMOTE_K26_TIMING_ARTIFACTS_PASS" "$tmp/provenance-pass.log"

bad="$tmp/bad"
mkdir "$bad"
cp "$tmp"/* "$bad"/ 2>/dev/null || true
perl -0pi -e 's/--source-dead-checker [^ ]+//' "$bad/k26-full-run-args.txt"
if "$checker" "$bad" > "$tmp/bad.log" 2>&1; then
  echo "checker accepted timing artifacts without source-dead checker wiring" >&2
  exit 1
fi
grep -q "bundle cert checker argument" "$tmp/bad.log"

bad_runtime="$tmp/bad-runtime"
mkdir "$bad_runtime"
cp "$tmp"/* "$bad_runtime"/ 2>/dev/null || true
rm "$bad_runtime/k26-runtime-budget-check.manual.log"
if "$checker" "$bad_runtime" > "$tmp/bad-runtime.log" 2>&1; then
  echo "checker accepted progress without manual runtime-budget diagnostics" >&2
  exit 1
fi
grep -q "k26-runtime-budget-check.manual.log" "$tmp/bad-runtime.log"

bad_claim="$tmp/bad-claim"
mkdir "$bad_claim"
cp "$tmp"/* "$bad_claim"/ 2>/dev/null || true
echo "SOURCE_DEAD_CERT_PASS" >> "$bad_claim/status.txt"
if "$checker" "$bad_claim" > "$tmp/bad-claim.log" 2>&1; then
  echo "checker accepted a claim-pass token in non-claim timing artifacts" >&2
  exit 1
fi
grep -q "claim-pass token" "$tmp/bad-claim.log"

echo "remote K26 timing artifact checker self-test PASS"
