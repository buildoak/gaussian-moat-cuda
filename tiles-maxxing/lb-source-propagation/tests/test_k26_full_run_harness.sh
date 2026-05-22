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

cat > "$build_dir/k26_source_run_commands" <<'SH'
#!/usr/bin/env bash
cat <<'JSON'
{"schema":"lb_source_k26_run_commands_v1","claim_label":"SOURCE_ORIGIN_K26","executable_now":false,"build":{"required_k_sq":26},"prefix":{"command":"source_origin_cpu_runner --endpoint-a 376039 --endpoint-b 943460"},"continuation":{"schedule_boundary_count":124,"schedule_segment_count":123,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","seam_bridge_policy":"require_full_bridge","blocked_if_unbridged_coordinate_carry_atoms":true,"schedule_radii_csv":"8192,16384,1015645","command":"source_tileop_port_runner --require-full-bridge"}}
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
{"schema":"lb_source_k26_run_profile_v1","profile_status":"RUN_PROFILE_DRAFT_NON_CLAIM","executable_now":false,"required_k_sq":26,"bz_schedule":"repaired","accepted_for_schedule":true,"accepted_for_claim":false,"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1","schedule_digest_hex":"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95","band_count":124}
JSON
SH

cat > "$build_dir/source_origin_cpu_runner" <<'SH'
#!/usr/bin/env bash
manifest=""
witness=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest-out)
      manifest="$2"; shift 2 ;;
    --prefix-witness-out)
      witness="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$manifest" ]] && echo "manifest" > "$manifest"
[[ -n "$witness" ]] && echo "witness" > "$witness"
cat <<'JSON'
{"schema":"lb_source_origin_cpu_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","k_sq":26,"r_final":8192,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"manifest_written":true,"prefix_witness_written":true}
JSON
SH

cat > "$build_dir/source_tileop_port_runner" <<'SH'
#!/usr/bin/env bash
for arg in "$@"; do
  if [[ "$arg" == "--require-full-bridge" ]]; then
    require_full_bridge=true
  fi
done
if [[ "${require_full_bridge:-false}" != true ]]; then
  echo "missing --require-full-bridge" >&2
  exit 1
fi
cat <<'JSON'
{"schema":"lb_source_tileop_port_runner_v1","proof_status":"DIAGNOSTIC_NON_CLAIM","source_mode":"ORIGIN_PREFIX_PORT_WITNESS","seam_bridge_policy":"require_full_bridge","k_sq":26,"r_start":8192,"r_final":1015645,"schedule_mode":"explicit_radii","schedule_boundary_count":124,"tileop_overflows":0,"unbridged_coordinate_carry_atoms":0,"accepted":true,"terminal_source_dead":true,"has_source_carry":false,"source_inventory_count":14542615005,"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1","max_source_norm_sq":1031520000000}
JSON
SH

chmod +x "$build_dir"/*

blocked_out="$tmp/blocked"
if "$harness" --build-dir "$build_dir" --out-dir "$blocked_out" >/tmp/k26-harness-blocked.out 2>/tmp/k26-harness-blocked.err; then
  echo "harness accepted a run without k26-source-dead-cert.json" >&2
  exit 1
fi
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING' \
  "$blocked_out/status.txt"
test -f "$blocked_out/k26-prefix-result.json"
test -f "$blocked_out/k26-continuation-result.json"

cert="$tmp/k26-source-dead-cert.json"
cat > "$cert" <<'JSON'
{"schema":"lb_source_dead_cert_draft_v1","k_sq":26,"terminal_radius":1015645,"negative_guard_pass":true,"endpoint":{"a":376039,"b":943460,"norm_sq":1031522101121},"source_path":[{"a":376039,"b":943460,"norm_sq":1031522101121}],"terminal_source_inventory_summary":{"count":14542615005,"digest_algorithm":"sha256:lb_source_inventory_v1","digest_hex":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","max_norm_sq":1031522101121,"max_norm_atom_ids":[1615070786916]}}
JSON

fake_source_dead_checker="$tmp/fake-source-dead-checker"
cat > "$fake_source_dead_checker" <<'SH'
#!/usr/bin/env bash
if grep -q '"terminal_source_inventory_summary":{"count":14542615005' "$1"; then
  echo '{"status":"SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM_PASS"}'
else
  exit 1
fi
SH
chmod +x "$fake_source_dead_checker"

checked_out="$tmp/checked"
"$harness" \
  --build-dir "$build_dir" \
  --out-dir "$checked_out" \
  --cert-in "$cert" \
  --source-dead-checker "$fake_source_dead_checker" \
  >/tmp/k26-harness-checked.out
grep -q 'K26_FULL_RUN_BUNDLE_CHECKED' "$checked_out/status.txt"
grep -q 'K26_FULL_RUN_BUNDLE_SUMMARY_ONLY_NON_CLAIM_PASS' \
  "$checked_out/k26-full-run-bundle-check.log"

echo "k26 full-run harness self-test PASS"
