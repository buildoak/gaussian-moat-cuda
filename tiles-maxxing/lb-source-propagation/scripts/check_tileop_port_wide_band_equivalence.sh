#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_tileop_port_wide_band_equivalence.sh SOURCE_TILEOP_PORT_RUNNER
      [--r-start R] [--segments N] [--segment-width W]
      [--max-atoms N] [--tileop-threads N] [--out-dir DIR]

Compare one wide TileOp-port source-propagation band against the same radial
range split into equal explicit schedule bands. The gate requires:
  - both runs accepted,
  - both runs keep live source carry,
  - exact byte-identical final carry manifests,
  - key source inventory/carry JSON fields match.

Defaults are K26-oriented and power-of-two aligned:
  --r-start        8192
  --segments       6
  --segment-width  8192
  total width      49152

The runner must already be built with the intended K_SQ.
USAGE
}

if [[ $# -lt 1 ]]; then
  usage >&2
  exit 2
fi

runner="$1"
shift

r_start="8192"
segments="6"
segment_width="8192"
max_atoms="50000000"
tileop_threads="0"
out_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --r-start)
      r_start="$2"
      shift 2
      ;;
    --segments)
      segments="$2"
      shift 2
      ;;
    --segment-width)
      segment_width="$2"
      shift 2
      ;;
    --max-atoms)
      max_atoms="$2"
      shift 2
      ;;
    --tileop-threads)
      tileop_threads="$2"
      shift 2
      ;;
    --out-dir)
      out_dir="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

for value_name in r_start segments segment_width max_atoms tileop_threads; do
  value="${!value_name}"
  if [[ ! "$value" =~ ^[0-9]+$ ]]; then
    echo "--${value_name//_/-} must be a nonnegative integer: $value" >&2
    exit 2
  fi
done
if [[ "$segments" == "0" || "$segment_width" == "0" || "$max_atoms" == "0" ]]; then
  echo "--segments, --segment-width, and --max-atoms must be positive" >&2
  exit 2
fi
if [[ ! -x "$runner" ]]; then
  echo "runner is not executable: $runner" >&2
  exit 2
fi

r_final="$((r_start + segments * segment_width))"
wide_schedule="${r_start},${r_final}"
multi_schedule="$r_start"
for ((i = 1; i <= segments; ++i)); do
  multi_schedule+=",$((r_start + i * segment_width))"
done

if [[ -z "$out_dir" ]]; then
  out_dir="$(mktemp -d "${TMPDIR:-/tmp}/gm-lbsp-wide-equivalence.XXXXXX")"
else
  mkdir -p "$out_dir"
fi

wide_json="$out_dir/wide.json"
wide_manifest="$out_dir/wide-manifest.txt"
wide_progress="$out_dir/wide-progress.jsonl"
multi_json="$out_dir/multi.json"
multi_manifest="$out_dir/multi-manifest.txt"
multi_progress="$out_dir/multi-progress.jsonl"

common_args=(
  --r-start "$r_start"
  --r-final "$r_final"
  --seed-inner-flags
  --max-atoms "$max_atoms"
  --tileop-threads "$tileop_threads"
)

"$runner" \
  "${common_args[@]}" \
  --band-width "$((segments * segment_width))" \
  --schedule-radii "$wide_schedule" \
  --manifest-out "$wide_manifest" \
  --progress-out "$wide_progress" \
  > "$wide_json"

"$runner" \
  "${common_args[@]}" \
  --band-width "$segment_width" \
  --schedule-radii "$multi_schedule" \
  --manifest-out "$multi_manifest" \
  --progress-out "$multi_progress" \
  > "$multi_json"

python3 - "$wide_json" "$multi_json" <<'PY'
import json
import sys

wide_path, multi_path = sys.argv[1], sys.argv[2]
with open(wide_path, "r", encoding="utf-8") as f:
    wide = json.load(f)
with open(multi_path, "r", encoding="utf-8") as f:
    multi = json.load(f)

for label, obj in (("wide", wide), ("multi", multi)):
    if not obj.get("accepted"):
        raise SystemExit(f"{label} run was not accepted: {obj.get('reject_diagnostic')}")
    if obj.get("terminal_source_dead"):
        raise SystemExit(f"{label} run reached terminal source death; expected live carry")
    if not obj.get("has_source_carry"):
        raise SystemExit(f"{label} run has no live source carry")
    if not obj.get("manifest_written"):
        raise SystemExit(f"{label} run did not write carry manifest")

keys = [
    "k_sq",
    "r_start",
    "r_final",
    "requested_r_final",
    "tileop_overflows",
    "source_carry_atoms",
    "source_inventory_count",
    "source_inventory_digest_algorithm",
    "source_inventory_digest_hex",
    "max_source_norm_sq",
    "max_source_norm_atom_ids",
]
for key in keys:
    if wide.get(key) != multi.get(key):
        raise SystemExit(
            f"field mismatch for {key}: wide={wide.get(key)!r} multi={multi.get(key)!r}"
        )

if wide.get("bands_processed") != 1:
    raise SystemExit(f"wide bands_processed mismatch: {wide.get('bands_processed')}")
expected_multi_bands = multi.get("schedule_boundary_count", 0) - 1
if multi.get("bands_processed") != expected_multi_bands:
    raise SystemExit(
        "multi bands_processed mismatch: "
        f"{multi.get('bands_processed')} vs schedule {expected_multi_bands}"
    )

print(
    "JSON_EQUIVALENCE_PASS "
    f"k_sq={wide['k_sq']} r_start={wide['r_start']} r_final={wide['r_final']} "
    f"wide_bands={wide['bands_processed']} multi_bands={multi['bands_processed']} "
    f"source_carry_atoms={wide['source_carry_atoms']} "
    f"source_inventory_count={wide['source_inventory_count']}"
)
PY

cmp "$wide_manifest" "$multi_manifest"

{
  echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "runner=$runner"
  echo "r_start=$r_start"
  echo "r_final=$r_final"
  echo "segments=$segments"
  echo "segment_width=$segment_width"
  echo "wide_schedule=$wide_schedule"
  echo "multi_schedule=$multi_schedule"
  echo "wide_manifest_sha256=$(shasum -a 256 "$wide_manifest" | awk '{print $1}')"
  echo "multi_manifest_sha256=$(shasum -a 256 "$multi_manifest" | awk '{print $1}')"
} > "$out_dir/status.txt"

echo "TILEOP_PORT_WIDE_BAND_EQUIVALENCE_PASS out_dir=$out_dir"
