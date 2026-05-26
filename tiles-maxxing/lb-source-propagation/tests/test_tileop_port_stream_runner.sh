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
neutral_full_json="$tmp/neutral-full.json"
neutral_full_checkpoint="$tmp/neutral-full.checkpoint.txt"
neutral_progress="$tmp/neutral-progress.jsonl"
neutral_chunk_json="$tmp/neutral-chunk.json"
neutral_chunk_checkpoint="$tmp/neutral-chunk.checkpoint.txt"
neutral_resumed_json="$tmp/neutral-resumed.json"
neutral_resumed_checkpoint="$tmp/neutral-resumed.checkpoint.txt"
neutral_stale_err="$tmp/neutral-stale.err"
neutral_seed_err="$tmp/neutral-seed.err"
neutral_handoff="$(dirname "$neutral_full_checkpoint")/detector_handoff.current.bin"
missing_handoff_err="$tmp/missing-handoff.err"
bad_hash_err="$tmp/bad-hash.err"
bad_bytes_err="$tmp/bad-bytes.err"
wrong_cut_err="$tmp/wrong-cut.err"

assert_json_nonnegative_field() {
  local file="$1"
  local field="$2"
  if ! grep -Eq '"'"$field"'":[0-9]+' "$file"; then
    echo "missing nonnegative JSON field $field in $file" >&2
    exit 1
  fi
}

assert_json_string_field() {
  local file="$1"
  local field="$2"
  if ! grep -Eq '"'"$field"'":"[^"]*"' "$file"; then
    echo "missing string JSON field $field in $file" >&2
    exit 1
  fi
}

assert_resumable_telemetry_fields() {
  local file="$1"
  assert_json_string_field "$file" "phase0_schema"
  assert_json_string_field "$file" "runner_id"
  assert_json_nonnegative_field "$file" "detector_handoff_encode_ms"
  assert_json_nonnegative_field "$file" "detector_handoff_hash_ms"
  assert_json_nonnegative_field "$file" "detector_handoff_write_ms"
  assert_json_nonnegative_field "$file" "detector_handoff_readback_ms"
  assert_json_nonnegative_field "$file" "detector_handoff_validate_ms"
  assert_json_nonnegative_field "$file" "detector_handoff_rename_ms"
  assert_json_nonnegative_field "$file" "detector_handoff_total_ms"
  assert_json_string_field "$file" "checkpoint_handoff_source"
  assert_json_nonnegative_field "$file" "rss_bytes"
  assert_json_nonnegative_field "$file" "peak_rss_bytes"
  assert_json_nonnegative_field "$file" "max_components"
  assert_json_nonnegative_field "$file" "handoff_wall_share_bp"
  assert_json_nonnegative_field "$file" "checkpoint_wall_share_bp"
  assert_json_nonnegative_field "$file" "materialization_wall_share_bp"
}

assert_progress_telemetry_fields() {
  local file="$1"
  assert_json_nonnegative_field "$file" "rss_after_enumerate_bytes"
  assert_json_nonnegative_field "$file" "peak_rss_after_enumerate_bytes"
  assert_json_nonnegative_field "$file" "rss_after_tileop_bytes"
  assert_json_nonnegative_field "$file" "peak_rss_after_tileop_bytes"
  assert_json_nonnegative_field "$file" "rss_after_stream_bytes"
  assert_json_nonnegative_field "$file" "peak_rss_after_stream_bytes"
  assert_json_nonnegative_field "$file" "rss_after_process_bytes"
  assert_json_nonnegative_field "$file" "peak_rss_after_process_bytes"
  assert_json_nonnegative_field "$file" "rss_after_handoff_write_bytes"
  assert_json_nonnegative_field "$file" "peak_rss_after_handoff_write_bytes"
  assert_json_nonnegative_field "$file" "max_components"
}

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
grep -q '"resumable_mode":"resumable-band"' "$full_json"
grep -q '"resumable_checkpoint_written":false' "$full_json"
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

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --resumable-checkpoint-out "$neutral_full_checkpoint" \
  --progress-out "$neutral_progress" \
  > "$neutral_full_json"

grep -q '"source_mode":"NONE"' "$neutral_full_json"
grep -q '"resumable_mode":"resumable-band"' "$neutral_full_json"
grep -q '"accepted":true' "$neutral_full_json"
grep -q '"has_source_carry":false' "$neutral_full_json"
grep -q '"checkpoint_written":false' "$neutral_full_json"
grep -q '"resumable_checkpoint_written":true' "$neutral_full_json"
grep -q '"detector_handoff_written":true' "$neutral_full_json"
grep -q '"checkpoint_handoff_source":"written"' "$neutral_full_json"
grep -q '"detector_handoff_sha256":"[0-9a-f]\{64\}"' "$neutral_full_json"
assert_resumable_telemetry_fields "$neutral_full_json"
assert_progress_telemetry_fields "$neutral_progress"
grep -q '^LB_RESUMABLE_BAND_CHECKPOINT_V1$' "$neutral_full_checkpoint"
grep -q '^mode resumable-band$' "$neutral_full_checkpoint"
grep -q '^proof_status DIAGNOSTIC_NON_CLAIM$' "$neutral_full_checkpoint"
grep -q '^detector_handoff_path '"$neutral_handoff"'$' \
  "$neutral_full_checkpoint"
grep -q '^detector_handoff_schema DETECTOR_BAND_HANDOFF_V1$' \
  "$neutral_full_checkpoint"
grep -q '^detector_handoff_bytes [1-9][0-9]*$' \
  "$neutral_full_checkpoint"
[[ -s "$neutral_handoff" ]]
[[ "$(find "$tmp" -maxdepth 1 -name 'detector_handoff*.bin' -type f | wc -l | tr -d ' ')" == "1" ]]
if grep -Eq 'source_mode|source_id|LB_SOURCE_LIVE_HANDOFF_V1' \
  "$neutral_full_checkpoint"; then
  echo "resumable checkpoint is not source-neutral" >&2
  exit 1
fi
neutral_full_handoff_hash="$(
  awk '$1 == "detector_handoff_sha256" {print $2}' "$neutral_full_checkpoint"
)"

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --stop-after-microbands 1 \
  --resumable-checkpoint-out "$neutral_chunk_checkpoint" \
  > "$neutral_chunk_json"

grep -q '"r_final":376' "$neutral_chunk_json"
grep -q '"schedule_index":1' "$neutral_chunk_json"
grep -q '"resumable_checkpoint_written":true' "$neutral_chunk_json"
grep -q '"checkpoint_handoff_source":"written"' "$neutral_chunk_json"
assert_resumable_telemetry_fields "$neutral_chunk_json"
grep -q '^detector_handoff_schema DETECTOR_BAND_HANDOFF_V1$' \
  "$neutral_chunk_checkpoint"
neutral_chunk_handoff="$tmp/neutral-chunk.handoff.bin"
cp "$neutral_handoff" "$neutral_chunk_handoff"
neutral_chunk_handoff_hash="$(
  awk '$1 == "detector_handoff_sha256" {print $2}' "$neutral_chunk_checkpoint"
)"

"$runner" \
  --r-start 376 \
  --r-final 512 \
  --microband-width 128 \
  --resumable-checkpoint-in "$neutral_chunk_checkpoint" \
  --resumable-checkpoint-out "$neutral_resumed_checkpoint" \
  > "$neutral_resumed_json"

grep -q '"original_r_start":248' "$neutral_resumed_json"
grep -q '"r_final":512' "$neutral_resumed_json"
grep -q '"schedule_index":3' "$neutral_resumed_json"
grep -q '"resumable_checkpoint_written":true' "$neutral_resumed_json"
grep -q '"detector_handoff_written":true' "$neutral_resumed_json"
grep -q '"checkpoint_handoff_source":"written"' "$neutral_resumed_json"
assert_resumable_telemetry_fields "$neutral_resumed_json"
cmp "$neutral_full_checkpoint" "$neutral_resumed_checkpoint"
neutral_resumed_handoff_hash="$(
  awk '$1 == "detector_handoff_sha256" {print $2}' "$neutral_resumed_checkpoint"
)"
[[ "$neutral_resumed_handoff_hash" == "$neutral_full_handoff_hash" ]]
[[ "$neutral_chunk_handoff_hash" != "$neutral_full_handoff_hash" ]]
[[ "$(find "$tmp" -maxdepth 1 -name 'detector_handoff*.bin' -type f | wc -l | tr -d ' ')" == "1" ]]

missing_dir="$tmp/missing-handoff"
mkdir "$missing_dir"
"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --stop-after-microbands 1 \
  --resumable-checkpoint-out "$missing_dir/checkpoint.txt" \
  > "$missing_dir/chunk.json"
rm "$missing_dir/detector_handoff.current.bin"
if "$runner" \
  --r-start 376 \
  --r-final 512 \
  --microband-width 128 \
  --resumable-checkpoint-in "$missing_dir/checkpoint.txt" \
  --resumable-checkpoint-out "$missing_dir/resumed.checkpoint.txt" \
  >"$missing_dir/resumed.out" \
  2>"$missing_handoff_err"; then
  echo "runner accepted missing detector handoff on resume" >&2
  exit 1
fi
grep -q -- 'cannot open --resumable-checkpoint-in detector handoff path' \
  "$missing_handoff_err"
[[ ! -e "$missing_dir/resumed.checkpoint.txt" ]]

bad_hash_dir="$tmp/bad-hash"
mkdir "$bad_hash_dir"
"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --stop-after-microbands 1 \
  --resumable-checkpoint-out "$bad_hash_dir/checkpoint.txt" \
  > "$bad_hash_dir/chunk.json"
awk '{
  if ($1 == "detector_handoff_sha256") {
    print "detector_handoff_sha256 0000000000000000000000000000000000000000000000000000000000000000"
  } else {
    print
  }
}' "$bad_hash_dir/checkpoint.txt" > "$bad_hash_dir/bad.checkpoint.txt"
if "$runner" \
  --r-start 376 \
  --r-final 512 \
  --microband-width 128 \
  --resumable-checkpoint-in "$bad_hash_dir/bad.checkpoint.txt" \
  --resumable-checkpoint-out "$bad_hash_dir/resumed.checkpoint.txt" \
  >"$bad_hash_dir/resumed.out" \
  2>"$bad_hash_err"; then
  echo "runner accepted bad detector handoff hash" >&2
  exit 1
fi
grep -q -- 'wrong detector_handoff_sha256' "$bad_hash_err"
[[ ! -e "$bad_hash_dir/resumed.checkpoint.txt" ]]

bad_bytes_dir="$tmp/bad-bytes"
mkdir "$bad_bytes_dir"
"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --stop-after-microbands 1 \
  --resumable-checkpoint-out "$bad_bytes_dir/checkpoint.txt" \
  > "$bad_bytes_dir/chunk.json"
awk '{
  if ($1 == "detector_handoff_bytes") {
    print "detector_handoff_bytes 1"
  } else {
    print
  }
}' "$bad_bytes_dir/checkpoint.txt" > "$bad_bytes_dir/bad.checkpoint.txt"
if "$runner" \
  --r-start 376 \
  --r-final 512 \
  --microband-width 128 \
  --resumable-checkpoint-in "$bad_bytes_dir/bad.checkpoint.txt" \
  --resumable-checkpoint-out "$bad_bytes_dir/resumed.checkpoint.txt" \
  >"$bad_bytes_dir/resumed.out" \
  2>"$bad_bytes_err"; then
  echo "runner accepted bad detector handoff byte count" >&2
  exit 1
fi
grep -q -- 'wrong detector_handoff_bytes' "$bad_bytes_err"
[[ ! -e "$bad_bytes_dir/resumed.checkpoint.txt" ]]

wrong_cut_dir="$tmp/wrong-cut"
mkdir "$wrong_cut_dir"
"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --resumable-checkpoint-out "$wrong_cut_dir/full.checkpoint.txt" \
  > "$wrong_cut_dir/full.json"
cp "$wrong_cut_dir/detector_handoff.current.bin" "$wrong_cut_dir/final.bin"
final_hash="$(
  awk '$1 == "detector_handoff_sha256" {print $2}' \
    "$wrong_cut_dir/full.checkpoint.txt"
)"
final_bytes="$(wc -c < "$wrong_cut_dir/final.bin" | tr -d ' ')"
"$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --stop-after-microbands 1 \
  --resumable-checkpoint-out "$wrong_cut_dir/chunk.checkpoint.txt" \
  > "$wrong_cut_dir/chunk.json"
awk -v path="$wrong_cut_dir/final.bin" \
    -v hash="$final_hash" \
    -v bytes="$final_bytes" '{
  if ($1 == "detector_handoff_path") {
    print "detector_handoff_path " path
  } else if ($1 == "detector_handoff_sha256") {
    print "detector_handoff_sha256 " hash
  } else if ($1 == "detector_handoff_bytes") {
    print "detector_handoff_bytes " bytes
  } else {
    print
  }
}' "$wrong_cut_dir/chunk.checkpoint.txt" > "$wrong_cut_dir/bad.checkpoint.txt"
if "$runner" \
  --r-start 376 \
  --r-final 512 \
  --microband-width 128 \
  --resumable-checkpoint-in "$wrong_cut_dir/bad.checkpoint.txt" \
  --resumable-checkpoint-out "$wrong_cut_dir/resumed.checkpoint.txt" \
  >"$wrong_cut_dir/resumed.out" \
  2>"$wrong_cut_err"; then
  echo "runner accepted wrong detector handoff cut radius" >&2
  exit 1
fi
grep -q -- 'wrong cut_radius' "$wrong_cut_err"
[[ ! -e "$wrong_cut_dir/resumed.checkpoint.txt" ]]

if "$runner" \
  --r-start 248 \
  --r-final 512 \
  --microband-width 128 \
  --resumable-checkpoint-in "$neutral_chunk_checkpoint" \
  >"$tmp/neutral-stale.out" \
  2>"$neutral_stale_err"; then
  echo "runner accepted stale --resumable-checkpoint-in" >&2
  exit 1
fi
grep -q -- 'invalid --resumable-checkpoint-in: stale next_r_start' \
  "$neutral_stale_err"

if "$runner" \
  --r-start 376 \
  --r-final 512 \
  --microband-width 128 \
  --seed-inner-flags \
  --resumable-checkpoint-in "$neutral_chunk_checkpoint" \
  >"$tmp/neutral-seed.out" \
  2>"$neutral_seed_err"; then
  echo "runner accepted --seed-inner-flags with --resumable-checkpoint-in" >&2
  exit 1
fi
grep -q -- '--seed-inner-flags cannot be combined with --resumable-checkpoint-in' \
  "$neutral_seed_err"

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
  "$full_json" "$chunk_json" "$resumed_json" "$progress" \
  "$neutral_full_json" "$neutral_chunk_json" "$neutral_resumed_json" \
  "$neutral_full_checkpoint" "$neutral_chunk_checkpoint" \
  "$neutral_resumed_checkpoint"; then
  echo "stream runner emitted a claim PASS token" >&2
  exit 1
fi

echo "tileop port stream runner self-test PASS"
