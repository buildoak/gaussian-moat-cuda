#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  remote_sidecar_smoke.sh [--repo DIR] [--build-dir DIR] [--k-sq N] [--out-dir DIR]

Build and run the LB source-propagation sidecar smoke gate on a remote host.
This script performs no Vast API actions and starts no long campaign. It is
intended to run after the repo has been copied to a rented host.

Defaults:
  --repo      current working directory
  --build-dir /tmp/gm-lbsp-remote-smoke
  --k-sq      36
  --out-dir   <repo>/tiles-maxxing/lb-source-propagation/artifacts/remote-smoke
USAGE
}

repo_dir="$(pwd)"
build_dir="/tmp/gm-lbsp-remote-smoke"
k_sq="36"
out_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      repo_dir="$2"
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

if [[ -z "$out_dir" ]]; then
  out_dir="$repo_dir/tiles-maxxing/lb-source-propagation/artifacts/remote-smoke"
fi

sidecar_dir="$repo_dir/tiles-maxxing/lb-source-propagation"
if [[ ! -f "$sidecar_dir/CMakeLists.txt" ]]; then
  echo "sidecar CMakeLists.txt not found at $sidecar_dir" >&2
  exit 2
fi

mkdir -p "$out_dir"

{
  echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "repo=$repo_dir"
  echo "commit=$(git -C "$repo_dir" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "branch=$(git -C "$repo_dir" branch --show-current 2>/dev/null || echo unknown)"
  echo "k_sq=$k_sq"
  if command -v nvidia-smi >/dev/null 2>&1; then
    echo "nvidia_smi_begin"
    nvidia-smi || true
    echo "nvidia_smi_end"
  else
    echo "nvidia_smi=unavailable"
  fi
} > "$out_dir/environment.txt"

cmake -S "$sidecar_dir" -B "$build_dir" -DK_SQ="$k_sq" \
  | tee "$out_dir/cmake-configure.log"
cmake --build "$build_dir" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" \
  | tee "$out_dir/cmake-build.log"
ctest --test-dir "$build_dir" --output-on-failure \
  | tee "$out_dir/ctest.log"

"$build_dir/source_prop_cpu_tileop_smoke" \
  | tee "$out_dir/source_prop_cpu_tileop_smoke.log"

"$build_dir/source_origin_cpu_runner" \
  --k-sq 26 \
  --r-final 12 \
  --band-width 6 \
  --endpoint-a 0 \
  --endpoint-b 3 \
  | tee "$out_dir/source_origin_cpu_runner_smoke.json"

"$build_dir/source_tileop_cpu_runner" \
  --r-start 248 \
  --r-final 512 \
  --band-width 128 \
  --seed-a 0 \
  --seed-b 251 \
  --endpoint-a 0 \
  --endpoint-b 251 \
  | tee "$out_dir/source_tileop_cpu_runner_smoke.json"

"$build_dir/k26_tsuchimura_preflight" \
  | tee "$out_dir/k26_tsuchimura_preflight.json"

"$build_dir/k26_source_run_contract" \
  | tee "$out_dir/k26_source_run_contract.json"

cat > "$out_dir/status.txt" <<'STATUS'
REMOTE_SIDECAR_SMOKE_PASS
Scope: sidecar build/test, CPU TileOp producer smoke, small coordinate source runner, CPU TileOp-fed source runner, and K26 non-claim run contract only.
Non-claim: this is not a sqrt(26) source/origin run and not a moat result.
STATUS

echo "remote sidecar smoke artifacts: $out_dir"
