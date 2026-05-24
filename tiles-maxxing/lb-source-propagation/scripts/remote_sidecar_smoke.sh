#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  remote_sidecar_smoke.sh [--repo DIR] [--build-dir DIR] [--verify-build-dir DIR]
                          [--k-sq N] [--out-dir DIR]

Build and run the LB source-propagation sidecar smoke gate on a remote host.
This script performs no Vast API actions and starts no long campaign. It is
intended to run after the repo has been copied to a rented host.

Defaults:
  --repo             current working directory
  --build-dir        /tmp/gm-lbsp-remote-smoke
  --verify-build-dir /tmp/gm-lbsp-remote-verify
  --k-sq             26
  --out-dir          <repo>/tiles-maxxing/lb-source-propagation/artifacts/remote-smoke
USAGE
}

repo_dir="$(pwd)"
build_dir="/tmp/gm-lbsp-remote-smoke"
verify_build_dir="/tmp/gm-lbsp-remote-verify"
k_sq="26"
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
    --verify-build-dir)
      verify_build_dir="$2"
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
verification_dir="$repo_dir/verification"
if [[ ! -f "$verification_dir/CMakeLists.txt" ]]; then
  echo "verification CMakeLists.txt not found at $verification_dir" >&2
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

cmake -S "$verification_dir" -B "$verify_build_dir" \
  | tee "$out_dir/verification-cmake-configure.log"
cmake --build "$verify_build_dir" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" \
  | tee "$out_dir/verification-cmake-build.log"
ctest --test-dir "$verify_build_dir" --output-on-failure \
  | tee "$out_dir/verification-ctest.log"

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

"$build_dir/source_origin_cpu_runner" \
  --k-sq "$k_sq" \
  --r-final 248 \
  --band-width 128 \
  --endpoint-a 0 \
  --endpoint-b 251 \
  --live-manifest-out "$out_dir/source_origin_prefix_live_handoff.txt" \
  --prefix-witness-out "$out_dir/source_origin_prefix_witness.txt" \
  | tee "$out_dir/source_origin_prefix_manifest_smoke.json"

"$build_dir/source_tileop_port_runner" \
  --r-start 248 \
  --r-final 512 \
  --band-width 128 \
  --target-a 0 \
  --target-b 251 \
  --live-manifest-in "$out_dir/source_origin_prefix_live_handoff.txt" \
  --prefix-witness-in "$out_dir/source_origin_prefix_witness.txt" \
  | tee "$out_dir/source_tileop_cpu_runner_manifest_smoke.json" \
      "$out_dir/source_tileop_port_runner_live_handoff_smoke.json"

"$build_dir/k26_tsuchimura_preflight" \
  | tee "$out_dir/k26_tsuchimura_preflight.json"

"$build_dir/k26_source_run_contract" \
  | tee "$out_dir/k26_source_run_contract.json"

"$build_dir/k26_execution_plan" \
  | tee "$out_dir/k26_execution_plan.json"

"$build_dir/k26_bz_schedule_check" \
  | tee "$out_dir/k26_bz_schedule_check.json"

"$build_dir/k26_source_run_profile" \
  | tee "$out_dir/k26_source_run_profile.json"

"$build_dir/k26_source_run_commands" \
  | tee "$out_dir/k26_source_run_commands.json"

cat > "$out_dir/status.txt" <<'STATUS'
REMOTE_SIDECAR_SMOKE_PASS
Scope: sidecar build/test, independent verification CTest, CPU TileOp producer smoke, small coordinate source runner, TileOp-port live handoff smoke, K26 non-claim run contract, K26 non-claim execution plan, K26 BZ schedule evidence, K26 run profile draft, and K26 run command contract only.
Non-claim: this is not a sqrt(26) source/origin run and not a moat result.
STATUS

"$sidecar_dir/scripts/check_remote_smoke_artifacts.sh" "$out_dir" \
  | tee "$out_dir/artifact-check.log"

echo "remote sidecar smoke artifacts: $out_dir"
