#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  remote_high_radius_cuda_campaign.sh [--repo DIR] [--out-dir DIR]
                                      [--build-dir DIR]
                                      [--k-sq N]
                                      [--radii CSV]
                                      [--chunk-size N]
                                      [--timeout-seconds N]

Run a bounded high-radius CUDA timing campaign on an already-rented remote host.
The campaign compares four stitched-size W=8192 annuli against one W=32768
annulus at each requested radius. Every row is full-ingest (--no-early-exit)
and diagnostic/non-claim.

Defaults:
  --repo             current working directory
  --out-dir          /workspace/high-radius-cuda-campaign
  --build-dir        /tmp/gm-high-radius-cuda-k36
  --k-sq             36
  --radii            60000000,80000000
  --chunk-size       200000
  --timeout-seconds  1200
USAGE
}

repo_dir="$(pwd)"
out_dir="/workspace/high-radius-cuda-campaign"
build_dir="/tmp/gm-high-radius-cuda-k36"
k_sq="36"
radii_csv="60000000,80000000"
chunk_size="200000"
timeout_seconds="1200"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      repo_dir="$2"
      shift 2
      ;;
    --out-dir)
      out_dir="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --k-sq)
      k_sq="$2"
      shift 2
      ;;
    --radii)
      radii_csv="$2"
      shift 2
      ;;
    --chunk-size)
      chunk_size="$2"
      shift 2
      ;;
    --timeout-seconds)
      timeout_seconds="$2"
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

require_positive_integer() {
  local value="$1"
  local label="$2"
  if ! [[ "$value" =~ ^[0-9]+$ ]] || [[ "$value" == "0" ]]; then
    echo "${label} must be a positive integer: ${value}" >&2
    exit 2
  fi
}

require_positive_integer "$k_sq" "--k-sq"
require_positive_integer "$chunk_size" "--chunk-size"
require_positive_integer "$timeout_seconds" "--timeout-seconds"

repo_dir="$(cd "$repo_dir" && pwd)"
cuda_dir="$repo_dir/tiles-maxxing/cuda-campaign-v2-sqrt-36"
if [[ ! -f "$cuda_dir/CMakeLists.txt" ]]; then
  echo "CUDA campaign CMakeLists.txt not found at $cuda_dir" >&2
  exit 2
fi

mkdir -p "$out_dir"/{logs,profiles,rows}
: > "$out_dir/rows.jsonl"

write_status() {
  printf '%s\n' "$1" > "$out_dir/status.txt"
}

write_environment() {
  {
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "hostname=$(hostname)"
    echo "repo=$repo_dir"
    echo "cuda_dir=$cuda_dir"
    echo "build_dir=$build_dir"
    echo "branch=$(git -C "$repo_dir" branch --show-current 2>/dev/null || echo unknown)"
    echo "commit=$(git -C "$repo_dir" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "commit_short=$(git -C "$repo_dir" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "k_sq=$k_sq"
    echo "radii=$radii_csv"
    echo "chunk_size=$chunk_size"
    echo "timeout_seconds=$timeout_seconds"
    echo "time_bin=$(command -v time || true)"
    echo "timeout_bin=$(command -v timeout || true)"
    echo "proof_status=DIAGNOSTIC_NON_CLAIM"
    if command -v nvidia-smi >/dev/null 2>&1; then
      echo "nvidia_smi_begin"
      nvidia-smi || true
      echo "nvidia_smi_end"
    else
      echo "nvidia_smi=unavailable"
    fi
  } > "$out_dir/environment.txt"
}

log() {
  printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" \
    | tee -a "$out_dir/logs/campaign.log"
}

run_logged() {
  local label="$1"
  shift
  log "RUN ${label}: $*"
  set +e
  "$@" > "$out_dir/logs/${label}.stdout.log" \
    2> "$out_dir/logs/${label}.stderr.log"
  local rc="$?"
  set -e
  echo "$rc" > "$out_dir/logs/${label}.exit_code.txt"
  log "END ${label} exit=${rc}"
  return "$rc"
}

run_row() {
  local label="$1"
  local r_inner="$2"
  local r_outer="$3"
  local width="$4"
  local segment_index="$5"
  local profile="$out_dir/profiles/${label}.profile.json"
  local stdout="$out_dir/rows/${label}.stdout.log"
  local stderr="$out_dir/rows/${label}.stderr.log"
  local time_file="$out_dir/rows/${label}.time.txt"
  local argv_file="$out_dir/rows/${label}.argv.txt"
  local exit_file="$out_dir/rows/${label}.exit_code.txt"
  local begin_epoch end_epoch rc
  local time_bin timeout_bin
  local cmd=(
    "$build_dir/campaign_main_cuda"
    "--k-sq=${k_sq}"
    "--r-inner=${r_inner}"
    "--r-outer=${r_outer}"
    --region full-octant
    "--chunk-size=${chunk_size}"
    --no-early-exit
    --timing
    --profile "$profile"
  )

  printf '%q ' "${cmd[@]}" > "$argv_file"
  printf '\n' >> "$argv_file"
  log "ROW ${label} r_inner=${r_inner} r_outer=${r_outer} width=${width}"
  begin_epoch="$(date +%s)"
  time_bin="$(command -v time || true)"
  timeout_bin="$(command -v timeout || true)"
  : > "$time_file"
  set +e
  if [[ -n "$time_bin" && "$time_bin" == /* && -n "$timeout_bin" ]]; then
    "$time_bin" -v -o "$time_file" "$timeout_bin" "$timeout_seconds" "${cmd[@]}" \
      > "$stdout" 2> "$stderr"
  elif [[ -n "$timeout_bin" ]]; then
    "$timeout_bin" "$timeout_seconds" "${cmd[@]}" \
      > "$stdout" 2> "$stderr"
  elif [[ -n "$time_bin" && "$time_bin" == /* ]]; then
    "$time_bin" -v -o "$time_file" "${cmd[@]}" \
      > "$stdout" 2> "$stderr"
  else
    "${cmd[@]}" > "$stdout" 2> "$stderr"
  fi
  rc="$?"
  end_epoch="$(date +%s)"
  set -e
  {
    echo "begin_epoch=$begin_epoch"
    echo "end_epoch=$end_epoch"
    echo "wall_seconds=$((end_epoch - begin_epoch))"
  } >> "$time_file"
  echo "$rc" > "$exit_file"
  printf '{"schema":"high_radius_cuda_row_v1","label":"%s","k_sq":%s,"r_inner":%s,"r_outer":%s,"width":%s,"segment_index":%s,"exit_code":%s,"profile":"profiles/%s.profile.json","stdout":"rows/%s.stdout.log","stderr":"rows/%s.stderr.log","time":"rows/%s.time.txt"}\n' \
    "$label" "$k_sq" "$r_inner" "$r_outer" "$width" "$segment_index" \
    "$rc" "$label" "$label" "$label" "$label" >> "$out_dir/rows.jsonl"
  log "END_ROW ${label} exit=${rc} wall_seconds=$((end_epoch - begin_epoch))"
}

summarize() {
  python3 - "$out_dir" <<'PY'
import json
import pathlib
import sys

out = pathlib.Path(sys.argv[1])
rows = []
for line in (out / "rows.jsonl").read_text().splitlines():
    if not line.strip():
        continue
    row = json.loads(line)
    profile_path = out / row["profile"]
    profile = {}
    if profile_path.exists() and row["exit_code"] == 0:
        profile = json.loads(profile_path.read_text())
    timings = profile.get("timings_seconds", {})
    tiles = profile.get("tiles", {})
    overflows = profile.get("overflow_counters", {})
    row.update({
        "verdict": profile.get("verdict"),
        "cuda_k1_k5_seconds": timings.get("cuda_k1_k5"),
        "compositor_seconds": timings.get("compositor"),
        "grid_seconds": timings.get("grid"),
        "total_seconds": timings.get("total"),
        "active_tiles": tiles.get("active"),
        "produced_tiles": tiles.get("produced"),
        "ingested_tiles": tiles.get("ingested"),
        "overflow_counters": overflows,
        "overflow_clean": all(v == 0 for v in overflows.values()) if overflows else None,
    })
    rows.append(row)

groups = {}
for row in rows:
    base = row["r_inner"] - (row["r_inner"] % 32768)
    # Labels contain the exact anchor after r.
    parts = row["label"].split("_")
    anchor = None
    for part in parts:
        if part.startswith("r") and part[1:].isdigit():
            anchor = int(part[1:])
            break
    groups.setdefault(anchor if anchor is not None else base, []).append(row)

comparisons = []
for anchor, group in sorted(groups.items()):
    wide = [r for r in group if r["width"] == 32768 and r["exit_code"] == 0]
    segs = sorted(
        [r for r in group if r["width"] == 8192 and r["exit_code"] == 0],
        key=lambda r: r["segment_index"],
    )
    if len(wide) == 1 and len(segs) == 4:
        total_4 = sum(float(r.get("total_seconds") or 0) for r in segs)
        cuda_4 = sum(float(r.get("cuda_k1_k5_seconds") or 0) for r in segs)
        comp_4 = sum(float(r.get("compositor_seconds") or 0) for r in segs)
        comparisons.append({
            "anchor": anchor,
            "four_by_8192_total_seconds": total_4,
            "one_by_32768_total_seconds": wide[0].get("total_seconds"),
            "total_ratio_four_over_wide": (
                total_4 / float(wide[0]["total_seconds"])
                if wide[0].get("total_seconds") else None
            ),
            "four_by_8192_cuda_seconds": cuda_4,
            "one_by_32768_cuda_seconds": wide[0].get("cuda_k1_k5_seconds"),
            "four_by_8192_compositor_seconds": comp_4,
            "one_by_32768_compositor_seconds": wide[0].get("compositor_seconds"),
            "four_by_8192_tiles": sum(int(r.get("ingested_tiles") or 0) for r in segs),
            "one_by_32768_tiles": wide[0].get("ingested_tiles"),
        })

status = "HIGH_RADIUS_CUDA_CAMPAIGN_PASS"
if any(r["exit_code"] != 0 for r in rows):
    status = "HIGH_RADIUS_CUDA_CAMPAIGN_ROW_FAILED"
elif any(r.get("overflow_clean") is not True for r in rows):
    status = "HIGH_RADIUS_CUDA_CAMPAIGN_OVERFLOW_DIRTY"

summary = {
    "schema": "high_radius_cuda_campaign_summary_v1",
    "proof_status": "DIAGNOSTIC_NON_CLAIM",
    "status": status,
    "rows": rows,
    "comparisons": comparisons,
}
(out / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

lines = [
    "# High-Radius CUDA Campaign",
    "",
    f"Status: `{status}`",
    "",
    "Proof status: `DIAGNOSTIC_NON_CLAIM`",
    "",
    "## Rows",
    "",
]
for row in rows:
    lines.append(
        f"- `{row['label']}`: exit `{row['exit_code']}`, "
        f"verdict `{row.get('verdict')}`, tiles `{row.get('ingested_tiles')}`, "
        f"cuda `{row.get('cuda_k1_k5_seconds')}`, compositor `{row.get('compositor_seconds')}`, "
        f"total `{row.get('total_seconds')}`"
    )
lines.extend(["", "## Comparisons", ""])
for comp in comparisons:
    lines.append(
        f"- `R={comp['anchor']}`: 4x8192 total "
        f"`{comp['four_by_8192_total_seconds']:.3f}s`, 1x32768 total "
        f"`{float(comp['one_by_32768_total_seconds']):.3f}s`, ratio "
        f"`{comp['total_ratio_four_over_wide']:.3f}`"
    )
lines.extend(["", "This is performance evidence only, not a moat proof."])
(out / "summary.md").write_text("\n".join(lines) + "\n")
print(status)
PY
}

write_status "HIGH_RADIUS_CUDA_CAMPAIGN_RUNNING"
write_environment

run_logged cmake-configure cmake -S "$cuda_dir" -B "$build_dir" \
  -DK_SQ="$k_sq" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=89
run_logged cmake-build cmake --build "$build_dir" --target campaign_main_cuda \
  -j"$(nproc 2>/dev/null || echo 8)"

IFS=',' read -r -a radii <<< "$radii_csv"
for radius in "${radii[@]}"; do
  require_positive_integer "$radius" "--radii entry"
  for segment in 0 1 2 3; do
    r_inner=$((radius + segment * 8192))
    r_outer=$((r_inner + 8192))
    run_row "k${k_sq}_r${radius}_w8192_s${segment}" \
      "$r_inner" "$r_outer" 8192 "$segment"
  done
  run_row "k${k_sq}_r${radius}_w32768" \
    "$radius" "$((radius + 32768))" 32768 -1
done

summary_status="$(summarize)"
write_status "$summary_status"
echo "high-radius CUDA campaign artifacts: $out_dir"
