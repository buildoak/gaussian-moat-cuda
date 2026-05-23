#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_k26_full_run_harness.sh HARNESS" >&2
  exit 2
fi

harness="$1"
harness_dir="$(cd "$(dirname "$harness")" && pwd)"
bundle_checker="$harness_dir/check_k26_full_run_bundle.sh"
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
original_args="$*"
if [[ "$*" != *"--target-a 376039 --target-b 943460"* ]]; then
  echo "missing K26 target bridge flags" >&2
  exit 1
fi
if [[ -n "${EXPECT_TILEOP_THREADS:-}" &&
      "$*" != *"--tileop-threads ${EXPECT_TILEOP_THREADS}"* ]]; then
  echo "missing expected --tileop-threads ${EXPECT_TILEOP_THREADS}" >&2
  exit 1
fi
progress=""
manifest=""
r_start="8192"
r_final="1015645"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --r-start)
      r_start="$2"; shift 2 ;;
    --r-final)
      r_final="$2"; shift 2 ;;
    --manifest-out)
      manifest="$2"; shift 2 ;;
    --progress-out)
      progress="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
band_index=0
[[ -n "$progress" ]] && printf '{"schema":"lb_source_tileop_port_progress_v1","accepted":true,"band_index":%s,"r_start":%s,"r_outer":%s,"total_ms":1000,"has_source_carry":true,"terminal_source_dead":false}\n' "$band_index" "$r_start" "$r_final" > "$progress"
if [[ -n "$manifest" ]]; then
  echo "port-manifest-${r_final}" > "$manifest"
  cat <<JSON
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"diagnostic_allow_unbridged","k_sq":26,"r_start":8192,"r_final":${r_final},"schedule_mode":"explicit_radii","schedule_boundary_count":3,"tileop_overflows":0,"unbridged_coordinate_carry_atoms":1249,"source_coordinate_carry_atoms_with_next_band_candidates":1426,"source_bridged_coordinate_carry_atoms":1369,"source_unbridged_coordinate_carry_atoms":1211,"source_unbridged_without_next_band_candidates":1154,"source_unbridged_with_next_band_candidates":57,"source_unbridged_dead_end_candidate_atoms":57,"source_unbridged_unsafe_candidate_atoms":0,"source_bridge_rejected_candidate_atoms":72,"target":{"enabled":true,"a":376039,"b":943460,"norm_sq":1031522101121,"seen":false,"port_atoms":0,"bridge_edges":0,"source_reached":false,"path_provenance":"component_reachability_only","atom_path_length":0,"atom_path":[]},"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":7,"source_inventory_count":123,"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1","source_inventory_digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_source_norm_sq":67108837,"max_source_norm_atom_ids":[1],"manifest_written":true}
JSON
  exit 0
fi
cat <<'JSON' | python3 -c 'import json, os, sys; doc = json.load(sys.stdin); doc.pop("terminal_source_inventory_accumulator", None) if os.environ.get("OMIT_TERMINAL_ACCUMULATOR") else None; print(json.dumps(doc, separators=(",", ":")))'
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"diagnostic_allow_unbridged","k_sq":26,"r_start":8192,"r_final":1015645,"schedule_mode":"explicit_radii","schedule_boundary_count":124,"tileop_overflows":0,"unbridged_coordinate_carry_atoms":1249,"source_coordinate_carry_atoms_with_next_band_candidates":1426,"source_bridged_coordinate_carry_atoms":1369,"source_unbridged_coordinate_carry_atoms":1211,"source_unbridged_without_next_band_candidates":1154,"source_unbridged_with_next_band_candidates":57,"source_unbridged_dead_end_candidate_atoms":57,"source_unbridged_unsafe_candidate_atoms":0,"source_bridge_rejected_candidate_atoms":72,"target":{"enabled":true,"a":376039,"b":943460,"norm_sq":1031522101121,"seen":true,"port_atoms":9,"bridge_edges":9,"source_reached":true,"path_provenance":"mixed_coordinate_port_atom_chain_non_claim","atom_path_length":3,"atom_path":[1615075207963900,-25220051735553,1615075207964004]},"accepted":true,"terminal_source_dead":true,"has_source_carry":false,"source_inventory_count":14542615005,"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1","source_inventory_digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_source_norm_sq":1031522101121,"max_source_norm_atom_ids":[1615075207964004],"terminal_source_inventory_accumulator":{"mode":"summary_digest_only_non_claim","provenance":"terminal_component_inventory_accumulator","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]}}
JSON
SH

chmod +x "$build_dir"/*

fake_source_dead_gap_checker="$tmp/fake-source-dead-gap-checker"
cat > "$fake_source_dead_gap_checker" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
if grep -q '"schema":"lb_source_k26_source_dead_gap_v1"' "$1" &&
    grep -q '"target_atom_path":\[1615075207963900,-25220051735553,1615075207964004\]' "$1"; then
  echo '{"status":"SOURCE_DEAD_GAP_NON_CLAIM_PASS","bridge_safety":"accepted_non_claim","coordinate_path_obligation":"blocked_coordinate_gaussian_prime_path","bz_schedule_obligation":"blocked_schedule_only_non_claim","terminal_inventory_obligation":"blocked_claim_grade_terminal_inventory","claim_grade":false}'
elif grep -q '"schema":"lb_source_k26_source_dead_gap_v1"' "$1" &&
    grep -q '"blocker":"SOURCE_DEAD_CERT_TARGET_NOT_REACHED"' "$1" &&
    grep -q '"target_atom_path":\[\]' "$1"; then
  echo '{"status":"SOURCE_DEAD_GAP_NON_CLAIM_PASS","bridge_safety":"accepted_non_claim","coordinate_path_obligation":"blocked_target_not_reached","bz_schedule_obligation":"blocked_schedule_only_non_claim","terminal_inventory_obligation":"blocked_claim_grade_terminal_inventory","claim_grade":false}'
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

if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$tmp/bad-runtime" \
    --max-runtime-seconds nope \
    >/tmp/k26-harness-bad-runtime.out \
    2>/tmp/k26-harness-bad-runtime.err; then
  echo "harness accepted nonnumeric max runtime" >&2
  exit 1
fi
grep -q -- '--max-runtime-seconds must be a nonnegative integer' \
  /tmp/k26-harness-bad-runtime.err

if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$tmp/bad-tileop-threads" \
    --tileop-threads nope \
    >/tmp/k26-harness-bad-tileop-threads.out \
    2>/tmp/k26-harness-bad-tileop-threads.err; then
  echo "harness accepted nonnumeric TileOp thread count" >&2
  exit 1
fi
grep -q -- '--tileop-threads must be a nonnegative integer' \
  /tmp/k26-harness-bad-tileop-threads.err

if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$tmp/bad-chunk" \
    --continuation-chunk-bands nope \
    >/tmp/k26-harness-bad-chunk.out \
    2>/tmp/k26-harness-bad-chunk.err; then
  echo "harness accepted nonnumeric chunk size" >&2
  exit 1
fi
grep -q -- '--continuation-chunk-bands must be a nonnegative integer' \
  /tmp/k26-harness-bad-chunk.err

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
progress=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --progress-out)
      progress="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$progress" ]] && echo '{"schema":"lb_source_tileop_port_progress_v1","accepted":true,"band_index":0,"r_outer":16384,"total_ms":1000,"has_source_carry":true,"terminal_source_dead":false}' > "$progress"
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
grep -q 'runtime_budget_check_status=K26_RUNTIME_BUDGET_PASS' \
  "$timeout_out/status.txt"
grep -q 'runtime_budget_completed_band_count=1' "$timeout_out/status.txt"
grep -q 'runtime_budget_label=K26_CONTINUATION' "$timeout_out/status.txt"
grep -q 'runtime_budget_progress=k26-continuation-progress.jsonl' \
  "$timeout_out/status.txt"
grep -q 'runtime_budget_exit_code=0' "$timeout_out/status.txt"
grep -q 'K26_CONTINUATION timed out after 1s' \
  /tmp/k26-harness-timeout.err

runtime_limit_build="$tmp/runtime-limit-build"
cp -R "$build_dir" "$runtime_limit_build"
cat > "$runtime_limit_build/source_tileop_port_runner" <<'SH'
#!/usr/bin/env bash
progress=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --progress-out)
      progress="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$progress" ]] && echo '{"schema":"lb_source_tileop_port_progress_v1","accepted":true,"band_index":0,"r_outer":16384,"total_ms":1000,"has_source_carry":true,"terminal_source_dead":false}' > "$progress"
sleep 3
echo '{"schema":"should_not_finish"}'
SH
chmod +x "$runtime_limit_build/source_tileop_port_runner"
runtime_limit_out="$tmp/runtime-limit-out"
if "$harness" \
    --build-dir "$runtime_limit_build" \
    --out-dir "$runtime_limit_out" \
    --max-runtime-seconds 2 \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-runtime-limit.out \
    2>/tmp/k26-harness-runtime-limit.err; then
  echo "harness accepted a K26 continuation past total runtime limit" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_K26_CONTINUATION_RUNTIME_LIMIT' \
  "$runtime_limit_out/status.txt"
grep -q 'max_runtime_seconds=2' "$runtime_limit_out/status.txt"
grep -Eq 'K26_CONTINUATION (exceeded K26 bundle max runtime|not started because max runtime) 2s' \
  /tmp/k26-harness-runtime-limit.err

chunk_budget_build="$tmp/chunk-budget-build"
cp -R "$build_dir" "$chunk_budget_build"
cat > "$chunk_budget_build/source_tileop_port_runner" <<'SH'
#!/usr/bin/env bash
progress=""
manifest=""
r_start="8192"
r_final="1015645"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --r-start)
      r_start="$2"; shift 2 ;;
    --r-final)
      r_final="$2"; shift 2 ;;
    --manifest-out)
      manifest="$2"; shift 2 ;;
    --progress-out)
      progress="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$progress" ]] && printf '{"schema":"lb_source_tileop_port_progress_v1","accepted":true,"band_index":0,"r_start":%s,"r_outer":%s,"total_ms":200000,"has_source_carry":true,"terminal_source_dead":false}\n' "$r_start" "$r_final" > "$progress"
[[ -n "$manifest" ]] && echo "slow-port-manifest-${r_final}" > "$manifest"
cat <<JSON
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"diagnostic_allow_unbridged","k_sq":26,"r_start":${r_start},"r_final":${r_final},"schedule_mode":"explicit_radii","schedule_boundary_count":3,"tileop_overflows":0,"unbridged_coordinate_carry_atoms":0,"source_unbridged_unsafe_candidate_atoms":0,"target":{"enabled":true,"a":376039,"b":943460,"norm_sq":1031522101121,"seen":false,"port_atoms":0,"bridge_edges":0,"source_reached":false,"path_provenance":"component_reachability_only","atom_path_length":0,"atom_path":[]},"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":7,"source_inventory_count":123,"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1","source_inventory_digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_source_norm_sq":67108837,"max_source_norm_atom_ids":[1],"manifest_written":true}
JSON
SH
chmod +x "$chunk_budget_build/source_tileop_port_runner"
chunk_budget_out="$tmp/chunk-budget-out"
if "$harness" \
    --build-dir "$chunk_budget_build" \
    --out-dir "$chunk_budget_out" \
    --continuation-chunk-bands 2 \
    --max-runtime-seconds 14000 \
    >/tmp/k26-harness-chunk-budget.out \
    2>/tmp/k26-harness-chunk-budget.err; then
  echo "harness accepted an over-budget completed continuation chunk" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_K26_CONTINUATION_CHUNK_000_RUNTIME_BUDGET_REJECT' \
  "$chunk_budget_out/status.txt"
grep -q 'runtime_budget_check_status=K26_RUNTIME_BUDGET_REJECT' \
  "$chunk_budget_out/status.txt"
grep -q 'runtime_budget_label=K26_CONTINUATION_CHUNK_000_CUMULATIVE' \
  "$chunk_budget_out/status.txt"
grep -q 'runtime_budget_projected_total_seconds=24600' \
  "$chunk_budget_out/status.txt"
grep -q 'runtime_budget_tail_projected_total_seconds=24600' \
  "$chunk_budget_out/status.txt"
grep -q '"chunk_id":"000"' "$chunk_budget_out/k26-continuation-chunks.jsonl"
if grep -q 'K26_CONTINUATION_CHUNK_001' "$chunk_budget_out/run.log"; then
  echo "harness started chunk 001 after chunk 000 budget reject" >&2
  exit 1
fi

live_build="$tmp/live-build"
cp -R "$build_dir" "$live_build"
cat > "$live_build/source_tileop_port_runner" <<'SH'
#!/usr/bin/env bash
progress=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --progress-out)
      progress="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$progress" ]] && echo '{"schema":"lb_source_tileop_port_progress_v1","accepted":true,"band_index":0,"r_outer":1015645,"total_ms":1000,"has_source_carry":true,"terminal_source_dead":false}' > "$progress"
cat <<'JSON'
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"diagnostic_allow_unbridged","k_sq":26,"r_start":8192,"r_final":1015645,"schedule_mode":"explicit_radii","schedule_boundary_count":124,"tileop_overflows":0,"unbridged_coordinate_carry_atoms":0,"source_unbridged_unsafe_candidate_atoms":0,"target":{"enabled":true,"a":376039,"b":943460,"norm_sq":1031522101121,"seen":false,"port_atoms":0,"bridge_edges":0,"source_reached":false,"path_provenance":"component_reachability_only","atom_path_length":0,"atom_path":[]},"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":7,"source_inventory_count":123,"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1","source_inventory_digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_source_norm_sq":67108837,"max_source_norm_atom_ids":[1]}
JSON
SH
chmod +x "$live_build/source_tileop_port_runner"
live_out="$tmp/live-out"
if "$harness" \
    --build-dir "$live_build" \
    --out-dir "$live_out" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-live.out \
    2>/tmp/k26-harness-live.err; then
  echo "harness accepted a live-source K26 continuation as source-dead" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_STILL_LIVE' \
  "$live_out/status.txt"
grep -q 'source carry still survives at R_final' \
  /tmp/k26-harness-live.err
test -f "$live_out/k26-continuation-result.json"
test -f "$live_out/k26-full-run-artifacts.sha256"
if [[ -f "$live_out/k26-source-dead-gap.json" ]]; then
  echo "live-source blocker unexpectedly wrote source-dead gap" >&2
  exit 1
fi
if grep -q 'k26-source-dead-gap.json' \
    "$live_out/k26-full-run-artifacts.sha256"; then
  echo "live-source manifest unexpectedly included source-dead gap" >&2
  exit 1
fi

target_not_reached_build="$tmp/target-not-reached-build"
cp -R "$build_dir" "$target_not_reached_build"
cat > "$target_not_reached_build/source_tileop_port_runner" <<'SH'
#!/usr/bin/env bash
progress=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --progress-out)
      progress="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$progress" ]] && echo '{"schema":"lb_source_tileop_port_progress_v1","accepted":true,"band_index":0,"r_outer":1015645,"total_ms":1000,"has_source_carry":false,"terminal_source_dead":true}' > "$progress"
cat <<'JSON'
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"diagnostic_allow_unbridged","k_sq":26,"r_start":8192,"r_final":1015645,"schedule_mode":"explicit_radii","schedule_boundary_count":124,"tileop_overflows":0,"unbridged_coordinate_carry_atoms":1249,"source_coordinate_carry_atoms_with_next_band_candidates":1426,"source_bridged_coordinate_carry_atoms":1369,"source_unbridged_coordinate_carry_atoms":1211,"source_unbridged_without_next_band_candidates":1154,"source_unbridged_with_next_band_candidates":57,"source_unbridged_dead_end_candidate_atoms":57,"source_unbridged_unsafe_candidate_atoms":0,"source_bridge_rejected_candidate_atoms":72,"target":{"enabled":true,"a":376039,"b":943460,"norm_sq":1031522101121,"seen":false,"port_atoms":0,"bridge_edges":0,"source_reached":false,"path_provenance":"component_reachability_only","atom_path_length":0,"atom_path":[]},"accepted":true,"terminal_source_dead":true,"has_source_carry":false,"source_inventory_count":2022302,"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1","source_inventory_digest_hex":"7463f2808ab896cd47b740719c3d8df5e321c1f978bf8b232ab3f48858b7901f","max_source_norm_sq":279511040,"max_source_norm_atom_ids":[-463856527362],"terminal_source_inventory_accumulator":{"mode":"summary_digest_only_non_claim","provenance":"terminal_component_inventory_accumulator","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"count":2022302,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"7463f2808ab896cd47b740719c3d8df5e321c1f978bf8b232ab3f48858b7901f","max_norm_sq":279511040,"max_norm_atom_ids":[-463856527362]}}
JSON
SH
chmod +x "$target_not_reached_build/source_tileop_port_runner"
target_not_reached_out="$tmp/target-not-reached-out"
if "$harness" \
    --build-dir "$target_not_reached_build" \
    --out-dir "$target_not_reached_out" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-target-not-reached.out \
    2>/tmp/k26-harness-target-not-reached.err; then
  echo "harness accepted target-not-reached terminal death as source-dead" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_TARGET_NOT_REACHED' \
  "$target_not_reached_out/status.txt"
grep -q 'source_dead_gap_coordinate_path_obligation=blocked_target_not_reached' \
  "$target_not_reached_out/status.txt"
grep -q 'canonical Tsuchimura endpoint was not source-reached' \
  /tmp/k26-harness-target-not-reached.err
test -f "$target_not_reached_out/k26-source-dead-gap.json"
test -f "$target_not_reached_out/k26-full-run-artifacts.sha256"
grep -q 'k26-source-dead-gap.json' \
  "$target_not_reached_out/k26-full-run-artifacts.sha256"
grep -q '"blocker":"SOURCE_DEAD_CERT_TARGET_NOT_REACHED"' \
  "$target_not_reached_out/k26-source-dead-gap.json"
grep -q '"target_path_provenance":"component_reachability_only"' \
  "$target_not_reached_out/k26-source-dead-gap.json"
grep -q '"target_atom_path_length":0' \
  "$target_not_reached_out/k26-source-dead-gap.json"
grep -q '"target_atom_path":\[\]' \
  "$target_not_reached_out/k26-source-dead-gap.json"
if [[ -f "$target_not_reached_out/k26-source-dead-cert.json" ]]; then
  echo "target-not-reached blocker unexpectedly accepted/copied a cert" >&2
  exit 1
fi

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
grep -q 'source_dead_gap_bz_schedule_obligation=blocked_schedule_only_non_claim' \
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
grep -q '"bridge_safety":{"seam_bridge_policy":"diagnostic_allow_unbridged","source_coordinate_carry_atoms_with_next_band_candidates":1426,"source_bridged_coordinate_carry_atoms":1369' \
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

missing_accumulator_out="$tmp/missing-terminal-accumulator"
if OMIT_TERMINAL_ACCUMULATOR=1 "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$missing_accumulator_out" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-missing-terminal-accumulator.out \
    2>/tmp/k26-harness-missing-terminal-accumulator.err; then
  echo "harness synthesized a source-dead gap without terminal accumulator evidence" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_TERMINAL_ACCUMULATOR_MISSING' \
  "$missing_accumulator_out/status.txt"
test ! -f "$missing_accumulator_out/k26-source-dead-gap.json"

threaded_out="$tmp/threaded"
if EXPECT_TILEOP_THREADS=6 "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$threaded_out" \
    --tileop-threads 6 \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-threaded.out 2>/tmp/k26-harness-threaded.err; then
  echo "harness accepted a thread-pinned run without k26-source-dead-cert.json" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING' \
  "$threaded_out/status.txt"
grep -q 'tileop_threads=6' "$threaded_out/status.txt"
grep -q -- '--tileop-threads 6' "$threaded_out/run.log"

chunked_out="$tmp/chunked"
if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$chunked_out" \
    --continuation-chunk-bands 2 \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-chunked.out 2>/tmp/k26-harness-chunked.err; then
  echo "harness accepted a chunked run without k26-source-dead-cert.json" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING' \
  "$chunked_out/status.txt"
grep -q 'continuation_chunk_bands=2' "$chunked_out/status.txt"
grep -q 'runtime_budget_check_status=K26_RUNTIME_BUDGET_PASS' \
  "$chunked_out/status.txt"
grep -q 'runtime_budget_completed_band_count=2' "$chunked_out/status.txt"
grep -q 'runtime_budget_progress=k26-continuation-progress.jsonl' \
  "$chunked_out/status.txt"
test -f "$chunked_out/k26-continuation-result.json"
test -f "$chunked_out/k26-continuation-progress.jsonl"
test -f "$chunked_out/k26-continuation-chunks.jsonl"
test -f "$chunked_out/k26-continuation-chunk-000.json"
test -f "$chunked_out/k26-continuation-chunk-000.progress.jsonl"
test -f "$chunked_out/k26-continuation-chunk-000.manifest.txt"
test -f "$chunked_out/k26-continuation-chunk-001.json"
test -f "$chunked_out/k26-continuation-chunk-001.progress.jsonl"
grep -q '"chunk_id":"000","action":"executed","schedule_segment_start":0,"schedule_segment_end":2,"schedule_segment_count":2,"r_start":8192,"r_final":475135,"schedule_radii_csv":"8192,122879,475135"' \
  "$chunked_out/k26-continuation-chunks.jsonl"
grep -q '"chunk_id":"001","action":"executed","schedule_segment_start":2,"schedule_segment_end":4,"schedule_segment_count":2,"r_start":475135,"r_final":1015645,"schedule_radii_csv":"475135,622591,1015645"' \
  "$chunked_out/k26-continuation-chunks.jsonl"
grep -q 'k26-continuation-chunk-000.json' \
  "$chunked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-continuation-chunk-000.manifest.txt' \
  "$chunked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-continuation-chunk-001.json' \
  "$chunked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-continuation-chunks.jsonl' \
  "$chunked_out/k26-full-run-artifacts.sha256"
grep -q '"terminal_source_dead":true' \
  "$chunked_out/k26-continuation-result.json"
grep -q '"chunk_ledger_artifact":{"name":"k26-continuation-chunks.jsonl","sha256":"[0-9a-f]\{64\}"' \
  "$chunked_out/k26-source-dead-gap.json"
grep -q '"bridge_source_artifact":{"name":"k26-continuation-chunk-000.json","sha256":"[0-9a-f]\{64\}"' \
  "$chunked_out/k26-source-dead-gap.json"

resume_out="$tmp/resume-existing"
mkdir -p "$resume_out"
cp "$chunked_out"/k26-prefix-result.json "$resume_out"/
cp "$chunked_out"/k26-prefix-progress.jsonl "$resume_out"/
cp "$chunked_out"/k26-prefix-manifest.txt "$resume_out"/
cp "$chunked_out"/k26-prefix-witness.txt "$resume_out"/
cp "$chunked_out"/k26-continuation-chunk-000.json "$resume_out"/
cp "$chunked_out"/k26-continuation-chunk-000.progress.jsonl "$resume_out"/
cp "$chunked_out"/k26-continuation-chunk-000.manifest.txt "$resume_out"/
if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$resume_out" \
    --continuation-chunk-bands 2 \
    --resume-existing \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    >/tmp/k26-harness-resume-existing.out \
    2>/tmp/k26-harness-resume-existing.err; then
  echo "harness accepted a resumed chunked run without k26-source-dead-cert.json" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING' \
  "$resume_out/status.txt"
grep -q 'resume_existing=1' "$resume_out/status.txt"
grep -q 'SKIP K26_PREFIX: existing prefix artifacts' "$resume_out/run.log"
grep -q 'SKIP K26_CONTINUATION_CHUNK_000: existing complete chunk' \
  "$resume_out/run.log"
grep -q 'RUN K26_CONTINUATION_CHUNK_001:' "$resume_out/run.log"
test -f "$resume_out/k26-continuation-result.json"
test -f "$resume_out/k26-continuation-chunks.jsonl"
test -f "$resume_out/k26-continuation-chunk-001.json"
grep -q '"chunk_id":"000","action":"reused"' \
  "$resume_out/k26-continuation-chunks.jsonl"
grep -q '"chunk_id":"001","action":"executed"' \
  "$resume_out/k26-continuation-chunks.jsonl"
grep -q 'k26-continuation-chunk-000.manifest.txt' \
  "$resume_out/k26-full-run-artifacts.sha256"
grep -q 'k26-continuation-chunk-001.json' \
  "$resume_out/k26-full-run-artifacts.sha256"
grep -q 'k26-continuation-chunks.jsonl' \
  "$resume_out/k26-full-run-artifacts.sha256"

cert="$tmp/k26-source-dead-cert.json"
continuation_digest="$("$build_dir/source_tileop_port_runner" \
  --target-a 376039 --target-b 943460 \
  | shasum -a 256 | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
cat > "$cert" <<JSON
{"schema":"lb_source_dead_cert_draft_v1","certificate_id":"k26-source-dead-cert-draft","profile_id":"k26-source-run-profile","metadata":{"source_mode":"ORIGIN_SOURCE","source_id":"omega","geometry_id":"SOURCE_ORIGIN_K26","commit_id":"abc123","build_id":"remote-test","bz_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","artifact_hash":"sha256:$continuation_digest"},"k_sq":26,"terminal_radius":1015645,"negative_guard_pass":true,"endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"endpoint_atom_id":1615075207964004,"source_path_provenance":"coordinate_gaussian_prime_path","source_path":[{"a":0,"b":3,"norm_sq":9},{"a":376039,"b":943460,"norm_sq":1031522101121}],"terminal_source_inventory_summary":{"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]}}
JSON

fake_source_dead_checker="$tmp/fake-source-dead-checker"
cat > "$fake_source_dead_checker" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
python3 - "$1" <<'PY'
import json
import sys

cert = json.loads(open(sys.argv[1]).read())
if cert.get("proof_status") == "SUMMARY_ONLY_NON_CLAIM":
    if cert.get("terminal_source_inventory_mode") != "summary_only_non_claim":
        print("SOURCE_DEAD_CERT_DRAFT_REJECT: missing summary mode", file=sys.stderr)
        raise SystemExit(1)
    accumulator = cert.get("terminal_source_inventory_accumulator", {})
    if accumulator.get("mode") != "summary_digest_only_non_claim":
        print("SOURCE_DEAD_CERT_DRAFT_REJECT: missing accumulator", file=sys.stderr)
        raise SystemExit(1)
    if accumulator.get("claim_grade_inventory_accepted") is not False:
        print("SOURCE_DEAD_CERT_DRAFT_REJECT: accumulator claims grade", file=sys.stderr)
        raise SystemExit(1)
    print('{"status":"SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM_PASS"}')
    raise SystemExit(0)

if cert.get("schema") != "lb_source_dead_cert_draft_v1":
    raise SystemExit(1)

if cert.get("metadata", {}).get("geometry_id") == "SOURCE_ORIGIN_K26":
    source_path = cert.get("source_path", [])
    if len(source_path) < 2 or source_path[0].get("norm_sq", 10**30) > 26:
        print(
            "SOURCE_DEAD_CERT_DRAFT_REJECT: bad K26 origin path fixture",
            file=sys.stderr,
        )
        raise SystemExit(1)

summary = cert.get("terminal_source_inventory_summary", {})
if summary.get("count") == 14542615005:
    print('{"status":"SOURCE_DEAD_CERT_DRAFT_PASS"}')
else:
    raise SystemExit(1)
PY
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
grep -q 'source_dead_gap_bz_schedule_obligation=blocked_schedule_only_non_claim' \
  "$checked_out/status.txt"
grep -q 'K26_FULL_RUN_BUNDLE_DRAFT_PASS' \
  "$checked_out/k26-full-run-bundle-check.log"
grep -q 'k26-source-dead-cert.json' \
  "$checked_out/k26-full-run-artifacts.sha256"
grep -q 'k26-source-dead-gap.json' \
  "$checked_out/k26-full-run-artifacts.sha256"
grep -q 'SOURCE_DEAD_GAP_NON_CLAIM_PASS' \
  "$checked_out/k26-source-dead-gap-check.log"

bad_origin_path_cert="$tmp/bad-origin-path-cert.json"
cp "$cert" "$bad_origin_path_cert"
perl -0pi -e 's/"source_path":\[\{"a":0,"b":3,"norm_sq":9\},\{"a":376039,"b":943460,"norm_sq":1031522101121\}\]/"source_path":[{"a":376039,"b":943460,"norm_sq":1031522101121}]/' \
  "$bad_origin_path_cert"
bad_origin_path_out="$tmp/bad-origin-path"
if "$harness" \
    --build-dir "$build_dir" \
    --out-dir "$bad_origin_path_out" \
    --cert-in "$bad_origin_path_cert" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    --source-dead-checker "$fake_source_dead_checker" \
    >/tmp/k26-harness-bad-origin-path.out \
    2>/tmp/k26-harness-bad-origin-path.err; then
  echo "harness accepted a K26 source-dead cert whose path starts at endpoint" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_BUNDLE_CHECK_REJECTED' \
  "$bad_origin_path_out/status.txt"
grep -q 'source-dead checker did not accept draft cert' \
  "$bad_origin_path_out/k26-full-run-bundle-check.log"

checked_chunked_out="$tmp/checked-chunked"
"$harness" \
  --build-dir "$build_dir" \
  --out-dir "$checked_chunked_out" \
  --continuation-chunk-bands 2 \
  --cert-in "$cert" \
  --source-dead-gap-checker "$fake_source_dead_gap_checker" \
  --source-dead-checker "$fake_source_dead_checker" \
  >/tmp/k26-harness-checked-chunked.out
grep -q 'K26_FULL_RUN_BUNDLE_CHECKED' "$checked_chunked_out/status.txt"
grep -q 'K26_FULL_RUN_BUNDLE_DRAFT_PASS' \
  "$checked_chunked_out/k26-full-run-bundle-check.log"
test -f "$checked_chunked_out/k26-continuation-chunks.jsonl"
grep -q '"chunk_id":"000","action":"executed"' \
  "$checked_chunked_out/k26-continuation-chunks.jsonl"
grep -q 'k26-continuation-chunks.jsonl' \
  "$checked_chunked_out/k26-full-run-artifacts.sha256"
grep -q '"chunk_ledger_artifact":{"name":"k26-continuation-chunks.jsonl","sha256":"[0-9a-f]\{64\}"' \
  "$checked_chunked_out/k26-source-dead-gap.json"
grep -q '"bridge_source_artifact":{"name":"k26-continuation-chunk-000.json","sha256":"[0-9a-f]\{64\}"' \
  "$checked_chunked_out/k26-source-dead-gap.json"

bad_ledger_out="$tmp/bad-ledger"
cp -R "$checked_chunked_out" "$bad_ledger_out"
perl -0pi -e 's/"schedule_segment_start":2/"schedule_segment_start":3/' \
  "$bad_ledger_out/k26-continuation-chunks.jsonl"
bad_ledger_digest="$(
  shasum -a 256 "$bad_ledger_out/k26-continuation-chunks.jsonl" |
    sed -nE 's/^([0-9a-f]{64}) .*/\1/p'
)"
perl -0pi -e \
  "s/[0-9a-f]{64}  k26-continuation-chunks\\.jsonl/${bad_ledger_digest}  k26-continuation-chunks.jsonl/" \
  "$bad_ledger_out/k26-full-run-artifacts.sha256"
if "$bundle_checker" "$bad_ledger_out" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    --source-dead-checker "$fake_source_dead_checker" \
    >/tmp/k26-harness-bad-ledger.out \
    2>/tmp/k26-harness-bad-ledger.err; then
  echo "bundle checker accepted a malformed K26 chunk ledger" >&2
  exit 1
fi
grep -q 'K26 chunk ledger' /tmp/k26-harness-bad-ledger.err

bad_gap_ledger_out="$tmp/bad-gap-ledger"
cp -R "$checked_chunked_out" "$bad_gap_ledger_out"
perl -0pi -e 's/"chunk_ledger_artifact":\{"name":"k26-continuation-chunks\.jsonl","sha256":"[0-9a-f]{64}"/"chunk_ledger_artifact":{"name":"k26-continuation-chunks.jsonl","sha256":"0000000000000000000000000000000000000000000000000000000000000000"/' \
  "$bad_gap_ledger_out/k26-source-dead-gap.json"
bad_gap_digest="$(
  shasum -a 256 "$bad_gap_ledger_out/k26-source-dead-gap.json" |
    sed -nE 's/^([0-9a-f]{64}) .*/\1/p'
)"
perl -0pi -e \
  "s/[0-9a-f]{64}  k26-source-dead-gap\\.json/${bad_gap_digest}  k26-source-dead-gap.json/" \
  "$bad_gap_ledger_out/k26-full-run-artifacts.sha256"
if "$bundle_checker" "$bad_gap_ledger_out" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    --source-dead-checker "$fake_source_dead_checker" \
    >/tmp/k26-harness-bad-gap-ledger.out \
    2>/tmp/k26-harness-bad-gap-ledger.err; then
  echo "bundle checker accepted a source-dead gap with mismatched chunk ledger hash" >&2
  exit 1
fi
grep -q 'K26 gap chunk ledger hash binding' \
  /tmp/k26-harness-bad-gap-ledger.err

summary_cert="$tmp/k26-source-dead-cert-summary-nonclaim.json"
cat > "$summary_cert" <<JSON
{"schema":"lb_source_dead_cert_draft_v1","certificate_id":"k26-source-dead-cert-summary-nonclaim","profile_id":"k26-source-run-profile","proof_status":"SUMMARY_ONLY_NON_CLAIM","non_claim":"summary-only diagnostic inventory, not a SOURCE_DEAD_CERT","terminal_source_inventory_mode":"summary_only_non_claim","metadata":{"source_mode":"ORIGIN_SOURCE","source_id":"omega","geometry_id":"SOURCE_ORIGIN_K26","commit_id":"abc123","build_id":"remote-test","bz_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","artifact_hash":"sha256:$continuation_digest"},"k_sq":26,"terminal_radius":1015645,"negative_guard_pass":true,"endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"endpoint_atom_id":1615075207964004,"source_path_provenance":"coordinate_gaussian_prime_path","source_path":[{"a":0,"b":3,"norm_sq":9},{"a":376039,"b":943460,"norm_sq":1031522101121}],"terminal_source_inventory_summary":{"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]},"terminal_source_inventory_accumulator":{"mode":"summary_digest_only_non_claim","provenance":"terminal_component_inventory_accumulator","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]}}
JSON
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
