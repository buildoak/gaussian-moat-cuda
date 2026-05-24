#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_tileop_port_runner_resume.sh SOURCE_TILEOP_PORT_RUNNER" >&2
  exit 2
fi

runner="$1"
if [[ ! -x "$runner" ]]; then
  echo "runner is not executable: $runner" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

full_json="$tmp/full.json"
full_manifest="$tmp/full-manifest.txt"
full_live_manifest="$tmp/full-live-manifest.txt"
chunk_json="$tmp/chunk.json"
chunk_manifest="$tmp/chunk-manifest.txt"
chunk_live_manifest="$tmp/chunk-live-manifest.txt"
resumed_json="$tmp/resumed.json"
resumed_manifest="$tmp/resumed-manifest.txt"
resumed_live_json="$tmp/resumed-live.json"
resumed_live_manifest="$tmp/resumed-live-manifest.txt"
live_reject_err="$tmp/live-reject.err"
death_live_manifest="$tmp/death-live-manifest.txt"
death_json="$tmp/death.json"
death_artifact="$tmp/death-artifact.json"
death_summary="$tmp/death-summary.json"
death_false_err="$tmp/death-false.err"

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 248,375,512 \
  --seed-inner-flags \
  --manifest-out "$full_manifest" \
  --live-manifest-out "$full_live_manifest" \
  > "$full_json"

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 248,375,512 \
  --seed-inner-flags \
  --stop-after-bands 1 \
  --manifest-out "$chunk_manifest" \
  --live-manifest-out "$chunk_live_manifest" \
  > "$chunk_json"

grep -q '"r_final":375' "$chunk_json"
grep -q '"requested_r_final":512' "$chunk_json"
grep -q '"bands_processed":1' "$chunk_json"
grep -q '"manifest_written":true' "$chunk_json"
grep -q '"live_manifest_written":true' "$chunk_json"

"$runner" \
  --r-start 375 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 375,512 \
  --manifest-in "$chunk_manifest" \
  --manifest-out "$resumed_manifest" \
  > "$resumed_json"

grep -q '"source_mode":"PORT_CARRY_MANIFEST"' "$resumed_json"
grep -q '"r_final":512' "$resumed_json"
grep -q '"accepted":true' "$resumed_json"
grep -q '"terminal_source_dead":false' "$resumed_json"
grep -q '"has_source_carry":true' "$resumed_json"
cmp "$full_manifest" "$resumed_manifest"

compiled_k_sq="$(
  sed -n 's/.*"k_sq":\([0-9][0-9]*\).*/\1/p' "$full_json"
)"
compiled_carry_width="$(
  awk -v k="$compiled_k_sq" \
    'BEGIN { r = int(sqrt(k)); if (r * r < k) { r += 1 } print r }'
)"

"$runner" \
  --r-start 375 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 375,512 \
  --live-manifest-in "$chunk_live_manifest" \
  --live-manifest-out "$resumed_live_manifest" \
  > "$resumed_live_json"

grep -q '"source_mode":"GEO_I_PORT_DIAGNOSTIC"' "$resumed_live_json"
grep -q '"r_final":512' "$resumed_live_json"
grep -q '"accepted":true' "$resumed_live_json"
grep -q '"terminal_source_dead":false' "$resumed_live_json"
grep -q '"has_source_carry":true' "$resumed_live_json"
grep -q '"live_manifest_written":true' "$resumed_live_json"
cmp "$full_live_manifest" "$resumed_live_manifest"

if "$runner" \
  --r-start 375 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 375,512 \
  --seed-inner-flags \
  --live-manifest-in "$chunk_live_manifest" \
  >/tmp/tileop-live-reject.out \
  2>"$live_reject_err"; then
  echo "runner accepted --seed-inner-flags with --live-manifest-in" >&2
  exit 1
fi
grep -q -- '--seed-inner-flags cannot be combined with --live-manifest-in' \
  "$live_reject_err"

if "$runner" \
  --r-start 248 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 248,375,512 \
  --seed-inner-flags \
  --death-out "$tmp/not-dead.json" \
  >/tmp/tileop-death-false.out \
  2>"$death_false_err"; then
  echo "runner accepted --death-out for a live source" >&2
  exit 1
fi
grep -q -- '--death-out requires terminal_source_dead=true' \
  "$death_false_err"

cat > "$death_live_manifest" <<EOF_MANIFEST
LB_SOURCE_LIVE_HANDOFF_V1
k_sq $compiled_k_sq
cut_radius 248
carry_width $compiled_carry_width
source_mode SYNTHETIC_LIVE_TEST
source_id tileop_port_materialized_source_v1
geometry_id gaussian_octant_tileop_port_v1
build_id local_campaign_build
schedule_digest_algorithm sha256:tileop_port_diagnostic_schedule_v1
schedule_digest_hex 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
overflow_summary none
carry_atoms 1
carry_atom 249 62001
components 1
component 1 1 249
END
EOF_MANIFEST

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 248,512 \
  --live-manifest-in "$death_live_manifest" \
  --last-band-summary-out "$death_summary" \
  --death-out "$death_artifact" \
  > "$death_json"

grep -q '"terminal_source_dead":true' "$death_json"
grep -q '"death_written":true' "$death_json"
grep -q '"last_band_summary_written":true' "$death_json"
grep -q '"schema":"lb_source_tileop_port_death_diagnostic_v1"' \
  "$death_artifact"
grep -q '"proof_status":"DIAGNOSTIC_NON_CLAIM"' "$death_artifact"
grep -q '"previous_live_handoff_sha256":"' "$death_artifact"
grep -q '"active_band_summary_sha256":"' "$death_artifact"
grep -q '"terminal_source_dead":true' "$death_artifact"
grep -q '"schema":"lb_source_last_band_reachability_summary_v1"' \
  "$death_summary"
if grep -Eq 'SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS' \
  "$death_json" "$death_artifact" "$death_summary"; then
  echo "live/death diagnostics emitted a claim PASS token" >&2
  exit 1
fi

echo "tileop port runner resume self-test PASS"
