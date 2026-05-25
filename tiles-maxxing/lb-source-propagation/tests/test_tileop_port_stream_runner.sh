#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_tileop_port_stream_runner.sh SOURCE_TILEOP_PORT_STREAM_RUNNER" >&2
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
full_live="$tmp/full.live-handoff.txt"
full_checkpoint="$tmp/full.checkpoint.txt"
progress="$tmp/progress.jsonl"
chunk_json="$tmp/chunk.json"
chunk_checkpoint="$tmp/chunk.checkpoint.txt"
resumed_json="$tmp/resumed.json"
resumed_live="$tmp/resumed.live-handoff.txt"
resumed_checkpoint="$tmp/resumed.checkpoint.txt"
seed_checkpoint_err="$tmp/seed-checkpoint.err"
death_out_err="$tmp/death-out.err"

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --seed-inner-flags \
  --live-manifest-out "$full_live" \
  --checkpoint-out "$full_checkpoint" \
  --progress-out "$progress" \
  > "$full_json"

grep -q '"schema":"lb_source_tileop_port_stream_runner_v1"' "$full_json"
grep -q '"proof_status":"DIAGNOSTIC_NON_CLAIM"' "$full_json"
grep -q '"source_mode":"GEO_I_PORT_DIAGNOSTIC"' "$full_json"
grep -q '"accepted":true' "$full_json"
grep -q '"terminal_source_dead":false' "$full_json"
grep -q '"has_source_carry":true' "$full_json"
grep -q '"live_manifest_written":true' "$full_json"
grep -q '"checkpoint_written":true' "$full_json"
grep -q '"death_summary_status":"NOT_TERMINAL"' "$full_json"
grep -q '"schema":"lb_source_tileop_port_stream_progress_v1"' "$progress"
grep -q '"max_resident_microband_tiles":' "$progress"
grep -q '"max_checkpoint_bytes":' "$progress"
grep -q '^LB_SOURCE_STREAM_CHECKPOINT_V1$' "$full_checkpoint"

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --seed-inner-flags \
  --stop-after-microbands 1 \
  --checkpoint-out "$chunk_checkpoint" \
  > "$chunk_json"

grep -q '"r_final":376' "$chunk_json"
grep -q '"requested_r_final":512' "$chunk_json"
grep -q '"schedule_index":1' "$chunk_json"
grep -q '"microbands_processed":1' "$chunk_json"
grep -q '"checkpoint_written":true' "$chunk_json"

"$runner" \
  --r-start 376 \
  --r-final 512 \
  --microband-width 128 \
  --checkpoint-in "$chunk_checkpoint" \
  --live-manifest-out "$resumed_live" \
  --checkpoint-out "$resumed_checkpoint" \
  > "$resumed_json"

grep -q '"source_mode":"GEO_I_PORT_DIAGNOSTIC"' "$resumed_json"
grep -q '"original_r_start":248' "$resumed_json"
grep -q '"r_final":512' "$resumed_json"
grep -q '"schedule_index":3' "$resumed_json"
grep -q '"accepted":true' "$resumed_json"
grep -q '"has_source_carry":true' "$resumed_json"
cmp "$full_live" "$resumed_live"
cmp "$full_checkpoint" "$resumed_checkpoint"

if "$runner" \
  --r-start 376 \
  --r-final 512 \
  --microband-width 128 \
  --seed-inner-flags \
  --checkpoint-in "$chunk_checkpoint" \
  >"$tmp/seed-checkpoint.out" \
  2>"$seed_checkpoint_err"; then
  echo "runner accepted --seed-inner-flags with --checkpoint-in" >&2
  exit 1
fi
grep -q -- '--seed-inner-flags cannot be combined with --checkpoint-in' \
  "$seed_checkpoint_err"

if "$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --seed-inner-flags \
  --death-out "$tmp/death.json" \
  >"$tmp/death-out.out" \
  2>"$death_out_err"; then
  echo "runner accepted --death-out" >&2
  exit 1
fi
grep -q -- '--death-out is unsupported' "$death_out_err"
[[ ! -e "$tmp/death.json" ]]

if grep -Eq 'SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS' \
  "$full_json" "$chunk_json" "$resumed_json" "$progress"; then
  echo "stream runner emitted a claim PASS token" >&2
  exit 1
fi

echo "tileop port stream runner self-test PASS"
