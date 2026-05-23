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
chunk_json="$tmp/chunk.json"
chunk_manifest="$tmp/chunk-manifest.txt"
resumed_json="$tmp/resumed.json"
resumed_manifest="$tmp/resumed-manifest.txt"

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 248,375,512 \
  --seed-inner-flags \
  --manifest-out "$full_manifest" \
  > "$full_json"

"$runner" \
  --r-start 248 \
  --r-final 512 \
  --band-width 128 \
  --schedule-radii 248,375,512 \
  --seed-inner-flags \
  --stop-after-bands 1 \
  --manifest-out "$chunk_manifest" \
  > "$chunk_json"

grep -q '"r_final":375' "$chunk_json"
grep -q '"requested_r_final":512' "$chunk_json"
grep -q '"bands_processed":1' "$chunk_json"
grep -q '"manifest_written":true' "$chunk_json"

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

echo "tileop port runner resume self-test PASS"
