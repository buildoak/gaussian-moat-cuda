#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_k26_runtime_budget_checker.sh CHECKER" >&2
  exit 2
fi

checker="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/pass-progress.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_phase_v1","phase":"tileop_build","event":"begin","band_index":0,"r_start":8192,"r_outer":16384}
{"schema":"lb_source_tileop_port_progress_v1","band_index":0,"r_start":8192,"r_outer":16384,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":1200,"total_ms":1000}
{"schema":"lb_source_tileop_port_progress_v1","band_index":1,"r_start":16384,"r_outer":24576,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":2337,"total_ms":2000}
JSONL

cat > "$tmp/chunks.jsonl" <<'JSONL'
{"schema":"lb_source_k26_continuation_chunk_v1","chunk_index":0,"chunk_id":"000","action":"executed"}
JSONL

"$checker" \
  --progress "$tmp/pass-progress.jsonl" \
  --chunk-ledger "$tmp/chunks.jsonl" \
  --schedule-segment-count 123 \
  --max-runtime-seconds 14000 \
  > "$tmp/pass.out"
grep -q '"status":"K26_RUNTIME_BUDGET_PASS"' "$tmp/pass.out"
grep -q '"projected_total_seconds":185' "$tmp/pass.out"
grep -q '"chunk_ledger_rows":1' "$tmp/pass.out"
grep -q '"claim_grade":false' "$tmp/pass.out"

cat > "$tmp/chunked-reset-progress.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_progress_v1","band_index":0,"r_start":8192,"r_outer":16384,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":1200,"total_ms":1000}
{"schema":"lb_source_tileop_port_progress_v1","band_index":0,"r_start":16384,"r_outer":24576,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":2337,"total_ms":2000}
JSONL

cat > "$tmp/two-chunks.jsonl" <<'JSONL'
{"schema":"lb_source_k26_continuation_chunk_v1","chunk_index":0,"chunk_id":"000","action":"executed"}
{"schema":"lb_source_k26_continuation_chunk_v1","chunk_index":1,"chunk_id":"001","action":"executed"}
JSONL

"$checker" \
  --progress "$tmp/chunked-reset-progress.jsonl" \
  --chunk-ledger "$tmp/two-chunks.jsonl" \
  --schedule-segment-count 123 \
  --max-runtime-seconds 14000 \
  > "$tmp/chunked-reset.out"
grep -q '"status":"K26_RUNTIME_BUDGET_PASS"' "$tmp/chunked-reset.out"
grep -q '"completed_band_count":2' "$tmp/chunked-reset.out"
grep -q '"last_completed_band_index":0' "$tmp/chunked-reset.out"
grep -q '"last_completed_r_start":16384' "$tmp/chunked-reset.out"
grep -q '"last_completed_r_outer":24576' "$tmp/chunked-reset.out"
grep -q '"chunk_ledger_rows":2' "$tmp/chunked-reset.out"

cat > "$tmp/slow-progress.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_progress_v1","band_index":0,"r_start":8192,"r_outer":16384,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":1200,"total_ms":200000}
{"schema":"lb_source_tileop_port_progress_v1","band_index":1,"r_start":16384,"r_outer":24576,"accepted":true,"terminal_source_dead":false,"has_source_carry":true,"source_carry_atoms":2337,"total_ms":200000}
JSONL

if "$checker" \
    --progress "$tmp/slow-progress.jsonl" \
    --schedule-segment-count 123 \
    --max-runtime-seconds 14000 \
    > "$tmp/slow.out"; then
  echo "runtime checker accepted over-budget projection" >&2
  exit 1
fi
grep -q '"status":"K26_RUNTIME_BUDGET_REJECT"' "$tmp/slow.out"
grep -q '"budget_margin_seconds":-' "$tmp/slow.out"

cat > "$tmp/phase-only.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_phase_v1","phase":"tileop_build","event":"begin","band_index":0,"r_start":8192,"r_outer":16384}
JSONL

if "$checker" --progress "$tmp/phase-only.jsonl" > "$tmp/phase-only.out"; then
  echo "runtime checker accepted missing completed bands" >&2
  exit 1
fi
grep -q '"status":"K26_RUNTIME_BUDGET_INSUFFICIENT_PROGRESS"' \
  "$tmp/phase-only.out"
grep -q '"active_phase":' "$tmp/phase-only.out"

cat > "$tmp/duplicate-progress.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_progress_v1","band_index":0,"accepted":true,"total_ms":1000}
{"schema":"lb_source_tileop_port_progress_v1","band_index":0,"accepted":true,"total_ms":1000}
JSONL

if "$checker" --progress "$tmp/duplicate-progress.jsonl" \
    > "$tmp/duplicate.out" 2> "$tmp/duplicate.err"; then
  echo "runtime checker accepted duplicate band indices" >&2
  exit 1
fi
grep -q 'duplicate completed progress band_index=0' "$tmp/duplicate.err"

echo "k26 runtime budget checker self-test PASS"
