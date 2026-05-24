#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_k26_full_run_bundle_checker.sh CHECKER" >&2
  exit 2
fi

checker="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fake_source_dead_checker="$tmp/fake-source-dead-checker"
cat > "$fake_source_dead_checker" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
python3 - "$1" <<'PY'
import json
import sys

path = sys.argv[1]
cert = json.loads(open(path).read())
if cert.get("schema") != "lb_source_dead_cert_draft_v1":
    print("SOURCE_DEAD_CERT_DRAFT_REJECT: bad fixture", file=sys.stderr)
    raise SystemExit(1)

if cert.get("metadata", {}).get("geometry_id") == "SOURCE_ORIGIN_K26":
    if cert.get("metadata", {}).get("bz_schedule_digest_hex") != "7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95":
        print(
            "SOURCE_DEAD_CERT_DRAFT_REJECT: bad K26 BZ digest fixture",
            file=sys.stderr,
        )
        raise SystemExit(1)
    source_path = cert.get("source_path", [])
    if len(source_path) < 2 or source_path[0].get("norm_sq", 10**30) > 26:
        print(
            "SOURCE_DEAD_CERT_DRAFT_REJECT: bad K26 origin path fixture",
            file=sys.stderr,
        )
        raise SystemExit(1)

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
    summary = cert.get("terminal_source_inventory_summary", {})
    source_path = cert.get("source_path", [])
    max_norm = int(summary.get("max_norm_sq", -1))
    ties = set(summary.get("max_norm_atom_ids", []))
    path_atoms = set()
    for point in source_path:
        atom_id = (int(point.get("a", -1)) << 32) | int(point.get("b", -1))
        path_atoms.add(atom_id)
        if int(point.get("norm_sq", -1)) > max_norm:
            print("SOURCE_DEAD_CERT_DRAFT_REJECT: summary max below path", file=sys.stderr)
            raise SystemExit(1)
        if int(point.get("norm_sq", -1)) == max_norm and atom_id not in ties:
            print("SOURCE_DEAD_CERT_DRAFT_REJECT: summary max tie omits path", file=sys.stderr)
            raise SystemExit(1)
    if int(summary.get("count", -1)) < len(path_atoms):
        print("SOURCE_DEAD_CERT_DRAFT_REJECT: summary count below path", file=sys.stderr)
        raise SystemExit(1)
    print('{"status":"SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM_PASS"}')
else:
    if cert.get("metadata", {}).get("geometry_id") == "SOURCE_ORIGIN_K26":
        if cert.get("terminal_source_inventory_mode") != "claim_grade_accumulator":
            print("SOURCE_DEAD_CERT_DRAFT_REJECT: missing claim accumulator mode", file=sys.stderr)
            raise SystemExit(1)
        accumulator = cert.get("terminal_source_inventory_accumulator", {})
        if accumulator.get("mode") != "claim_grade_digest_accumulator":
            print("SOURCE_DEAD_CERT_DRAFT_REJECT: missing claim accumulator", file=sys.stderr)
            raise SystemExit(1)
        for field in (
            "complete_stream_observed",
            "canonical_order",
            "duplicate_free",
            "retired_component_finalized",
            "overflow_checked",
        ):
            if accumulator.get(field) is not True:
                print(f"SOURCE_DEAD_CERT_DRAFT_REJECT: accumulator {field} false", file=sys.stderr)
                raise SystemExit(1)
        if accumulator.get("claim_grade_inventory_accepted") is not True:
            print("SOURCE_DEAD_CERT_DRAFT_REJECT: accumulator not claim grade", file=sys.stderr)
            raise SystemExit(1)
    print('{"status":"SOURCE_DEAD_CERT_DRAFT_PASS"}')
PY
SH
chmod +x "$fake_source_dead_checker"

fake_source_dead_gap_checker="$tmp/fake-source-dead-gap-checker"
cat > "$fake_source_dead_gap_checker" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
if grep -q '"schema":"lb_source_k26_source_dead_gap_v1"' "$1"; then
  echo '{"status":"SOURCE_DEAD_GAP_NON_CLAIM_PASS"}'
else
  echo 'SOURCE_DEAD_GAP_REJECT: bad fixture' >&2
  exit 1
fi
SH
chmod +x "$fake_source_dead_gap_checker"

write_manifest() {
  local dir="$1"
  : > "$dir/k26-full-run-artifacts.sha256"
  local name digest
  for name in \
      k26_source_run_commands.json \
      k26_bz_schedule_check.json \
      k26_source_run_profile.json \
      k26-prefix-result.json \
      k26-prefix-progress.jsonl \
      k26-continuation-result.json \
      k26-continuation-progress.jsonl \
      k26-prefix-live-handoff.txt \
      k26-prefix-witness.txt \
      k26-source-dead-gap.json \
      k26-source-dead-cert.json; do
    digest="$(shasum -a 256 "$dir/$name" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
    printf '%s  %s\n' "$digest" "$name" \
      >> "$dir/k26-full-run-artifacts.sha256"
  done
  while IFS= read -r path; do
    [[ -z "$path" ]] && continue
    name="${path#$dir/}"
    digest="$(shasum -a 256 "$path" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
    printf '%s  %s\n' "$digest" "$name" \
      >> "$dir/k26-full-run-artifacts.sha256"
  done < <(find "$dir" -maxdepth 1 -type f \
    \( -name 'k26-continuation-chunks.jsonl' \
       -o -name 'k26-continuation-chunk-*.json' \
       -o -name 'k26-continuation-chunk-*.live-handoff.txt' \
       -o -name 'k26-continuation-chunk-*.progress.jsonl' \) \
    -print | LC_ALL=C sort)
}

write_bundle() {
  local dir="$1"
  mkdir -p "$dir"
  cat > "$dir/k26_source_run_commands.json" <<'JSON'
{"schema":"lb_source_k26_run_commands_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":false,"target":{"tsuchimura_endpoint":{"a":943460,"b":376039,"norm_sq":1031522101121},"canonical_octant_endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121}},"prefix":{"command":"source_origin_cpu_runner --endpoint-a 376039 --endpoint-b 943460 --live-manifest-out k26-prefix-live-handoff.txt"},"continuation":{"r_start":8192,"r_final":1015645,"schedule_boundary_count":124,"schedule_segment_count":123,"schedule_min_width":8029,"schedule_max_width":8193,"schedule_radii_csv":"8192,122879,475135,622591,1015645","schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","seam_bridge_policy":"diagnostic_allow_unbridged","blocked_if_unbridged_coordinate_carry_atoms":false,"claim_grade_requires_source_unbridged_unsafe_candidate_atoms":0,"command":"source_tileop_port_runner --target-a 376039 --target-b 943460 --live-manifest-in k26-prefix-live-handoff.txt"}}
JSON
  cat > "$dir/k26_bz_schedule_check.json" <<'JSON'
{"schema":"lb_source_k26_bz_schedule_check_v1","proof_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","repaired_summary":{"bad_norm_count":0,"bz_clean":true}}
JSON
  cat > "$dir/k26_source_run_profile.json" <<'JSON'
{"schema":"lb_source_k26_run_profile_v1","claim_label":"SOURCE_ORIGIN_K26","profile_id":"k26-source-run-profile","profile_status":"RUN_PROFILE_DRAFT_NON_CLAIM","target":{"tsuchimura_endpoint":{"a":943460,"b":376039,"norm_sq":1031522101121},"canonical_octant_endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"expected_component_size":14542615005},"schedule":{"terminal_radius":1015645,"preferred_band_width":8192,"band_count":124,"repaired_boundary_count":3,"max_abs_boundary_shift":1,"nominal_dirty_row_indices":[15,58,75],"prefix_row_index":0,"tileop_port_first_row_index":1,"bz_evidence":{"accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95"}}}
JSON
  cat > "$dir/k26-prefix-result.json" <<'JSON'
{"schema":"lb_source_origin_cpu_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","k_sq":26,"r_final":8192,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"live_manifest_written":true,"prefix_witness_written":true}
JSON
  cat > "$dir/k26-prefix-progress.jsonl" <<'JSONL'
{"schema":"lb_source_origin_progress_v1","band_index":0,"r_start":0,"r_outer":8192,"accepted":true}
JSONL
  cat > "$dir/k26-continuation-result.json" <<'JSON'
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"diagnostic_allow_unbridged","k_sq":26,"r_start":8192,"r_final":1015645,"schedule_mode":"explicit_radii","schedule_boundary_count":124,"tileop_overflows":0,"unbridged_coordinate_carry_atoms":1249,"source_coordinate_carry_atoms_with_next_band_candidates":1426,"source_bridged_coordinate_carry_atoms":1369,"source_unbridged_coordinate_carry_atoms":1211,"source_unbridged_without_next_band_candidates":1154,"source_unbridged_with_next_band_candidates":57,"source_unbridged_dead_end_candidate_atoms":57,"source_unbridged_unsafe_candidate_atoms":0,"source_bridge_rejected_candidate_atoms":72,"target":{"enabled":true,"a":376039,"b":943460,"norm_sq":1031522101121,"seen":true,"port_atoms":9,"bridge_edges":9,"source_reached":true,"path_provenance":"mixed_coordinate_port_atom_chain_non_claim","atom_path_length":3,"atom_path":[1615075207963900,-25220051735553,1615075207964004],"prefix_witness_path":{"available":true,"target_atom_id":1615075207963900,"path_points":2,"seed_norm_sq":9,"target_norm_sq":1031325872257},"coordinate_port_expansions":{"required_edges":2,"available_edges":2,"path_points_total":4,"expansions":[{"coordinate_atom_id":1615075207963900,"port_atom_id":-25220051735553,"path_points":2,"coordinate_norm_sq":1031325872257,"port_witness_norm_sq":1031325872257,"path":[{"a":376039,"b":943356,"norm_sq":1031325872257},{"a":376039,"b":943356,"norm_sq":1031325872257}]},{"coordinate_atom_id":1615075207964004,"port_atom_id":-25220051735553,"path_points":2,"coordinate_norm_sq":1031522101121,"port_witness_norm_sq":1031325872257,"path":[{"a":376039,"b":943460,"norm_sq":1031522101121},{"a":376039,"b":943356,"norm_sq":1031325872257}]}]}},"accepted":true,"terminal_source_dead":true,"has_source_carry":false,"source_inventory_count":14542615005,"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1","source_inventory_digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_source_norm_sq":1031522101121,"max_source_norm_atom_ids":[1615075207964004],"terminal_source_inventory_accumulator":{"mode":"summary_digest_only_non_claim","provenance":"terminal_component_inventory_accumulator","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]}}
JSON
  cat > "$dir/k26-continuation-progress.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_progress_v1","band_index":0,"r_start":8192,"r_outer":122879,"accepted":true}
JSONL
  local continuation_digest
  continuation_digest="$(shasum -a 256 "$dir/k26-continuation-result.json" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  echo 'live-handoff' > "$dir/k26-prefix-live-handoff.txt"
  cat > "$dir/k26-prefix-witness.txt" <<'WITNESS'
LB_SOURCE_PREFIX_WITNESS_V1
k_sq 26
outer_radius 8192
witness_count 1
witness 1615075207963900 376039 943356 1031325872257 2
point 0 3 9
point 376039 943356 1031325872257
END
WITNESS
  local prefix_live_handoff_digest
  prefix_live_handoff_digest="$(shasum -a 256 "$dir/k26-prefix-live-handoff.txt" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  local prefix_witness_digest
  prefix_witness_digest="$(shasum -a 256 "$dir/k26-prefix-witness.txt" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  cat > "$dir/k26-continuation-chunk-000.json" <<'JSON'
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"diagnostic_allow_unbridged","terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":7,"source_coordinate_carry_atoms_with_next_band_candidates":1426,"source_bridged_coordinate_carry_atoms":1369,"source_unbridged_coordinate_carry_atoms":1211,"source_unbridged_without_next_band_candidates":1154,"source_unbridged_with_next_band_candidates":57,"source_unbridged_dead_end_candidate_atoms":57,"source_unbridged_unsafe_candidate_atoms":0,"source_bridge_rejected_candidate_atoms":72}
JSON
  echo '{"schema":"lb_source_tileop_port_progress_v1","accepted":true}' \
    > "$dir/k26-continuation-chunk-000.progress.jsonl"
  echo 'chunk-000-live-handoff' > "$dir/k26-continuation-chunk-000.live-handoff.txt"
  cp "$dir/k26-continuation-result.json" \
    "$dir/k26-continuation-chunk-001.json"
  echo '{"schema":"lb_source_tileop_port_progress_v1","accepted":true}' \
    > "$dir/k26-continuation-chunk-001.progress.jsonl"
  cat > "$dir/k26-continuation-chunks.jsonl" <<'JSONL'
{"schema":"lb_source_k26_continuation_chunk_v1","chunk_index":0,"chunk_id":"000","action":"executed","schedule_segment_start":0,"schedule_segment_end":2,"schedule_segment_count":2,"r_start":8192,"r_final":475135,"schedule_radii_csv":"8192,122879,475135","input_live_handoff":"k26-prefix-live-handoff.txt","output_live_handoff":"k26-continuation-chunk-000.live-handoff.txt","result":"k26-continuation-chunk-000.json","progress":"k26-continuation-chunk-000.progress.jsonl","final_chunk":false,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":7}
{"schema":"lb_source_k26_continuation_chunk_v1","chunk_index":1,"chunk_id":"001","action":"executed","schedule_segment_start":2,"schedule_segment_end":4,"schedule_segment_count":2,"r_start":475135,"r_final":1015645,"schedule_radii_csv":"475135,622591,1015645","input_live_handoff":"k26-continuation-chunk-000.live-handoff.txt","output_live_handoff":"","result":"k26-continuation-chunk-001.json","progress":"k26-continuation-chunk-001.progress.jsonl","final_chunk":true,"terminal_source_dead":true,"has_source_carry":false,"source_carry_atoms":null}
JSONL
  local chunk_ledger_digest
  chunk_ledger_digest="$(shasum -a 256 "$dir/k26-continuation-chunks.jsonl" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  local bridge_source_digest
  bridge_source_digest="$(shasum -a 256 "$dir/k26-continuation-chunk-000.json" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  cat > "$dir/k26-source-dead-cert.json" <<JSON
{"schema":"lb_source_dead_cert_draft_v1","certificate_id":"k26-source-dead-cert-draft","profile_id":"k26-source-run-profile","metadata":{"source_mode":"ORIGIN_SOURCE","source_id":"omega","geometry_id":"SOURCE_ORIGIN_K26","commit_id":"abc123","build_id":"remote-test","bz_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","bz_schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","bz_schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","artifact_hash":"sha256:$continuation_digest"},"terminal_source_inventory_mode":"claim_grade_accumulator","k_sq":26,"terminal_radius":1015645,"negative_guard_pass":true,"endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"endpoint_atom_id":1615075207964004,"source_path_provenance":"coordinate_gaussian_prime_path","source_path":[{"a":0,"b":3,"norm_sq":9},{"a":376039,"b":943356,"norm_sq":1031325872257},{"a":376039,"b":943460,"norm_sq":1031522101121}],"terminal_source_inventory_summary":{"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]},"terminal_source_inventory_accumulator":{"mode":"claim_grade_digest_accumulator","provenance":"terminal_component_inventory_accumulator","accumulator_algorithm":"sha256:lb_source_inventory_v1","complete_stream_observed":true,"canonical_order":true,"duplicate_free":true,"retired_component_finalized":true,"overflow_checked":true,"listed_inventory_present":false,"claim_grade_inventory_accepted":true,"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]}}
JSON
  cat > "$dir/k26-source-dead-gap.json" <<JSON
{"schema":"lb_source_k26_source_dead_gap_v1","claim_label":"SOURCE_ORIGIN_K26","proof_status":"DIAGNOSTIC_NON_CLAIM","blocker":"SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING","non_claim":"executed prefix and continuation evidence only; not a SOURCE_DEAD_CERT","k_sq":26,"terminal_radius":1015645,"target":{"tsuchimura_endpoint":{"a":943460,"b":376039,"norm_sq":1031522101121},"canonical_octant_endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121}},"prefix_live_handoff_artifact":{"name":"k26-prefix-live-handoff.txt","sha256":"$prefix_live_handoff_digest"},"prefix_witness_artifact":{"name":"k26-prefix-witness.txt","sha256":"$prefix_witness_digest"},"continuation_artifact":{"name":"k26-continuation-result.json","sha256":"$continuation_digest"},"chunk_ledger_artifact":{"name":"k26-continuation-chunks.jsonl","sha256":"$chunk_ledger_digest"},"bridge_source_artifact":{"name":"k26-continuation-chunk-000.json","sha256":"$bridge_source_digest"},"bz_evidence":{"status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95"},"bz_schedule_obligation":{"required_status":"claim_grade_bz_schedule","observed_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","observed_schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","observed_schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","accepted_for_schedule":true,"accepted_for_claim":false,"claim_grade_bz_accepted":false},"bridge_safety":{"seam_bridge_policy":"diagnostic_allow_unbridged","source_coordinate_carry_atoms_with_next_band_candidates":1426,"source_bridged_coordinate_carry_atoms":1369,"source_unbridged_coordinate_carry_atoms":1211,"source_unbridged_without_next_band_candidates":1154,"source_unbridged_with_next_band_candidates":57,"source_unbridged_dead_end_candidate_atoms":57,"source_unbridged_unsafe_candidate_atoms":0,"source_bridge_rejected_candidate_atoms":72},"target_path_provenance":"mixed_coordinate_port_atom_chain_non_claim","target_atom_path_length":3,"target_atom_path":[1615075207963900,-25220051735553,1615075207964004],"coordinate_path_obligation":{"required_provenance":"coordinate_gaussian_prime_path","observed_provenance":"mixed_coordinate_port_atom_chain_non_claim","observed_coordinate_atom_count":2,"observed_port_atom_count":1,"origin_prefix_witness_artifact":"k26-prefix-witness.txt","origin_prefix_witness_target_atom_id":1615075207963900,"origin_prefix_witness_accepted":true,"origin_prefix_witness_path_available":true,"origin_prefix_witness_path_target_atom_id":1615075207963900,"origin_prefix_witness_path_points":2,"origin_prefix_witness_seed_norm_sq":9,"origin_prefix_witness_path_target_norm_sq":1031325872257,"per_port_coordinate_expansion":"available_summary_non_claim","per_port_coordinate_expansion_required_edges":2,"per_port_coordinate_expansion_available_edges":2,"per_port_coordinate_expansion_path_points_total":4,"claim_grade_path_accepted":false},"target_bridge_obligation":{"endpoint_atom_id":1615075207964004,"observed_target_seen":true,"observed_target_source_reached":true,"observed_target_port_atoms":9,"observed_target_bridge_edges":9,"endpoint_bridge_accepted":true},"terminal_source_inventory_summary":{"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]},"terminal_source_inventory_accumulator":{"mode":"summary_digest_only_non_claim","provenance":"terminal_component_inventory_accumulator","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]},"terminal_inventory_obligation":{"required_mode":"claim_grade_terminal_inventory","observed_mode":"summary_digest_only_non_claim","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"observed_count":14542615005,"observed_digest_algorithm":"sha256:lb_source_inventory_v1","observed_digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","observed_max_norm_sq":1031522101121},"missing_for_source_dead_cert":["coordinate Gaussian-prime source_path from origin prefix to canonical endpoint","claim-grade verifier binding the coordinate path to terminal inventory and BZ schedule"]}
JSON
  write_manifest "$dir"
}

good="$tmp/good"
write_bundle "$good"
if "$checker" "$good" --source-dead-checker "$fake_source_dead_checker" \
    > "$tmp/missing-gap-checker.log" 2>&1; then
  echo "checker accepted bundle without source-dead gap checker" >&2
  exit 1
fi
grep -q 'missing required --source-dead-gap-checker' \
  "$tmp/missing-gap-checker.log"
"$checker" "$good" \
  --source-dead-checker "$fake_source_dead_checker" \
  --source-dead-gap-checker "$fake_source_dead_gap_checker" \
  > "$tmp/good.log"
grep -q 'K26_FULL_RUN_BUNDLE_DRAFT_PASS' "$tmp/good.log"

bad_chunk_ledger="$tmp/bad-chunk-ledger"
write_bundle "$bad_chunk_ledger"
perl -0pi -e 's/"schedule_segment_start":2/"schedule_segment_start":3/' \
  "$bad_chunk_ledger/k26-continuation-chunks.jsonl"
write_manifest "$bad_chunk_ledger"
if "$checker" "$bad_chunk_ledger" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-chunk-ledger.log" 2>&1; then
  echo "checker accepted malformed chunk ledger in its own fixture" >&2
  exit 1
fi
grep -Eq 'K26 chunk ledger|K26 gap chunk ledger hash binding' \
  "$tmp/bad-chunk-ledger.log"

summary_nonclaim="$tmp/summary-nonclaim"
write_bundle "$summary_nonclaim"
summary_digest="$(shasum -a 256 "$summary_nonclaim/k26-continuation-result.json" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
cat > "$summary_nonclaim/k26-source-dead-cert.json" <<JSON
{"schema":"lb_source_dead_cert_draft_v1","certificate_id":"k26-source-dead-cert-summary-nonclaim","profile_id":"k26-source-run-profile","proof_status":"SUMMARY_ONLY_NON_CLAIM","non_claim":"summary-only diagnostic inventory, not a SOURCE_DEAD_CERT","terminal_source_inventory_mode":"summary_only_non_claim","metadata":{"source_mode":"ORIGIN_SOURCE","source_id":"omega","geometry_id":"SOURCE_ORIGIN_K26","commit_id":"abc123","build_id":"remote-test","bz_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","bz_schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","bz_schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","artifact_hash":"sha256:$summary_digest"},"k_sq":26,"terminal_radius":1015645,"negative_guard_pass":true,"endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"endpoint_atom_id":1615075207964004,"source_path_provenance":"coordinate_gaussian_prime_path","source_path":[{"a":0,"b":3,"norm_sq":9},{"a":376039,"b":943356,"norm_sq":1031325872257},{"a":376039,"b":943460,"norm_sq":1031522101121}],"terminal_source_inventory_summary":{"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]},"terminal_source_inventory_accumulator":{"mode":"summary_digest_only_non_claim","provenance":"terminal_component_inventory_accumulator","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]}}
JSON
write_manifest "$summary_nonclaim"
if "$checker" "$summary_nonclaim" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/summary-nonclaim.log" 2>&1; then
  echo "checker accepted summary-only non-claim cert as completed K26 bundle" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM' \
  "$tmp/summary-nonclaim.log"

bad_cert_source_path_binding="$tmp/bad-cert-source-path-binding"
write_bundle "$bad_cert_source_path_binding"
perl -0pi -e 's/"source_path":\[\{"a":0,"b":3,"norm_sq":9\},\{"a":376039,"b":943356,"norm_sq":1031325872257\},\{"a":376039,"b":943460,"norm_sq":1031522101121\}\]/"source_path":[{"a":0,"b":3,"norm_sq":9},{"a":376039,"b":943460,"norm_sq":1031522101121}]/' \
  "$bad_cert_source_path_binding/k26-source-dead-cert.json"
write_manifest "$bad_cert_source_path_binding"
if "$checker" "$bad_cert_source_path_binding" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-source-path-binding.log" 2>&1; then
  echo "checker accepted source-dead cert path not assembled from run artifacts" >&2
  exit 1
fi
grep -q 'K26 cert source path binding' \
  "$tmp/bad-cert-source-path-binding.log"

bad_summary_path="$tmp/bad-summary-path"
write_bundle "$bad_summary_path"
bad_summary_digest="$(shasum -a 256 "$bad_summary_path/k26-continuation-result.json" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
cat > "$bad_summary_path/k26-source-dead-cert.json" <<JSON
{"schema":"lb_source_dead_cert_draft_v1","certificate_id":"k26-source-dead-cert-summary-bad-path","profile_id":"k26-source-run-profile","proof_status":"SUMMARY_ONLY_NON_CLAIM","non_claim":"summary-only diagnostic inventory, not a SOURCE_DEAD_CERT","terminal_source_inventory_mode":"summary_only_non_claim","metadata":{"source_mode":"ORIGIN_SOURCE","source_id":"omega","geometry_id":"SOURCE_ORIGIN_K26","commit_id":"abc123","build_id":"remote-test","bz_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE","bz_schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","bz_schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","artifact_hash":"sha256:$bad_summary_digest"},"k_sq":26,"terminal_radius":1015645,"negative_guard_pass":true,"endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"endpoint_atom_id":1615075207964004,"source_path_provenance":"coordinate_gaussian_prime_path","source_path":[{"a":0,"b":3,"norm_sq":9},{"a":376039,"b":943461,"norm_sq":1031523988042},{"a":376039,"b":943460,"norm_sq":1031522101121}],"terminal_source_inventory_summary":{"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]},"terminal_source_inventory_accumulator":{"mode":"summary_digest_only_non_claim","provenance":"terminal_component_inventory_accumulator","listed_inventory_present":false,"claim_grade_inventory_accepted":false,"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615075207964004]}}
JSON
write_manifest "$bad_summary_path"
if "$checker" "$bad_summary_path" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-summary-path.log" 2>&1; then
  echo "checker accepted summary-only cert whose source path exceeds inventory summary" >&2
  exit 1
fi
grep -Eq 'K26 cert source path binding|source-dead checker did not accept draft cert' \
  "$tmp/bad-summary-path.log"

bad_gap="$tmp/bad-gap"
write_bundle "$bad_gap"
perl -0pi -e 's/SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING/WRONG_BLOCKER/' \
  "$bad_gap/k26-source-dead-gap.json"
if "$checker" "$bad_gap" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-gap.log" 2>&1; then
  echo "checker accepted stale artifact hash after gap mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26-source-dead-gap.json' \
  "$tmp/bad-gap.log"
write_manifest "$bad_gap"
if "$checker" "$bad_gap" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-gap-rehashed.log" 2>&1; then
  echo "checker accepted malformed source-dead gap artifact" >&2
  exit 1
fi
grep -q 'K26 source-dead gap blocker' "$tmp/bad-gap-rehashed.log"

bad_gap_binding="$tmp/bad-gap-binding"
write_bundle "$bad_gap_binding"
perl -0pi -e 's/"target_atom_path_length":3/"target_atom_path_length":4/' \
  "$bad_gap_binding/k26-source-dead-gap.json"
write_manifest "$bad_gap_binding"
if "$checker" "$bad_gap_binding" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-gap-binding.log" 2>&1; then
  echo "checker accepted gap atom path length that disagrees with continuation" >&2
  exit 1
fi
grep -q 'K26 gap atom path length binding' "$tmp/bad-gap-binding.log"

bad_gap_prefix_witness="$tmp/bad-gap-prefix-witness"
write_bundle "$bad_gap_prefix_witness"
perl -0pi -e 's/"prefix_witness_artifact":\{"name":"k26-prefix-witness\.txt","sha256":"[0-9a-f]{64}"/"prefix_witness_artifact":{"name":"k26-prefix-witness.txt","sha256":"0000000000000000000000000000000000000000000000000000000000000000"/' \
  "$bad_gap_prefix_witness/k26-source-dead-gap.json"
write_manifest "$bad_gap_prefix_witness"
if "$checker" "$bad_gap_prefix_witness" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-gap-prefix-witness.log" 2>&1; then
  echo "checker accepted source-dead gap with mismatched prefix witness hash" >&2
  exit 1
fi
grep -q 'K26 gap prefix witness hash binding' \
  "$tmp/bad-gap-prefix-witness.log"

bad_prefix_witness_row="$tmp/bad-prefix-witness-row"
write_bundle "$bad_prefix_witness_row"
perl -0pi -e 's/witness 1615075207963900 /witness 1 /' \
  "$bad_prefix_witness_row/k26-prefix-witness.txt"
bad_prefix_witness_digest="$(
  shasum -a 256 "$bad_prefix_witness_row/k26-prefix-witness.txt" |
    sed -nE 's/^([0-9a-f]{64}) .*/\1/p'
)"
perl -0pi -e \
  "s/\"prefix_witness_artifact\":\\{\"name\":\"k26-prefix-witness\\.txt\",\"sha256\":\"[0-9a-f]{64}\"/\"prefix_witness_artifact\":{\"name\":\"k26-prefix-witness.txt\",\"sha256\":\"${bad_prefix_witness_digest}\"/" \
  "$bad_prefix_witness_row/k26-source-dead-gap.json"
write_manifest "$bad_prefix_witness_row"
if "$checker" "$bad_prefix_witness_row" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-prefix-witness-row.log" 2>&1; then
  echo "checker accepted prefix witness that omits mixed-path source atom" >&2
  exit 1
fi
grep -q 'K26 prefix witness target row binding' \
  "$tmp/bad-prefix-witness-row.log"

bad_prefix_path="$tmp/bad-prefix-path"
write_bundle "$bad_prefix_path"
perl -0pi -e 's/"origin_prefix_witness_seed_norm_sq":9/"origin_prefix_witness_seed_norm_sq":27/' \
  "$bad_prefix_path/k26-source-dead-gap.json"
write_manifest "$bad_prefix_path"
if "$checker" "$bad_prefix_path" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-prefix-path.log" 2>&1; then
  echo "checker accepted source-dead gap with bad prefix witness path seed" >&2
  exit 1
fi
grep -q 'K26 gap origin-prefix witness path seed binding' \
  "$tmp/bad-prefix-path.log"

bad_gap_bz="$tmp/bad-gap-bz"
write_bundle "$bad_gap_bz"
perl -0pi -e 's/7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95/0000000000000000000000000000000000000000000000000000000000000000/' \
  "$bad_gap_bz/k26-source-dead-gap.json"
write_manifest "$bad_gap_bz"
if "$checker" "$bad_gap_bz" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-gap-bz.log" 2>&1; then
  echo "checker accepted source-dead gap with mismatched BZ digest" >&2
  exit 1
fi
grep -q 'K26 gap BZ digest binding' "$tmp/bad-gap-bz.log"

bad_gap_bz_obligation="$tmp/bad-gap-bz-obligation"
write_bundle "$bad_gap_bz_obligation"
perl -0pi -e 's/"observed_schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95"/"observed_schedule_digest_hex":"0000000000000000000000000000000000000000000000000000000000000000"/' \
  "$bad_gap_bz_obligation/k26-source-dead-gap.json"
write_manifest "$bad_gap_bz_obligation"
if "$checker" "$bad_gap_bz_obligation" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-gap-bz-obligation.log" 2>&1; then
  echo "checker accepted source-dead gap with mismatched BZ obligation digest" >&2
  exit 1
fi
grep -q 'K26 gap BZ obligation digest binding' \
  "$tmp/bad-gap-bz-obligation.log"

bad_gap_target_bridge="$tmp/bad-gap-target-bridge"
write_bundle "$bad_gap_target_bridge"
perl -0pi -e 's/"observed_target_bridge_edges":9/"observed_target_bridge_edges":8/' \
  "$bad_gap_target_bridge/k26-source-dead-gap.json"
write_manifest "$bad_gap_target_bridge"
if "$checker" "$bad_gap_target_bridge" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-gap-target-bridge.log" 2>&1; then
  echo "checker accepted source-dead gap with mismatched target bridge edge count" >&2
  exit 1
fi
grep -q 'K26 gap target bridge edge count binding' \
  "$tmp/bad-gap-target-bridge.log"

bad_gap_missing_list="$tmp/bad-gap-missing-list"
write_bundle "$bad_gap_missing_list"
perl -0pi -e 's/"claim-grade verifier binding the coordinate path to terminal inventory and BZ schedule"/"claim-grade verifier binding the coordinate path"/' \
  "$bad_gap_missing_list/k26-source-dead-gap.json"
write_manifest "$bad_gap_missing_list"
if "$checker" "$bad_gap_missing_list" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-gap-missing-list.log" 2>&1; then
  echo "checker accepted source-dead gap with incomplete missing-obligation list" >&2
  exit 1
fi
grep -q 'K26 source-dead gap missing terminal inventory' \
  "$tmp/bad-gap-missing-list.log"

bad_command_target="$tmp/bad-command-target"
write_bundle "$bad_command_target"
perl -0pi -e 's/"canonical_octant_endpoint":\{"a":376039,"b":943460/"canonical_octant_endpoint":{"a":376038,"b":943460/' \
  "$bad_command_target/k26_source_run_commands.json"
if "$checker" "$bad_command_target" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-command-target.log" 2>&1; then
  echo "checker accepted stale artifact hash after command target mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26_source_run_commands.json' \
  "$tmp/bad-command-target.log"
write_manifest "$bad_command_target"
if "$checker" "$bad_command_target" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-command-target-rehashed.log" 2>&1; then
  echo "checker accepted command contract with wrong canonical endpoint" >&2
  exit 1
fi
grep -q 'K26 command canonical endpoint' \
  "$tmp/bad-command-target-rehashed.log"

bad_command_prefix="$tmp/bad-command-prefix"
write_bundle "$bad_command_prefix"
perl -0pi -e 's/--endpoint-a 376039 --endpoint-b 943460/--endpoint-a 376038 --endpoint-b 943460/' \
  "$bad_command_prefix/k26_source_run_commands.json"
if "$checker" "$bad_command_prefix" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-command-prefix.log" 2>&1; then
  echo "checker accepted stale artifact hash after prefix endpoint mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26_source_run_commands.json' \
  "$tmp/bad-command-prefix.log"
write_manifest "$bad_command_prefix"
if "$checker" "$bad_command_prefix" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-command-prefix-rehashed.log" 2>&1; then
  echo "checker accepted command contract with wrong prefix endpoint flags" >&2
  exit 1
fi
grep -q 'K26 command prefix endpoint flags' \
  "$tmp/bad-command-prefix-rehashed.log"

bad_command_schedule="$tmp/bad-command-schedule"
write_bundle "$bad_command_schedule"
perl -0pi -e 's/"schedule_boundary_count":124/"schedule_boundary_count":123/' \
  "$bad_command_schedule/k26_source_run_commands.json"
if "$checker" "$bad_command_schedule" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-command-schedule.log" 2>&1; then
  echo "checker accepted stale artifact hash after command schedule mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26_source_run_commands.json' \
  "$tmp/bad-command-schedule.log"
write_manifest "$bad_command_schedule"
if "$checker" "$bad_command_schedule" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-command-schedule-rehashed.log" 2>&1; then
  echo "checker accepted command contract with wrong schedule boundary count" >&2
  exit 1
fi
grep -q 'K26 command schedule boundary count' \
  "$tmp/bad-command-schedule-rehashed.log"

bad_digest="$tmp/bad-digest"
write_bundle "$bad_digest"
perl -0pi -e 's/7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95/0000000000000000000000000000000000000000000000000000000000000000/' \
  "$bad_digest/k26_source_run_profile.json"
if "$checker" "$bad_digest" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-digest.log" 2>&1; then
  echo "checker accepted stale artifact hash after profile mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26_source_run_profile.json' \
  "$tmp/bad-digest.log"
write_manifest "$bad_digest"
if "$checker" "$bad_digest" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-digest-rehashed.log" 2>&1; then
  echo "checker accepted mismatched BZ digest" >&2
  exit 1
fi
grep -q 'K26 BZ digest profile binding' "$tmp/bad-digest-rehashed.log"

bad_profile_claim="$tmp/bad-profile-claim"
write_bundle "$bad_profile_claim"
perl -0pi -e 's/"claim_label":"SOURCE_ORIGIN_K26"/"claim_label":"SOURCE_ORIGIN_K34"/' \
  "$bad_profile_claim/k26_source_run_profile.json"
if "$checker" "$bad_profile_claim" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-profile-claim.log" 2>&1; then
  echo "checker accepted stale artifact hash after profile claim mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26_source_run_profile.json' \
  "$tmp/bad-profile-claim.log"
write_manifest "$bad_profile_claim"
if "$checker" "$bad_profile_claim" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-profile-claim-rehashed.log" 2>&1; then
  echo "checker accepted non-K26 source run profile claim label" >&2
  exit 1
fi
grep -q 'K26 profile claim label' \
  "$tmp/bad-profile-claim-rehashed.log"

bad_profile_id="$tmp/bad-profile-id"
write_bundle "$bad_profile_id"
perl -0pi -e 's/"profile_id":"k26-source-run-profile"/"profile_id":"k26-other-profile"/' \
  "$bad_profile_id/k26_source_run_profile.json"
if "$checker" "$bad_profile_id" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-profile-id.log" 2>&1; then
  echo "checker accepted stale artifact hash after profile id mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26_source_run_profile.json' \
  "$tmp/bad-profile-id.log"
write_manifest "$bad_profile_id"
if "$checker" "$bad_profile_id" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-profile-id-rehashed.log" 2>&1; then
  echo "checker accepted non-K26 source run profile id" >&2
  exit 1
fi
grep -q 'K26 profile id' \
  "$tmp/bad-profile-id-rehashed.log"

bad_profile_schedule="$tmp/bad-profile-schedule"
write_bundle "$bad_profile_schedule"
perl -0pi -e 's/"terminal_radius":1015645/"terminal_radius":1015644/' \
  "$bad_profile_schedule/k26_source_run_profile.json"
if "$checker" "$bad_profile_schedule" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-profile-schedule.log" 2>&1; then
  echo "checker accepted stale artifact hash after profile schedule mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26_source_run_profile.json' \
  "$tmp/bad-profile-schedule.log"
write_manifest "$bad_profile_schedule"
if "$checker" "$bad_profile_schedule" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-profile-schedule-rehashed.log" 2>&1; then
  echo "checker accepted profile with wrong terminal radius" >&2
  exit 1
fi
grep -q 'K26 profile terminal radius' \
  "$tmp/bad-profile-schedule-rehashed.log"

bad_source_candidate_bridge="$tmp/bad-source-candidate-bridge"
write_bundle "$bad_source_candidate_bridge"
perl -0pi -e 's/"source_unbridged_unsafe_candidate_atoms":0/"source_unbridged_unsafe_candidate_atoms":1/' \
  "$bad_source_candidate_bridge/k26-continuation-result.json"
cp "$bad_source_candidate_bridge/k26-continuation-result.json" \
  "$bad_source_candidate_bridge/k26-continuation-chunk-001.json"
if "$checker" "$bad_source_candidate_bridge" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-source-candidate-bridge-stale.log" 2>&1; then
  echo "checker accepted stale artifact hash after source unsafe bridge mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26-continuation-result.json' \
  "$tmp/bad-source-candidate-bridge-stale.log"
write_manifest "$bad_source_candidate_bridge"
if "$checker" "$bad_source_candidate_bridge" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-source-candidate-bridge.log" 2>&1; then
  echo "checker accepted source unsafe candidate gap" >&2
  exit 1
fi
grep -q 'K26 continuation source unsafe bridge' \
  "$tmp/bad-source-candidate-bridge.log"

bad_cert_summary="$tmp/bad-cert-summary"
write_bundle "$bad_cert_summary"
perl -0pi -e 's/"digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"/"digest_hex":"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"/' \
  "$bad_cert_summary/k26-source-dead-cert.json"
if "$checker" "$bad_cert_summary" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-summary.log" 2>&1; then
  echo "checker accepted stale artifact hash after cert summary mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26-source-dead-cert.json' \
  "$tmp/bad-cert-summary.log"
write_manifest "$bad_cert_summary"
if "$checker" "$bad_cert_summary" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-summary-rehashed.log" 2>&1; then
  echo "checker accepted cert summary that disagrees with continuation" >&2
  exit 1
fi
grep -q 'K26 cert inventory digest binding' \
  "$tmp/bad-cert-summary-rehashed.log"

bad_cert_metadata="$tmp/bad-cert-metadata"
write_bundle "$bad_cert_metadata"
perl -0pi -e 's/"source_mode":"ORIGIN_SOURCE"/"source_mode":"CERTIFIED_SEED"/' \
  "$bad_cert_metadata/k26-source-dead-cert.json"
if "$checker" "$bad_cert_metadata" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-metadata.log" 2>&1; then
  echo "checker accepted stale artifact hash after cert metadata mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26-source-dead-cert.json' \
  "$tmp/bad-cert-metadata.log"
write_manifest "$bad_cert_metadata"
if "$checker" "$bad_cert_metadata" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-metadata-rehashed.log" 2>&1; then
  echo "checker accepted non-origin source mode in K26 source-dead cert" >&2
  exit 1
fi
grep -q 'K26 source-dead cert source mode' \
  "$tmp/bad-cert-metadata-rehashed.log"

bad_cert_profile="$tmp/bad-cert-profile"
write_bundle "$bad_cert_profile"
perl -0pi -e 's/"profile_id":"k26-source-run-profile"/"profile_id":"k26-other-profile"/' \
  "$bad_cert_profile/k26-source-dead-cert.json"
if "$checker" "$bad_cert_profile" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-profile.log" 2>&1; then
  echo "checker accepted stale artifact hash after cert profile mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26-source-dead-cert.json' \
  "$tmp/bad-cert-profile.log"
write_manifest "$bad_cert_profile"
if "$checker" "$bad_cert_profile" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-profile-rehashed.log" 2>&1; then
  echo "checker accepted source-dead cert bound to wrong profile id" >&2
  exit 1
fi
grep -q 'K26 source-dead cert profile binding' \
  "$tmp/bad-cert-profile-rehashed.log"

bad_cert_artifact_hash="$tmp/bad-cert-artifact-hash"
write_bundle "$bad_cert_artifact_hash"
perl -0pi -e 's/"artifact_hash":"sha256:[0-9a-f]{64}"/"artifact_hash":"sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"/' \
  "$bad_cert_artifact_hash/k26-source-dead-cert.json"
write_manifest "$bad_cert_artifact_hash"
if "$checker" "$bad_cert_artifact_hash" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-artifact-hash.log" 2>&1; then
  echo "checker accepted cert artifact hash not bound to continuation" >&2
  exit 1
fi
grep -q 'K26 source-dead cert continuation artifact hash binding' \
  "$tmp/bad-cert-artifact-hash.log"

bad_cert_path_provenance="$tmp/bad-cert-path-provenance"
write_bundle "$bad_cert_path_provenance"
perl -0pi -e 's/"source_path_provenance":"coordinate_gaussian_prime_path"/"source_path_provenance":"mixed_coordinate_port_atom_chain_non_claim"/' \
  "$bad_cert_path_provenance/k26-source-dead-cert.json"
if "$checker" "$bad_cert_path_provenance" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-path-provenance.log" 2>&1; then
  echo "checker accepted stale artifact hash after cert path provenance mutation" >&2
  exit 1
fi
grep -q 'artifact hash mismatch for k26-source-dead-cert.json' \
  "$tmp/bad-cert-path-provenance.log"
write_manifest "$bad_cert_path_provenance"
if "$checker" "$bad_cert_path_provenance" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-path-provenance-rehashed.log" 2>&1; then
  echo "checker accepted mixed coordinate/port path provenance as K26 source-dead cert" >&2
  exit 1
fi
grep -q 'K26 source-dead cert coordinate path provenance' \
  "$tmp/bad-cert-path-provenance-rehashed.log"

bad_cert_origin_path="$tmp/bad-cert-origin-path"
write_bundle "$bad_cert_origin_path"
perl -0pi -e 's/"source_path":\[\{"a":0,"b":3,"norm_sq":9\},\{"a":376039,"b":943356,"norm_sq":1031325872257\},\{"a":376039,"b":943460,"norm_sq":1031522101121\}\]/"source_path":[{"a":376039,"b":943460,"norm_sq":1031522101121}]/' \
  "$bad_cert_origin_path/k26-source-dead-cert.json"
write_manifest "$bad_cert_origin_path"
if "$checker" "$bad_cert_origin_path" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/bad-cert-origin-path.log" 2>&1; then
  echo "checker accepted K26 source-dead cert whose path starts at endpoint" >&2
  exit 1
fi
grep -Eq 'K26 cert source path binding|source-dead checker did not accept draft cert' \
  "$tmp/bad-cert-origin-path.log"

missing="$tmp/missing"
write_bundle "$missing"
rm "$missing/k26-source-dead-cert.json"
if "$checker" "$missing" \
    --source-dead-checker "$fake_source_dead_checker" \
    --source-dead-gap-checker "$fake_source_dead_gap_checker" \
    > "$tmp/missing.log" 2>&1; then
  echo "checker accepted missing source-dead cert" >&2
  exit 1
fi
grep -q 'missing required artifact' "$tmp/missing.log"

echo "k26 full-run bundle checker self-test PASS"
