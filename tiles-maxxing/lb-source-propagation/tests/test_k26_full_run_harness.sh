#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_k26_full_run_harness.sh HARNESS" >&2
  exit 2
fi

harness="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

build_dir="$tmp/build"
mkdir -p "$build_dir"
cat > "$build_dir/CMakeCache.txt" <<'CACHE'
K_SQ:STRING=26
CACHE

cat > "$build_dir/k26_source_run_commands" <<'SH'
#!/usr/bin/env bash
cat <<'JSON'
{"schema":"lb_source_k26_run_commands_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":false,"build":{"required_k_sq":26},"target":{"tsuchimura_endpoint":{"a":943460,"b":376039,"norm_sq":1031522101121},"canonical_octant_endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121}},"prefix":{"command":"source_origin_cpu_runner --endpoint-a 376039 --endpoint-b 943460"},"continuation":{"r_start":8192,"r_final":1015645,"schedule_boundary_count":124,"schedule_segment_count":123,"schedule_min_width":8029,"schedule_max_width":8193,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","seam_bridge_policy":"diagnostic_allow_unbridged","blocked_if_unbridged_coordinate_carry_atoms":false,"claim_grade_requires_source_unbridged_unsafe_candidate_atoms":0,"schedule_radii_csv":"8192,122879,475135,622591,1015645","command":"source_tileop_port_runner --target-a 376039 --target-b 943460"}}
JSON
SH

cat > "$build_dir/k26_bz_schedule_check" <<'SH'
#!/usr/bin/env bash
cat <<'JSON'
{"schema":"lb_source_k26_bz_schedule_check_v1","proof_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","accepted_for_schedule":true,"accepted_for_claim":false,"band_count":124,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","repaired_boundary_count":3,"max_abs_boundary_shift":1,"repaired_summary":{"bad_norm_count":0,"bz_clean":true}}
JSON
SH

cat > "$build_dir/k26_source_run_profile" <<'SH'
#!/usr/bin/env bash
cat <<'JSON'
{"schema":"lb_source_k26_run_profile_v1","claim_label":"SOURCE_ORIGIN_K26","profile_id":"k26-source-run-profile","profile_status":"RUN_PROFILE_DRAFT_NON_CLAIM","executable_now":false,"target":{"tsuchimura_endpoint":{"a":943460,"b":376039,"norm_sq":1031522101121},"canonical_octant_endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"expected_component_size":14542615005},"schedule":{"terminal_radius":1015645,"preferred_band_width":8192,"band_count":124,"repaired_boundary_count":3,"max_abs_boundary_shift":1,"nominal_dirty_row_indices":[15,58,75],"prefix_row_index":0,"tileop_port_first_row_index":1},"required_k_sq":26,"bz_schedule":"repaired","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","band_count":124}
JSON
SH

cat > "$build_dir/source_origin_cpu_runner" <<'SH'
#!/usr/bin/env bash
manifest=""
witness=""
progress=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest-out)
      manifest="$2"; shift 2 ;;
    --prefix-witness-out)
      witness="$2"; shift 2 ;;
    --progress-out)
      progress="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$manifest" ]] && echo "manifest" > "$manifest"
[[ -n "$witness" ]] && echo "witness" > "$witness"
[[ -n "$progress" ]] && echo '{"schema":"lb_source_origin_progress_v1","accepted":true}' > "$progress"
cat <<'JSON'
{"schema":"lb_source_origin_cpu_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","k_sq":26,"r_final":8192,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"manifest_written":true,"prefix_witness_written":true}
JSON
SH

cat > "$build_dir/source_tileop_port_runner" <<'SH'
#!/usr/bin/env bash
if [[ "$*" != *"--target-a 376039 --target-b 943460"* ]]; then
  echo "missing K26 target bridge flags" >&2
  exit 1
fi
progress=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --progress-out)
      progress="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$progress" ]] && echo '{"schema":"lb_source_tileop_port_progress_v1","accepted":true}' > "$progress"
cat <<'JSON'
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"diagnostic_allow_unbridged","k_sq":26,"r_start":8192,"r_final":1015645,"schedule_mode":"explicit_radii","schedule_boundary_count":124,"tileop_overflows":0,"unbridged_coordinate_carry_atoms":1249,"source_bridged_coordinate_carry_atoms":1369,"source_unbridged_coordinate_carry_atoms":1211,"source_unbridged_without_next_band_candidates":1154,"source_unbridged_with_next_band_candidates":57,"source_unbridged_dead_end_candidate_atoms":57,"source_unbridged_unsafe_candidate_atoms":0,"source_bridge_rejected_candidate_atoms":72,"target":{"enabled":true,"a":376039,"b":943460,"norm_sq":1031522101121,"seen":true,"port_atoms":9,"bridge_edges":9,"source_reached":true,"path_provenance":"mixed_coordinate_port_atom_chain_non_claim","atom_path_length":3,"atom_path":[1615075207963900,-25220051735553,1615075207964004]},"accepted":true,"terminal_source_dead":true,"has_source_carry":false,"source_inventory_count":14542615005,"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1","source_inventory_digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_source_norm_sq":1031522101121,"max_source_norm_atom_ids":[1615075207964004]}
JSON
SH

chmod +x "$build_dir"/*

fake_source_dead_gap_checker="$tmp/fake-source-dead-gap-checker"
cat > "$fake_source_dead_gap_checker" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
if grep -q '"schema":"lb_source_k26_source_dead_gap_v1"' "$1" &&
    grep -q '"target_atom_path":\[1615075207963900,-25220051735553,1615075207964004\]' "$1"; then
  echo '{"status":"SOURCE_DEAD_GAP_NON_CLAIM_PASS","bridge_safety":"accepted_non_claim","coordinate_path_obligation":"blocked_coordinate_gaussian_prime_path","terminal_inventory_obligation":"blocked_claim_grade_terminal_inventory","claim_grade":false}'
else
  echo 'SOURCE_DEAD_GAP_REJECT: bad fixture' >&2
  exit 1
fi
SH
chmod +x "$fake_source_dead_gap_checker"

if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$tmp/bad-timeout" \
    --timeout-seconds nope \
    >/tmp/k26-harness-bad-timeout.out \
    2>/tmp/k26-harness-bad-timeout.err; then
  echo "harness accepted nonnumeric timeout" >&2
  exit 1
fi
grep -q -- '--timeout-seconds must be a nonnegative integer' \
  /tmp/k26-harness-bad-timeout.err

bad_cache_build="$tmp/bad-cache-build"
cp -R "$build_dir" "$bad_cache_build"
cat > "$bad_cache_build/CMakeCache.txt" <<'CACHE'
K_SQ:STRING=36
CACHE
if "$harness" \
    --build-dir "$bad_cache_build" \
    --out-dir "$tmp/bad-cache-out" \
    >/tmp/k26-harness-bad-cache.out \
    2>/tmp/k26-harness-bad-cache.err; then
  echo "harness accepted a non-K26 build cache" >&2
  exit 1
fi
grep -q 'requires build configured with -DK_SQ=26; found K_SQ=36' \
  /tmp/k26-harness-bad-cache.err

timeout_build="$tmp/timeout-build"
cp -R "$build_dir" "$timeout_build"
cat > "$timeout_build/source_tileop_port_runner" <<'SH'
#!/usr/bin/env bash
sleep 2
echo '{"schema":"should_not_finish"}'
SH
chmod +x "$timeout_build/source_tileop_port_runner"
timeout_out="$tmp/timeout-out"
if "$harness" \
    --build-dir "$timeout_build" \
    --out-dir "$timeout_out" \
    --timeout-seconds 1 \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-timeout.out \
    2>/tmp/k26-harness-timeout.err; then
  echo "harness accepted a timed-out K26 continuation" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_K26_CONTINUATION_TIMEOUT' \
  "$timeout_out/status.txt"
grep -q 'timeout_seconds=1' "$timeout_out/status.txt"
grep -q 'K26_CONTINUATION timed out after 1s' \
  /tmp/k26-harness-timeout.err

blocked_out="$tmp/blocked"
if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$blocked_out" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-blocked.out 2>/tmp/k26-harness-blocked.err; then
  echo "harness accepted a run without k26-source-dead-cert.json" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING' \
  "$blocked_out/status.txt"
grep -q 'source_dead_gap_check_status=SOURCE_DEAD_GAP_NON_CLAIM_PASS' \
  "$blocked_out/status.txt"
grep -q 'source_dead_gap_bridge_safety=accepted_non_claim' \
  "$blocked_out/status.txt"
grep -q 'source_dead_gap_coordinate_path_obligation=blocked_coordinate_gaussian_prime_path' \
  "$blocked_out/status.txt"
grep -q 'source_dead_gap_terminal_inventory_obligation=blocked_claim_grade_terminal_inventory' \
  "$blocked_out/status.txt"
grep -q 'source_dead_gap_claim_grade=false' \
  "$blocked_out/status.txt"
test -f "$blocked_out/k26-prefix-result.json"
test -f "$blocked_out/k26-prefix-progress.jsonl"
test -f "$blocked_out/k26-continuation-result.json"
test -f "$blocked_out/k26-continuation-progress.jsonl"
test -f "$blocked_out/k26-source-dead-gap.json"
test -f "$blocked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-continuation-result.json' \
  "$blocked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-prefix-progress.jsonl' \
  "$blocked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-continuation-progress.jsonl' \
  "$blocked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-source-dead-gap.json' \
  "$blocked_out/k26-full-run-artifacts.sha256"
grep -q '"schema":"lb_source_k26_source_dead_gap_v1"' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"blocker":"SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING"' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"bz_evidence":{"status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","accepted_for_schedule":true,"accepted_for_claim":false' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95"' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"bridge_safety":{"seam_bridge_policy":"diagnostic_allow_unbridged","source_bridged_coordinate_carry_atoms":1369' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"source_unbridged_unsafe_candidate_atoms":0' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"target_path_provenance":"mixed_coordinate_port_atom_chain_non_claim"' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"target_atom_path":\[1615075207963900,-25220051735553,1615075207964004\]' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"coordinate_path_obligation":{"required_provenance":"coordinate_gaussian_prime_path","observed_provenance":"mixed_coordinate_port_atom_chain_non_claim","observed_coordinate_atom_count":2,"observed_port_atom_count":1' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q '"terminal_inventory_obligation":{"required_mode":"claim_grade_terminal_inventory","observed_mode":"summary_digest_only_non_claim","listed_inventory_present":false,"claim_grade_inventory_accepted":false' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q 'coordinate Gaussian-prime source_path' \
  "$blocked_out/k26-source-dead-gap.json"
grep -q 'SOURCE_DEAD_GAP_NON_CLAIM_PASS' \
  "$blocked_out/k26-source-dead-gap-check.log"
if grep -q 'k26-source-dead-cert.json' \
    "$blocked_out/k26-full-run-artifacts.sha256"; then
  echo "blocked partial manifest unexpectedly included missing cert" >&2
  exit 1
fi

cert="$tmp/k26-source-dead-cert.json"
continuation_digest="$("$build_dir/source_tileop_port_runner" \
  --target-a 376039 --target-b 943460 \
  | shasum -a 256 | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
cat > "$cert" <<JSON
{"schema":"lb_source_dead_cert_draft_v1","certificate_id":"k26-source-dead-cert-draft","profile_id":"k26-source-run-profile","metadata":{"source_mode":"ORIGIN_SOURCE","source_id":"omega","geometry_id":"SOURCE_ORIGIN_K26","commit_id":"abc123","build_id":"remote-test","bz_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","artifact_hash":"sha256:$continuation_digest"},"k_sq":26,"terminal_radius":1015645,"negative_guard_pass":true,"endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"endpoint_atom_id":1615075207964004,"source_path_provenance":"coordinate_gaussian_prime_path","source_path":[{"a":376039,"b":943460,"norm_sq":1031522101121}],"terminal_source_inventory_summary":{"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]}}
JSON

fake_source_dead_checker="$tmp/fake-source-dead-checker"
cat > "$fake_source_dead_checker" <<'SH'
#!/usr/bin/env bash
if grep -q '"proof_status":"SUMMARY_ONLY_NON_CLAIM"' "$1"; then
  echo '{"status":"SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM_PASS"}'
elif grep -q '"terminal_source_inventory_summary":{"count":14542615005' "$1"; then
  echo '{"status":"SOURCE_DEAD_CERT_DRAFT_PASS"}'
else
  exit 1
fi
SH
chmod +x "$fake_source_dead_checker"

missing_gap_checker_out="$tmp/missing-gap-checker"
if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$missing_gap_checker_out" \
    --cert-in "$cert" \
    --source-dead-checker "$fake_source_dead_checker" \
    >/tmp/k26-harness-missing-gap-checker.out \
    2>/tmp/k26-harness-missing-gap-checker.err; then
  echo "harness accepted a cert-supplied run without source-dead gap checker" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_GAP_CHECKER_MISSING' \
  "$missing_gap_checker_out/status.txt"

checked_out="$tmp/checked"
"$harness" \
  --build-dir "$build_dir" \
  --out-dir "$checked_out" \
  --cert-in "$cert" \
  --source-dead-gap-checker "$fake_source_dead_gap_checker" \
  --source-dead-checker "$fake_source_dead_checker" \
  >/tmp/k26-harness-checked.out
grep -q 'K26_FULL_RUN_BUNDLE_CHECKED' "$checked_out/status.txt"
grep -q 'source_dead_gap_check_status=SOURCE_DEAD_GAP_NON_CLAIM_PASS' \
  "$checked_out/status.txt"
grep -q 'K26_FULL_RUN_BUNDLE_DRAFT_PASS' \
  "$checked_out/k26-full-run-bundle-check.log"
grep -q 'k26-source-dead-cert.json' \
  "$checked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-source-dead-gap.json' \
  "$checked_out/k26-full-run-artifacts.sha256"
grep -q 'SOURCE_DEAD_GAP_NON_CLAIM_PASS' \
  "$checked_out/k26-source-dead-gap-check.log"

summary_cert="$tmp/k26-source-dead-cert-summary-nonclaim.json"
cp "$cert" "$summary_cert"
perl -0pi -e 's/"metadata":/"proof_status":"SUMMARY_ONLY_NON_CLAIM","non_claim":"summary-only diagnostic inventory, not a SOURCE_DEAD_CERT","metadata":/' \
  "$summary_cert"
summary_out="$tmp/summary-checked"
if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$summary_out" \
    --cert-in "$summary_cert" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    --source-dead-checker "$fake_source_dead_checker" \
    >/tmp/k26-harness-summary.out \
    2>/tmp/k26-harness-summary.err; then
  echo "harness accepted a summary-only non-claim cert as checked K26 bundle" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM' \
  "$summary_out/status.txt"
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM' \
  "$summary_out/k26-full-run-bundle-check.log"

echo "k26 full-run harness self-test PASS"
