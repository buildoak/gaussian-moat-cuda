#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_tileop_port_stream_equivalence.sh STREAM_RUNNER MATERIALIZED_RUNNER
      [--r-start R] [--r-final R] [--microband-width W]
      [--max-atoms N] [--tileop-threads N] [--out-dir DIR]

Compare the materialized TileOp-port runner against the diagnostic microband
stream runner, then check uninterrupted stream output against stop/resume.
USAGE
}

if [[ $# -lt 2 ]]; then
  usage >&2
  exit 2
fi

stream_runner="$1"
materialized_runner="$2"
shift 2

r_start="248"
r_final="512"
microband_width="128"
max_atoms="1000000"
tileop_threads="0"
out_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --r-start)
      r_start="$2"
      shift 2
      ;;
    --r-final)
      r_final="$2"
      shift 2
      ;;
    --microband-width)
      microband_width="$2"
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

for value_name in r_start r_final microband_width max_atoms tileop_threads; do
  value="${!value_name}"
  if [[ ! "$value" =~ ^[0-9]+$ ]]; then
    echo "--${value_name//_/-} must be a nonnegative integer: $value" >&2
    exit 2
  fi
done
if [[ "$r_start" == "0" || "$r_final" -le "$r_start" ||
      "$microband_width" == "0" || "$max_atoms" == "0" ]]; then
  echo "invalid radius/width/max-atoms arguments" >&2
  exit 2
fi
if [[ ! -x "$stream_runner" ]]; then
  echo "stream runner is not executable: $stream_runner" >&2
  exit 2
fi
if [[ ! -x "$materialized_runner" ]]; then
  echo "materialized runner is not executable: $materialized_runner" >&2
  exit 2
fi

if [[ -z "$out_dir" ]]; then
  out_dir="$(mktemp -d "${TMPDIR:-/tmp}/gm-lbsp-stream-equivalence.XXXXXX")"
else
  mkdir -p "$out_dir"
fi

materialized_json="$out_dir/materialized.json"
materialized_live="$out_dir/materialized.live-handoff.txt"
stream_json="$out_dir/stream.json"
stream_live="$out_dir/stream.live-handoff.txt"
stream_checkpoint="$out_dir/stream.checkpoint.txt"
chunk_json="$out_dir/chunk.json"
chunk_checkpoint="$out_dir/chunk.checkpoint.txt"
resumed_json="$out_dir/resumed.json"
resumed_live="$out_dir/resumed.live-handoff.txt"
resumed_checkpoint="$out_dir/resumed.checkpoint.txt"
mismatch_schedule_err="$out_dir/mismatch-schedule.err"
mismatch_source_mode_err="$out_dir/mismatch-source-mode.err"
mismatch_source_err="$out_dir/mismatch-source.err"
mismatch_build_err="$out_dir/mismatch-build.err"

"$materialized_runner" \
  --r-start "$r_start" \
  --r-final "$r_final" \
  --band-width "$((r_final - r_start))" \
  --schedule-radii "$r_start,$r_final" \
  --seed-inner-flags \
  --max-atoms "$max_atoms" \
  --tileop-threads "$tileop_threads" \
  --live-manifest-out "$materialized_live" \
  > "$materialized_json"

"$stream_runner" \
  --r-start "$r_start" \
  --r-final "$r_final" \
  --microband-width "$microband_width" \
  --seed-inner-flags \
  --max-atoms "$max_atoms" \
  --tileop-threads "$tileop_threads" \
  --live-manifest-out "$stream_live" \
  --checkpoint-out "$stream_checkpoint" \
  > "$stream_json"

python3 - "$materialized_json" "$stream_json" "$materialized_live" "$stream_live" <<'PY'
import json
import sys

materialized_json, stream_json, materialized_live, stream_live = sys.argv[1:]

def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)

def require_phase0_materialized(path, obj):
    expected_string = {
        "phase0_schema": "lb_diagnostic_phase0_v1",
        "runner_id": "source_tileop_port_runner_v1",
    }
    for key, expected in expected_string.items():
        if obj.get(key) != expected:
            raise SystemExit(
                f"{path}: {key} mismatch: {obj.get(key)!r} != {expected!r}"
            )
    integer_keys = [
        "rss_bytes",
        "peak_rss_bytes",
        "max_resident_tiles",
        "max_resident_tileops",
        "max_resident_port_atoms",
        "max_resident_edges",
        "max_live_frontier_atoms",
        "max_resident_components",
        "graph_ms",
        "process_ms",
        "handoff_ms",
        "total_ms",
    ]
    for key in integer_keys:
        value = obj.get(key)
        if not isinstance(value, int) or value < 0:
            raise SystemExit(f"{path}: missing/nonnegative integer {key}")

def parse_live(path):
    with open(path, "r", encoding="utf-8") as f:
        tokens = f.read().split()
    i = 0
    def expect(token):
        nonlocal i
        if i >= len(tokens) or tokens[i] != token:
            raise SystemExit(f"{path}: expected {token!r} at token {i}")
        i += 1
    def take():
        nonlocal i
        if i >= len(tokens):
            raise SystemExit(f"{path}: truncated manifest")
        value = tokens[i]
        i += 1
        return value
    expect("LB_SOURCE_LIVE_HANDOFF_V1")
    out = {}
    for key in (
        "k_sq", "cut_radius", "carry_width", "source_mode", "source_id",
        "geometry_id", "build_id", "schedule_digest_algorithm",
        "schedule_digest_hex", "overflow_summary",
    ):
        expect(key)
        out[key] = take()
    expect("carry_atoms")
    carry_count = int(take())
    carry = []
    for _ in range(carry_count):
        expect("carry_atom")
        carry.append((int(take()), int(take())))
    expect("components")
    component_count = int(take())
    components = []
    for _ in range(component_count):
        expect("component")
        source_bit = int(take())
        n = int(take())
        atoms = [int(take()) for _ in range(n)]
        components.append((source_bit, atoms))
    expect("END")
    if i != len(tokens):
        raise SystemExit(f"{path}: trailing tokens")
    out["carry_atoms"] = carry
    out["components"] = components
    return out

mat_json = load_json(materialized_json)
stream_json_obj = load_json(stream_json)
require_phase0_materialized(materialized_json, mat_json)
for label, obj in (("materialized", mat_json), ("stream", stream_json_obj)):
    if not obj.get("accepted"):
        raise SystemExit(f"{label} run was not accepted: {obj.get('reject_diagnostic')}")
    if obj.get("terminal_source_dead"):
        raise SystemExit(f"{label} run reached terminal source death")
    if not obj.get("has_source_carry"):
        raise SystemExit(f"{label} run has no source carry")
    if not obj.get("live_manifest_written"):
        raise SystemExit(f"{label} did not write live manifest")

mat = parse_live(materialized_live)
stream = parse_live(stream_live)
for key in ("k_sq", "cut_radius", "carry_width", "source_mode", "geometry_id", "build_id", "overflow_summary"):
    if mat[key] != stream[key]:
        raise SystemExit(f"handoff field mismatch {key}: {mat[key]!r} != {stream[key]!r}")
if mat["carry_atoms"] != stream["carry_atoms"]:
    raise SystemExit("carry atom set differs between materialized and stream")
if mat["components"] != stream["components"]:
    raise SystemExit("component partition/source bits differ between materialized and stream")

print(
    "MATERIALIZED_STREAM_HANDOFF_EQUIVALENCE_PASS "
    f"k_sq={mat['k_sq']} cut_radius={mat['cut_radius']} "
    f"carry_atoms={len(mat['carry_atoms'])} components={len(mat['components'])}"
)
PY

next_r_start="$((r_start + microband_width))"
if [[ "$next_r_start" -ge "$r_final" ]]; then
  echo "test requires at least two stream microbands" >&2
  exit 2
fi

"$stream_runner" \
  --r-start "$r_start" \
  --r-final "$r_final" \
  --microband-width "$microband_width" \
  --seed-inner-flags \
  --stop-after-microbands 1 \
  --max-atoms "$max_atoms" \
  --tileop-threads "$tileop_threads" \
  --checkpoint-out "$chunk_checkpoint" \
  > "$chunk_json"

"$stream_runner" \
  --r-start "$next_r_start" \
  --r-final "$r_final" \
  --microband-width "$microband_width" \
  --checkpoint-in "$chunk_checkpoint" \
  --max-atoms "$max_atoms" \
  --tileop-threads "$tileop_threads" \
  --live-manifest-out "$resumed_live" \
  --checkpoint-out "$resumed_checkpoint" \
  > "$resumed_json"

cmp "$stream_live" "$resumed_live"
cmp "$stream_checkpoint" "$resumed_checkpoint"

bad_width="$((microband_width + 1))"
if "$stream_runner" \
  --r-start "$next_r_start" \
  --r-final "$r_final" \
  --microband-width "$bad_width" \
  --checkpoint-in "$chunk_checkpoint" \
  > "$out_dir/mismatch-schedule.out" \
  2>"$mismatch_schedule_err"; then
  echo "stream runner accepted a mismatched checkpoint schedule" >&2
  exit 1
fi
grep -q 'wrong schedule_digest_hex' "$mismatch_schedule_err"

bad_source_mode_checkpoint="$out_dir/bad-source-mode.checkpoint.txt"
cp "$chunk_checkpoint" "$bad_source_mode_checkpoint"
perl -0pi -e 's/GEO_I_PORT_DIAGNOSTIC/NONE/g' \
  "$bad_source_mode_checkpoint"
if "$stream_runner" \
  --r-start "$next_r_start" \
  --r-final "$r_final" \
  --microband-width "$microband_width" \
  --checkpoint-in "$bad_source_mode_checkpoint" \
  > "$out_dir/mismatch-source-mode.out" \
  2>"$mismatch_source_mode_err"; then
  echo "stream runner accepted a mismatched checkpoint source mode" >&2
  exit 1
fi
grep -q 'wrong source_mode' "$mismatch_source_mode_err"

bad_source_checkpoint="$out_dir/bad-source.checkpoint.txt"
perl -0pi -e 's/tileop_port_stream_diagnostic_source_v1/tileop_port_stream_wrong_source_v1/g' \
  "$chunk_checkpoint"
mv "$chunk_checkpoint" "$bad_source_checkpoint"
if "$stream_runner" \
  --r-start "$next_r_start" \
  --r-final "$r_final" \
  --microband-width "$microband_width" \
  --checkpoint-in "$bad_source_checkpoint" \
  > "$out_dir/mismatch-source.out" \
  2>"$mismatch_source_err"; then
  echo "stream runner accepted a mismatched checkpoint source" >&2
  exit 1
fi
grep -q 'wrong source_id' "$mismatch_source_err"

"$stream_runner" \
  --r-start "$r_start" \
  --r-final "$r_final" \
  --microband-width "$microband_width" \
  --seed-inner-flags \
  --stop-after-microbands 1 \
  --max-atoms "$max_atoms" \
  --tileop-threads "$tileop_threads" \
  --checkpoint-out "$chunk_checkpoint" \
  > "$chunk_json"
bad_build_checkpoint="$out_dir/bad-build.checkpoint.txt"
cp "$chunk_checkpoint" "$bad_build_checkpoint"
perl -0pi -e 's/local_campaign_build/local_campaign_wrong_build/g' \
  "$bad_build_checkpoint"
if "$stream_runner" \
  --r-start "$next_r_start" \
  --r-final "$r_final" \
  --microband-width "$microband_width" \
  --checkpoint-in "$bad_build_checkpoint" \
  > "$out_dir/mismatch-build.out" \
  2>"$mismatch_build_err"; then
  echo "stream runner accepted a mismatched checkpoint build" >&2
  exit 1
fi
grep -q 'wrong build_id' "$mismatch_build_err"

if rg -n "make_tileop_port_band" \
  tiles-maxxing/lb-source-propagation/apps/source_tileop_port_stream_runner.cpp \
  tiles-maxxing/lb-source-propagation/src/tileop_port_stream.cpp; then
  echo "stream runner/library hot path references make_tileop_port_band" >&2
  exit 1
fi

{
  echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "stream_runner=$stream_runner"
  echo "materialized_runner=$materialized_runner"
  echo "r_start=$r_start"
  echo "r_final=$r_final"
  echo "microband_width=$microband_width"
  echo "materialized_live_sha256=$(shasum -a 256 "$materialized_live" | awk '{print $1}')"
  echo "stream_live_sha256=$(shasum -a 256 "$stream_live" | awk '{print $1}')"
  echo "stream_checkpoint_sha256=$(shasum -a 256 "$stream_checkpoint" | awk '{print $1}')"
} > "$out_dir/status.txt"

echo "TILEOP_PORT_STREAM_EQUIVALENCE_PASS out_dir=$out_dir"
